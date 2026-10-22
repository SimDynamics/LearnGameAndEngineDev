#include "presentation/windows/icons.hpp"
#include "native_build_config.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

namespace lgaed::presentation {
namespace {
struct Layer { std::string path; bool fill = true; float stroke = 0; };
struct Vector { float width=24,height=24; std::vector<Layer> layers; };
std::string attr(const std::string& xml,const std::string& key) {
    const auto at=xml.find("android:"+key+"=\"");
    if(at==std::string::npos) return {};
    const auto start=at+key.size()+10, end=xml.find('"',start);
    return end==std::string::npos?std::string{}:xml.substr(start,end-start);
}
Vector load(Icon icon) {
    const wchar_t* file=nullptr;
    switch(icon) {
    case Icon::home:file=L"ic_home.xml";break;
    case Icon::settings:file=L"ic_settings.xml";break;
    case Icon::speaker:file=L"ic_person_speaking.xml";break;
    case Icon::language:file=L"ic_lesson_language.xml";break;
    case Icon::terminal:file=L"ic_attempt_programming.xml";break;
    case Icon::copy:file=L"ic_code_copy.xml";break;
    case Icon::reset:file=L"ic_code_reset.xml";break;
    case Icon::expand:file=L"ic_code_fullscreen.xml";break;
    default:break;
    }
    Vector result;
    if(file) {
        std::ifstream input(std::filesystem::path(build::source_root)/L"app/src/main/res/drawable"/file);
        const std::string xml((std::istreambuf_iterator<char>(input)),{});
        const auto width=attr(xml,"viewportWidth"),height=attr(xml,"viewportHeight");
        if(!width.empty())result.width=std::strtof(width.c_str(),nullptr);
        if(!height.empty())result.height=std::strtof(height.c_str(),nullptr);
        std::size_t at=0;
        while((at=xml.find("<path",at))!=std::string::npos) {
            const auto end=xml.find('>',at);if(end==std::string::npos)break;
            const auto tag=xml.substr(at,end-at+1); at=end+1;
            const auto fill=attr(tag,"fillColor"),stroke=attr(tag,"strokeWidth");
            result.layers.push_back({attr(tag,"pathData"),!fill.empty()&&fill.find("transparent")==std::string::npos,
                stroke.empty()?0:std::strtof(stroke.c_str(),nullptr)});
        }
    }
    if(!result.layers.empty())return result;
    const char* path="";
    switch(icon) {
    case Icon::back:path="M20,12H4M11,5L4,12L11,19";break;
    case Icon::play:result.layers.push_back({"M7,3L21,12L7,21Z",true,0});return result;
    case Icon::pause:result.layers.push_back({"M6,3H10V21H6ZM14,3H18V21H14Z",true,0});return result;
    case Icon::undo:path="M4,9H12C21,9 21,20 12,20M4,9L9,4M4,9L9,14";break;
    case Icon::redo:path="M20,9H12C3,9 3,20 12,20M20,9L15,4M20,9L15,14";break;
    case Icon::rewind3:case Icon::rewind5:path="M3,10C5,3 18,3 20,11M3,10V4M3,10H9";break;
    case Icon::forward3:case Icon::forward5:path="M21,10C19,3 6,3 4,11M21,10V4M21,10H15";break;
    default:path="M4,4H20V20H4Z";break;
    }
    result.layers.push_back({path,false,1.8f});return result;
}
struct Point { double x=0,y=0; };
void arc(Gdiplus::GraphicsPath& out,Point from,Point to,double rx,double ry,double phi,bool large,bool sweep) {
    rx=std::abs(rx);ry=std::abs(ry);
    if(rx==0||ry==0) {out.AddLine(static_cast<float>(from.x),static_cast<float>(from.y),static_cast<float>(to.x),static_cast<float>(to.y));return;}
    if(from.x==to.x&&from.y==to.y)return;
    const auto pi=std::numbers::pi;
    phi*=pi/180;const auto c=std::cos(phi),s=std::sin(phi);
    const auto xp=c*(from.x-to.x)/2+s*(from.y-to.y)/2;
    const auto yp=-s*(from.x-to.x)/2+c*(from.y-to.y)/2;
    const auto lambda=xp*xp/(rx*rx)+yp*yp/(ry*ry);
    if(lambda>1){rx*=std::sqrt(lambda);ry*=std::sqrt(lambda);}
    const auto denominator=rx*rx*yp*yp+ry*ry*xp*xp;
    const auto factor=(large==sweep?-1:1)*std::sqrt(std::max(0.0,(rx*rx*ry*ry-denominator)/denominator));
    const auto cxp=factor*rx*yp/ry,cyp=-factor*ry*xp/rx;
    const auto cx=c*cxp-s*cyp+(from.x+to.x)/2,cy=s*cxp+c*cyp+(from.y+to.y)/2;
    const auto start=std::atan2((yp-cyp)/ry,(xp-cxp)/rx);
    auto delta=std::atan2((-yp-cyp)/ry,(-xp-cxp)/rx)-start;
    if(!sweep&&delta>0)delta-=2*pi;if(sweep&&delta<0)delta+=2*pi;
    const auto count=std::max(1,static_cast<int>(std::ceil(std::abs(delta)/(pi/2))));
    const auto step=delta/count;
    const auto point=[&](double t){return Point{cx+c*rx*std::cos(t)-s*ry*std::sin(t),cy+s*rx*std::cos(t)+c*ry*std::sin(t)};};
    const auto tangent=[&](double t){return Point{-c*rx*std::sin(t)-s*ry*std::cos(t),-s*rx*std::sin(t)+c*ry*std::cos(t)};};
    for(int i=0;i<count;++i){
        const auto a=start+i*step,b=a+step,k=4.0/3*std::tan(step/4);
        const auto p=point(a),q=point(b),u=tangent(a),v=tangent(b);
        out.AddBezier(static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.x+k*u.x),static_cast<float>(p.y+k*u.y),
            static_cast<float>(q.x-k*v.x),static_cast<float>(q.y-k*v.y),static_cast<float>(q.x),static_cast<float>(q.y));
    }
}
void parse(Gdiplus::GraphicsPath& out,const std::string& source) {
    const char* p=source.c_str();Point current{},start{},control{};char command=0,previous=0;
    const auto skip=[&]{while(*p==' '||*p==','||*p=='\r'||*p=='\n'||*p=='\t')++p;};
    const auto number=[&]{skip();char* end=nullptr;const auto n=std::strtod(p,&end);if(end==p)throw 0;p=end;return n;};
    try {while(*p){skip();if(!*p)break;
        if((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z'))command=*p++;
        const auto relative=command>='a'&&command<='z';const auto kind=relative?command-32:command;
        const auto base=current;
        const auto point=[&]{Point q{number(),number()};if(relative){q.x+=base.x;q.y+=base.y;}return q;};
        Point next=current;
        switch(kind){
        case 'M':next=point();out.StartFigure();start=next;command=relative?'l':'L';break;
        case 'L':next=point();out.AddLine(static_cast<float>(current.x),static_cast<float>(current.y),static_cast<float>(next.x),static_cast<float>(next.y));break;
        case 'H':next.x=number()+(relative?base.x:0);out.AddLine(static_cast<float>(current.x),static_cast<float>(current.y),static_cast<float>(next.x),static_cast<float>(next.y));break;
        case 'V':next.y=number()+(relative?base.y:0);out.AddLine(static_cast<float>(current.x),static_cast<float>(current.y),static_cast<float>(next.x),static_cast<float>(next.y));break;
        case 'C':case 'S':{Point a{};if(kind=='C')a=point();else a=(previous=='C'||previous=='S')?Point{2*current.x-control.x,2*current.y-control.y}:current;
            const auto b=point();next=point();out.AddBezier(static_cast<float>(current.x),static_cast<float>(current.y),static_cast<float>(a.x),static_cast<float>(a.y),static_cast<float>(b.x),static_cast<float>(b.y),static_cast<float>(next.x),static_cast<float>(next.y));control=b;break;}
        case 'Q':case 'T':{Point a{};if(kind=='Q')a=point();else a=(previous=='Q'||previous=='T')?Point{2*current.x-control.x,2*current.y-control.y}:current;
            next=point();out.AddBezier(static_cast<float>(current.x),static_cast<float>(current.y),static_cast<float>(current.x+(a.x-current.x)*2/3),static_cast<float>(current.y+(a.y-current.y)*2/3),static_cast<float>(next.x+(a.x-next.x)*2/3),static_cast<float>(next.y+(a.y-next.y)*2/3),static_cast<float>(next.x),static_cast<float>(next.y));control=a;break;}
        case 'A':{const auto rx=number(),ry=number(),angle=number(),large=number(),sweep=number();next=point();arc(out,current,next,rx,ry,angle,large!=0,sweep!=0);break;}
        case 'Z':out.CloseFigure();next=start;command=0;break;
        default:return;
        }
        previous=static_cast<char>(kind);current=next;
    }}catch(int){} // Malformed optional assets cannot break navigation controls.
}
}
void draw_icon(HDC dc,RECT bounds,Icon icon,COLORREF tint) {
    struct Gdi {ULONG_PTR token{};Gdi(){Gdiplus::GdiplusStartupInput in;Gdiplus::GdiplusStartup(&token,&in,nullptr);}~Gdi(){if(token)Gdiplus::GdiplusShutdown(token);}};
    static Gdi startup;
    static std::map<Icon,Vector> cache;
    static std::mutex mutex;
    Vector vector;
    {const std::lock_guard lock(mutex);auto it=cache.find(icon);if(it==cache.end())it=cache.emplace(icon,load(icon)).first;vector=it->second;}
    if(vector.width<=0||vector.height<=0)return;
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const auto scale=std::min((bounds.right-bounds.left)/vector.width,(bounds.bottom-bounds.top)/vector.height);
    graphics.TranslateTransform(bounds.left+(bounds.right-bounds.left-vector.width*scale)/2,bounds.top+(bounds.bottom-bounds.top-vector.height*scale)/2);
    graphics.ScaleTransform(scale,scale);
    const Gdiplus::Color color(255,GetRValue(tint),GetGValue(tint),GetBValue(tint));
    for(const auto& layer:vector.layers){Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);parse(path,layer.path);
        if(layer.fill){Gdiplus::SolidBrush brush(color);graphics.FillPath(&brush,&path);}
        if(layer.stroke>0){Gdiplus::Pen pen(color,layer.stroke);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);graphics.DrawPath(&pen,&path);}}
    if(icon==Icon::rewind3||icon==Icon::rewind5||icon==Icon::forward3||icon==Icon::forward5){
        Gdiplus::FontFamily family(L"Segoe UI");Gdiplus::Font font(&family,11,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);Gdiplus::SolidBrush brush(color);
        graphics.DrawString(icon==Icon::rewind3||icon==Icon::forward3?L"3":L"5",-1,&font,Gdiplus::PointF(9,11),&brush);
    }
}
}
