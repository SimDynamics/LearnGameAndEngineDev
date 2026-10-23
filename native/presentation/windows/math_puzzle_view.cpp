#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "platform/windows/winrt_apartment.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr wchar_t view_class[] = L"LearnGameAndEngineDev.MathPuzzle";
constexpr wchar_t canvas_class[] = L"LearnGameAndEngineDev.MathCanvas";
constexpr wchar_t canvas_fullscreen_class[] = L"LearnGameAndEngineDev.MathCanvasFullscreen";
constexpr int canvas_close_id=6401;
constexpr wchar_t keyboard_context_property[]=L"LGAED.MathKeyboardContext";
std::string text(const JsonObject& object,const wchar_t* key,std::string fallback = {}) {
    const auto value = object.TryLookup(key);
    return value && value.ValueType() == JsonValueType::String ? winrt::to_string(value.GetString()) : fallback;
}
double number(const JsonObject& object,const wchar_t* key,double fallback = 0) {
    const auto value = object.TryLookup(key); return value && value.ValueType() == JsonValueType::Number ? value.GetNumber() : fallback;
}
bool flag(const JsonObject& object,const wchar_t* key,bool fallback = false) {
    const auto value = object.TryLookup(key); return value && value.ValueType() == JsonValueType::Boolean ? value.GetBoolean() : fallback;
}
JsonObject object(const JsonObject& parent,const wchar_t* key) {
    const auto value = parent.TryLookup(key); return value && value.ValueType() == JsonValueType::Object ? value.GetObject() : JsonObject{};
}
JsonArray array(const JsonObject& parent,const wchar_t* key) {
    const auto value = parent.TryLookup(key); return value && value.ValueType() == JsonValueType::Array ? value.GetArray() : JsonArray{};
}
std::string stringify(const IJsonValue& value) { return winrt::to_string(value.Stringify()); }
JsonValue string_value(std::string_view value) { return JsonValue::CreateStringValue(winrt::to_hstring(value)); }
JsonObject answer(std::string_view type) { JsonObject result; result.SetNamedValue(L"type",string_value(type)); return result; }
double snapped(double value,double step,double low,double high) {
    if (step > 0) value = low + std::round((value - low) / step) * step;
    return std::clamp(value,low,high);
}
std::string decimal(double value) {
    auto result = std::to_string(value); while (result.size() > 1 && result.back() == '0') result.pop_back();
    if (result.back() == '.') result.pop_back(); return result;
}
std::string visible_label(const JsonObject& item) {
    return text(item,L"label",text(item,L"text",text(item,L"value")));
}
bool own_vertical_scroll(HWND item) {
    if (!(GetWindowLongPtrW(item,GWL_STYLE) & WS_VSCROLL)) return false;
    SCROLLINFO info{sizeof(info),SIF_RANGE | SIF_PAGE};
    return GetScrollInfo(item,SB_VERT,&info) && info.nPage > 0 && info.nMax - info.nMin + 1 > static_cast<int>(info.nPage);
}
bool passive_mouse_message(UINT message) {
    return message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK||message==WM_MOUSEMOVE||
        message==WM_CAPTURECHANGED||message==WM_CANCELMODE||message==WM_NCDESTROY;
}
LRESULT CALLBACK text_wheel_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR context) {
    const auto* callbacks=reinterpret_cast<const MathPuzzleCallbacks*>(context);
    if(callbacks&&callbacks->passive_gesture&&passive_mouse_message(message)) {
        const auto result=callbacks->passive_gesture(item,message,wparam,lparam);
        if(result&&message!=WM_NCDESTROY)return *result;
    }
    if (message == WM_MOUSEWHEEL && !own_vertical_scroll(item)) return SendMessageW(GetParent(item),message,wparam,lparam);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(item,text_wheel_proc,14);
    return DefSubclassProc(item,message,wparam,lparam);
}
struct PlotPoint { double x = 0,y = 0; std::string id; std::vector<std::string> labels; bool draggable = false; };
struct Canvas {
    HWND window = nullptr;
    HWND fullscreen=nullptr,inline_parent=nullptr,previous_focus=nullptr,expand_button=nullptr;
    RECT inline_rectangle{};
    HFONT scaled_font = nullptr;
    JsonObject config, state;
    std::string type;
    std::string locale;
    std::function<void(JsonObject)> changed;
    std::function<void()> sync_fields;
    std::vector<PlotPoint> points;
    std::optional<std::size_t> dragging;
    double xmin = -5,xmax = 5,ymin = -5,ymax = 5;
    double zoom=1,pan_x=0,pan_y=0;
    POINT pan_last{};bool panning=false;
    std::optional<JsonObject> number_line_click;
    POINT number_line_click_start{};
    ~Canvas() { close_expanded();if (scaled_font) DeleteObject(scaled_font); }
    std::string tr(const char* es,const char* en,const char* fr)const{return locale.starts_with("fr")?fr:locale.starts_with("en")?en:es;}
    void clamp_pan(){RECT client{};GetClientRect(window,&client);const auto x=std::max(0.,client.right*(zoom-1)/2),y=std::max(0.,client.bottom*(zoom-1)/2);pan_x=std::clamp(pan_x,-x,x);pan_y=std::clamp(pan_y,-y,y);}
    POINT content_point(POINT at)const{RECT client{};GetClientRect(window,&client);return {
        static_cast<LONG>(std::lround((at.x-client.right/2.-pan_x)/zoom+client.right/2.)),
        static_cast<LONG>(std::lround((at.y-client.bottom/2.-pan_y)/zoom+client.bottom/2.))};}
    void toggle_zoom(POINT focus){
        if(zoom>1){zoom=1;pan_x=pan_y=0;}
        else {RECT client{};GetClientRect(window,&client);zoom=2;pan_x=-(focus.x-client.right/2.);pan_y=-(focus.y-client.bottom/2.);clamp_pan();}
        InvalidateRect(window,nullptr,FALSE);
    }
    double aspect_ratio()const{
        if(type=="math_number_line"||type=="number_line")return 2.4;
        if(type=="euclidean_diagram")return 1.4;
        if(type=="geometry_3d_diagram"||type=="venn_diagram")return 1.2;
        if(type=="math_coordinate_graph"||type=="coordinate_graph"||type=="graph")return std::clamp((xmax-xmin)/(ymax-ymin),.6,4.);
        return 1.;
    }
    void expanded_layout(){
        if(!fullscreen)return;RECT area{};GetClientRect(fullscreen,&area);const int row=px(48),available=std::max(1,static_cast<int>(area.bottom)-row);
        const auto ratio=aspect_ratio();const int width=std::max(1,std::min(static_cast<int>(area.right),static_cast<int>(available*ratio))),height=std::max(1,std::min(available,static_cast<int>(width/ratio)));
        MoveWindow(window,(area.right-width)/2,(available-height)/2,width,height,TRUE);
        MoveWindow(GetDlgItem(fullscreen,canvas_close_id),std::max(0L,area.right-row),available,row,row,TRUE);clamp_pan();
    }
    void close_expanded(){
        if(!fullscreen)return;const auto frame=fullscreen;fullscreen=nullptr;zoom=1;pan_x=pan_y=0;panning=false;dragging.reset();
        if(GetCapture()==window)ReleaseCapture();
        if(IsWindow(window)&&IsWindow(inline_parent)){SetParent(window,inline_parent);MoveWindow(window,inline_rectangle.left,inline_rectangle.top,inline_rectangle.right-inline_rectangle.left,inline_rectangle.bottom-inline_rectangle.top,TRUE);}
        if(IsWindow(frame))DestroyWindow(frame);if(IsWindow(previous_focus)&&IsWindowEnabled(previous_focus))SetFocus(previous_focus);
    }
    static LRESULT CALLBACK exit_key_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
        if(message==WM_KEYDOWN&&wp==VK_ESCAPE){SendMessageW(GetParent(item),WM_CLOSE,0,0);return 0;}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(item,exit_key_proc,id);
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK fullscreen_proc(HWND target,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Canvas*>(GetWindowLongPtrW(target,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Canvas*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(target,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(target,message,wp,lp);
        if(message==WM_SIZE){self->expanded_layout();return 0;}
        if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(wp)==canvas_close_id)||(message==WM_KEYDOWN&&wp==VK_ESCAPE)){self->close_expanded();return 0;}
        if(message==WM_ERASEBKGND){RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::palette(target).high_contrast?GetSysColorBrush(COLOR_WINDOW):reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));return 1;}
        if(message==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item.CtlID==canvas_close_id){
            FillRect(item.hDC,&item.rcItem,reader_theme::palette(target).high_contrast?GetSysColorBrush(COLOR_WINDOW):reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));auto icon=item.rcItem;InflateRect(&icon,-self->px(12),-self->px(12));draw_icon(item.hDC,icon,Icon::close,reader_theme::palette(target).high_contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(190,195,205));return TRUE;}}
        return DefWindowProcW(target,message,wp,lp);
    }
    void expand(){
        if(fullscreen||!IsWindow(window))return;previous_focus=GetFocus();inline_parent=GetParent(window);GetWindowRect(window,&inline_rectangle);MapWindowPoints(nullptr,inline_parent,reinterpret_cast<POINT*>(&inline_rectangle),2);
        WNDCLASSEXW definition{sizeof(definition)};definition.lpfnWndProc=fullscreen_proc;definition.hInstance=GetModuleHandleW(nullptr);definition.lpszClassName=canvas_fullscreen_class;definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassExW(&definition)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register the expanded mathematics viewport");
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);const auto area=monitor.rcMonitor;
        fullscreen=CreateWindowExW(WS_EX_CONTROLPARENT,canvas_fullscreen_class,wide(tr("Representación ampliada","Expanded representation","Représentation agrandie")).c_str(),WS_POPUP|WS_CLIPCHILDREN,area.left,area.top,area.right-area.left,area.bottom-area.top,GetAncestor(window,GA_ROOT),nullptr,GetModuleHandleW(nullptr),this);
        if(!fullscreen)throw std::runtime_error("Cannot expand the mathematics representation");
        reader_theme::apply(fullscreen,reader_theme::Surface::card);reader_theme::set_accent(fullscreen,reader_theme::palette(inline_parent).accent);
        const auto close=CreateWindowExW(0,L"BUTTON",wide(tr("Cerrar representación ampliada","Close expanded representation","Fermer la représentation agrandie")).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,1,1,fullscreen,reinterpret_cast<HMENU>(static_cast<INT_PTR>(canvas_close_id)),GetModuleHandleW(nullptr),nullptr);
        if(!close){DestroyWindow(fullscreen);fullscreen=nullptr;throw std::runtime_error("Cannot create the expanded mathematics exit control");}
        reader_theme::apply(close,reader_theme::Surface::card);reader_theme::icon_button(close);
        SetWindowSubclass(close,exit_key_proc,19,0);
        SetParent(window,fullscreen);expanded_layout();ShowWindow(fullscreen,SW_SHOW);SetForegroundWindow(fullscreen);SetFocus(window);
    }
    void scale_text(int percentage) {
        const auto next=CreateFontW(-MulDiv(18*percentage,static_cast<int>(GetDpiForWindow(window)),96*100),
            0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(next){if(scaled_font)DeleteObject(scaled_font);scaled_font=next;}InvalidateRect(window,nullptr,FALSE);
    }
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96); }
    RECT bounds() const { RECT result{}; GetClientRect(window,&result); InflateRect(&result,-px(38),-px(28)); return result; }
    POINT project(double x,double y) const {
        const auto rect = bounds(); return {rect.left + static_cast<LONG>(std::lround((x - xmin) * (rect.right - rect.left) / (xmax - xmin))),
            rect.bottom - static_cast<LONG>(std::lround((y - ymin) * (rect.bottom - rect.top) / (ymax - ymin)))};
    }
    void initialize() {
        const auto limits = object(config,L"bounds"); xmin = number(limits,L"xMin",-5); xmax = number(limits,L"xMax",5);
        ymin = number(limits,L"yMin",-5); ymax = number(limits,L"yMax",5);
        auto values = array(state,L"points"); if (!values.Size()) values = array(config,L"points");
        const auto initial = array(config,L"points");
        for (std::uint32_t index = 0; index < values.Size(); ++index) {
            const auto item = values.GetObjectAt(index); PlotPoint point;
            point.x = number(item,L"x"); point.y = number(item,L"y"); point.id = text(item,L"id");
            if (index < initial.Size()) point.draggable = flag(initial.GetObjectAt(index),L"draggable",changed != nullptr);
            for (const auto& label : array(item,L"labels")) if (label.ValueType() == JsonValueType::String) point.labels.push_back(winrt::to_string(label.GetString()));
            points.push_back(std::move(point));
        }
        if (type == "euclidean_diagram" && !points.empty()) {
            xmin = xmax = points.front().x; ymin = ymax = points.front().y;
            for (const auto& point : points) { xmin = std::min(xmin,point.x); xmax = std::max(xmax,point.x); ymin = std::min(ymin,point.y); ymax = std::max(ymax,point.y); }
            for (const auto& value : array(config,L"circles")) {
                const auto circle = value.GetObject(); const auto center = find(text(circle,L"center"));
                if (!center) continue; const auto radius = number(circle,L"radius");
                xmin = std::min(xmin,center->x - radius); xmax = std::max(xmax,center->x + radius);
                ymin = std::min(ymin,center->y - radius); ymax = std::max(ymax,center->y + radius);
            }
            const auto span = std::max({xmax - xmin,ymax - ymin,1.0});
            const auto cx = (xmin + xmax) / 2,cy = (ymin + ymax) / 2;
            xmin = cx - span * .7; xmax = cx + span * .7; ymin = cy - span * .7; ymax = cy + span * .7;
        }
        if (xmax <= xmin) xmax = xmin + 1; if (ymax <= ymin) ymax = ymin + 1;
    }
    const PlotPoint* find(const std::string& id) const {
        const auto found = std::ranges::find(points,id,&PlotPoint::id); return found == points.end() ? nullptr : &*found;
    }
    void label(HDC dc,POINT point,std::string_view value,int dx = 6,int dy = -22) const {
        const auto content = wide(value); RECT rect{point.x + px(dx),point.y + px(dy),point.x + px(dx + 280),point.y + px(dy + 80)};
        DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&rect,DT_LEFT | DT_NOPREFIX | DT_WORDBREAK);
    }
    void line(HDC dc,POINT from,POINT to,bool arrow = false) const {
        MoveToEx(dc,from.x,from.y,nullptr); LineTo(dc,to.x,to.y);
        if (arrow) {
            const auto a = std::atan2(static_cast<double>(to.y - from.y),static_cast<double>(to.x - from.x));
            for (const auto delta : {-.45,.45}) {
                MoveToEx(dc,to.x,to.y,nullptr); LineTo(dc,to.x - static_cast<int>(px(10) * std::cos(a + delta)),
                    to.y - static_cast<int>(px(10) * std::sin(a + delta)));
            }
        }
    }
    void paint(HDC dc) {
        const auto colors = reader_theme::palette(window); RECT client{}; GetClientRect(window,&client);
        FillRect(dc,&client,reader_theme::brush(reader_theme::Surface::canvas));const auto transform=SaveDC(dc);
        if(zoom>1){SetGraphicsMode(dc,GM_ADVANCED);XFORM matrix{static_cast<FLOAT>(zoom),0,0,static_cast<FLOAT>(zoom),static_cast<FLOAT>(client.right*(1-zoom)/2+pan_x),static_cast<FLOAT>(client.bottom*(1-zoom)/2+pan_y)};SetWorldTransform(dc,&matrix);}
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,colors.text);
        const auto font = scaled_font ? scaled_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)); const auto old_font = SelectObject(dc,font);
        const auto pen = CreatePen(PS_SOLID,px(2),reader_theme::accent_ink(window,colors.canvas)), grid = CreatePen(PS_SOLID,1,colors.outline);
        const auto previous_pen = SelectObject(dc,pen), previous_brush = SelectObject(dc,GetStockObject(NULL_BRUSH));
        const auto rect = bounds();
        if (type == "math_number_line" || type == "number_line") {
            const auto low = number(config,L"min",-5), high = number(config,L"max",5), step = number(config,L"step",1);
            const auto y = (rect.top + rect.bottom) / 2; line(dc,{rect.left,y},{rect.right,y},true);
            for (double value = low; value <= high + step * .01 && step > 0 && (high - low) / step <= 100; value += step) {
                const auto x = rect.left + static_cast<LONG>((value - low) / (high - low) * (rect.right - rect.left));
                line(dc,{x,y - px(5)},{x,y + px(5)}); label(dc,{x,y},decimal(value),-8,12);
            }
            const auto value = number(state,L"value",number(config,L"value",0));
            const auto x = rect.left + static_cast<LONG>((value - low) / (high - low) * (rect.right - rect.left));
            const auto closed = flag(state,L"closed",flag(config,L"closed"));
            if (closed) SelectObject(dc,GetStockObject(DC_BRUSH)); SetDCBrushColor(dc,colors.accent);
            Ellipse(dc,x - px(6),y - px(6),x + px(6),y + px(6)); SelectObject(dc,GetStockObject(NULL_BRUSH));
            const auto direction = text(state,L"direction",text(config,L"direction","none"));
            if (direction == "left") line(dc,{x - px(8),y},{rect.left,y},true);
            if (direction == "right") line(dc,{x + px(8),y},{rect.right,y},true);
            for (const auto& item : array(config,L"points")) {
                const auto point = item.GetObject(); const auto at = number(point,L"value");
                const auto xx = rect.left + static_cast<LONG>((at - low) / (high - low) * (rect.right - rect.left));
                Ellipse(dc,xx - px(4),y - px(4),xx + px(4),y + px(4)); label(dc,{xx,y},text(point,L"label"),-6,-28);
            }
        } else if (type == "euclidean_diagram") {
            // Shared isotropic scale keeps circles, angles and labels geometrically faithful.
            const auto aspect = static_cast<double>(rect.right - rect.left) / std::max<LONG>(1,rect.bottom - rect.top);
            const auto cx = (xmin + xmax) / 2, span = ymax - ymin; xmin = cx - span * aspect / 2; xmax = cx + span * aspect / 2;
            for (const auto& value : array(config,L"circles")) {
                const auto circle = value.GetObject(); const auto center = find(text(circle,L"center")); if (!center) continue;
                const auto r = number(circle,L"radius"); const auto a = project(center->x - r,center->y + r), b = project(center->x + r,center->y - r);
                Ellipse(dc,a.x,a.y,b.x,b.y); if (!text(circle,L"label").empty()) label(dc,project(center->x + r,center->y),text(circle,L"label"));
            }
            for (const auto& value : array(config,L"objects")) {
                const auto item = value.GetObject(); const auto through = array(item,L"through");
                PlotPoint a,b;
                if (through.Size() >= 2) {
                    const auto first = find(winrt::to_string(through.GetStringAt(0))), second = find(winrt::to_string(through.GetStringAt(1)));
                    if (!first || !second) continue; a = *first; b = *second;
                } else {
                    const auto origin = object(item,L"origin"), direction = object(item,L"direction");
                    if (const auto point = find(text(item,L"origin"))) a = *point;
                    else { a.x = number(origin,L"x"); a.y = number(origin,L"y"); }
                    b.x = a.x + number(direction,L"x",1); b.y = a.y + number(direction,L"y");
                }
                const auto kind = text(item,L"kind","segment"); const double dx = b.x - a.x,dy = b.y - a.y;
                double begin = 0,end = 1;
                if (kind != "segment") {
                    begin = kind == "line" ? -1e9 : 0; end = 1e9;
                    const auto clip = [&](double origin,double delta,double low,double high) {
                        if (std::abs(delta) < 1e-10) return;
                        const auto one = (low - origin) / delta,two = (high - origin) / delta;
                        begin = std::max(begin,std::min(one,two)); end = std::min(end,std::max(one,two));
                    };
                    clip(a.x,dx,xmin,xmax); clip(a.y,dy,ymin,ymax);
                }
                const auto first = project(a.x + begin * dx,a.y + begin * dy), last = project(a.x + end * dx,a.y + end * dy);
                line(dc,first,last,kind != "segment"); if (kind == "line") line(dc,last,first,true);
                if (!text(item,L"label").empty()) label(dc,last,text(item,L"label"),-24,-22);
            }
            for (const auto& value : array(config,L"arcs")) {
                const auto arc = value.GetObject(); const auto through = array(arc,L"through"); if (through.Size() < 3) continue;
                const auto first = find(winrt::to_string(through.GetStringAt(0))), middle = find(winrt::to_string(through.GetStringAt(1))), last = find(winrt::to_string(through.GetStringAt(2)));
                if (!first || !middle || !last) continue;
                const auto circles = array(config,L"circles");
                for (const auto& circle_value : circles) {
                    const auto circle = circle_value.GetObject(); if (text(circle,L"id") != text(arc,L"circle")) continue;
                    const auto center = find(text(circle,L"center")); if (!center) continue;
                    const auto a = std::atan2(first->y - center->y,first->x - center->x), b = std::atan2(last->y - center->y,last->x - center->x);
                    const auto m = std::atan2(middle->y - center->y,middle->x - center->x); constexpr double tau = 6.283185307179586;
                    const auto normalize = [](double x) { constexpr double full = 6.283185307179586; while (x < 0) x += full; while (x >= full) x -= full; return x; };
                    auto sweep = normalize(b - a); if (normalize(m - a) > sweep) sweep -= tau;
                    const auto radius = number(circle,L"radius"); auto from = project(first->x,first->y);
                    const auto arc_pen = CreatePen(PS_SOLID,px(4),colors.text); SelectObject(dc,arc_pen);
                    for (int step = 1; step <= 96; ++step) { const auto t = a + sweep * step / 96; const auto to = project(center->x + radius * std::cos(t),center->y + radius * std::sin(t)); line(dc,from,to); from = to; }
                    SelectObject(dc,pen); DeleteObject(arc_pen);
                }
            }
        } else if (type == "venn_diagram") {
            const auto w = rect.right - rect.left,h = rect.bottom - rect.top,r = std::min(w / 4,h / 2);
            const int cy = (rect.top + rect.bottom) / 2,cx = (rect.left + rect.right) / 2;
            Rectangle(dc,rect.left,rect.top,rect.right,rect.bottom);
            Ellipse(dc,cx - r * 3 / 2,cy - r,cx + r / 2,cy + r); Ellipse(dc,cx - r / 2,cy - r,cx + r * 3 / 2,cy + r);
            label(dc,{cx - r,cy - r},text(config,L"leftLabel","A"),0,-20); label(dc,{cx + r,cy - r},text(config,L"rightLabel","B"),0,-20);
            label(dc,{cx - r,cy},text(config,L"leftOnly"),0,0); label(dc,{cx,cy},text(config,L"intersection"),0,0);
            label(dc,{cx + r,cy},text(config,L"rightOnly"),0,0); label(dc,{rect.left,rect.bottom},text(config,L"outside"),0,-18);
        } else if (type == "geometry_2d_diagram" || type == "geometry_3d_diagram" || type == "sliders_visual") {
            const auto shape = text(config,L"shape",text(config,L"type","rectangle"));
            auto box = rect; InflateRect(&box,-(rect.right - rect.left) / 5,-px(8));
            if (type == "sliders_visual") {
                const auto values = object(state,L"values"); const auto w = number(values,winrt::to_hstring(text(config,L"widthParameter","width")).c_str(),2);
                const auto h = number(values,winrt::to_hstring(text(config,L"heightParameter","height")).c_str(),2);
                const auto scale = std::min((rect.right - rect.left) / std::max(1.0,w),(rect.bottom - rect.top) / std::max(1.0,h)) * .8;
                box.right = box.left + static_cast<LONG>(w * scale); box.bottom = box.top + static_cast<LONG>(h * scale);
            }
            if (shape == "circle" || shape == "sphere") Ellipse(dc,box.left,box.top,box.right,box.bottom);
            else if (shape == "triangle") { const POINT vertices[]{{box.left,box.bottom},{box.right,box.bottom},{(box.left + box.right) / 2,box.top}}; Polygon(dc,vertices,3); }
            else { Rectangle(dc,box.left,box.top,box.right,box.bottom);
                if (type == "geometry_3d_diagram") { const auto d = px(24); Rectangle(dc,box.left + d,box.top - d,box.right + d,box.bottom - d);
                    line(dc,{box.left,box.top},{box.left + d,box.top - d}); line(dc,{box.right,box.top},{box.right + d,box.top - d});
                    line(dc,{box.left,box.bottom},{box.left + d,box.bottom - d}); line(dc,{box.right,box.bottom},{box.right + d,box.bottom - d}); }
            }
            const auto labels = array(config,L"labels");
            for (std::uint32_t index = 0; index < labels.Size(); ++index) if (labels.GetAt(index).ValueType() == JsonValueType::String) {
                auto label_rect = client; UINT alignment = DT_NOPREFIX | DT_WORDBREAK;
                if (index == 0) { label_rect.top = box.bottom + px(8); label_rect.bottom = client.bottom; alignment |= DT_CENTER; }
                else if (index == 1) { label_rect.right = std::max<LONG>(px(50),box.left - px(4)); label_rect.top = (box.top + box.bottom) / 2; alignment |= DT_LEFT; }
                else { label_rect.left = box.right; label_rect.top = std::max<LONG>(0,box.top - px(22)); alignment |= DT_RIGHT; }
                const auto caption = wide(winrt::to_string(labels.GetStringAt(index))); DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&label_rect,alignment);
            }
        } else {
            SelectObject(dc,grid);
            if (xmax - xmin <= 60 && ymax - ymin <= 60) {
                for (auto x = std::ceil(xmin); x <= xmax; ++x) line(dc,project(x,ymin),project(x,ymax));
                for (auto y = std::ceil(ymin); y <= ymax; ++y) line(dc,project(xmin,y),project(xmax,y));
            }
            SelectObject(dc,pen); if (xmin <= 0 && xmax >= 0) line(dc,project(0,ymin),project(0,ymax),true);
            if (ymin <= 0 && ymax >= 0) line(dc,project(xmin,0),project(xmax,0),true);
            for (auto x = std::ceil(xmin); x <= xmax && xmax - xmin <= 30; ++x) label(dc,project(x,ymin),decimal(x),-4,8);
            for (auto y = std::ceil(ymin); y <= ymax && ymax - ymin <= 30; ++y) label(dc,project(xmin,y),decimal(y),-28,-8);
            const auto mode = text(config,L"mode","point"); const auto saved = SaveDC(dc); IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
            if (mode == "line" && points.size() >= 2) {
                const auto& first = points[0]; const auto& second = points[1];
                if (std::abs(second.x - first.x) < 1e-10) line(dc,project(first.x,ymin),project(first.x,ymax));
                else { const auto slope = (second.y - first.y) / (second.x - first.x);
                    line(dc,project(xmin,first.y + slope * (xmin - first.x)),project(xmax,first.y + slope * (xmax - first.x))); }
            } else if (mode == "polygon") {
                for (std::size_t index = 1; index < points.size(); ++index) line(dc,project(points[index - 1].x,points[index - 1].y),project(points[index].x,points[index].y));
                if (points.size() > 2) line(dc,project(points.back().x,points.back().y),project(points.front().x,points.front().y));
            } else if (mode == "parabola" && points.size() >= 2 && std::abs(points[1].x - points[0].x) > 1e-10) {
                const auto dx = points[1].x - points[0].x, a = (points[1].y - points[0].y) / (dx * dx);
                const auto value_at = [&](double x) { return a * (x - points[0].x) * (x - points[0].x) + points[0].y; };
                auto from = project(xmin,value_at(xmin));
                for (int step = 1; step <= 200; ++step) { const auto x = xmin + (xmax - xmin) * step / 200;
                    const auto to = project(x,value_at(x)); line(dc,from,to); from = to; }
            }
            RestoreDC(dc,saved);
        }
        if (type == "euclidean_diagram" || type == "math_coordinate_graph" || type == "coordinate_graph" || type == "graph") {
            const auto fill = CreateSolidBrush(colors.accent); SelectObject(dc,fill);
            for (const auto& point : points) {
                const auto at = project(point.x,point.y); Ellipse(dc,at.x - px(4),at.y - px(4),at.x + px(4),at.y + px(4));
                for (std::size_t index = 0; index < point.labels.size(); ++index) label(dc,at,point.labels[index],8,-22 - static_cast<int>(index) * 20);
                if (type != "euclidean_diagram" && flag(config,L"showLabels",true)) label(dc,at,"(" + decimal(point.x) + ", " + decimal(point.y) + ")");
            }
            SelectObject(dc,previous_brush); DeleteObject(fill);
        } else SelectObject(dc,previous_brush);
        SelectObject(dc,previous_pen); SelectObject(dc,old_font); DeleteObject(pen); DeleteObject(grid);RestoreDC(dc,transform);
    }
    void pointer(POINT at) {
        if (!changed) return;
        const auto rect = bounds();
        if (type == "number_line") {
            const auto low = number(config,L"min",-5),high = number(config,L"max",5);
            const auto value = snapped(low + static_cast<double>(at.x - rect.left) / std::max<LONG>(1,rect.right - rect.left) * (high - low),number(config,L"step",1),low,high);
            state.SetNamedValue(L"value",JsonValue::CreateNumberValue(value)); changed(state); InvalidateRect(window,nullptr,FALSE); return;
        }
        if (!dragging || *dragging >= points.size()) return;
        auto& point = points[*dragging]; const auto snap = number(config,L"snap",1);
        point.x = snapped(xmin + static_cast<double>(at.x - rect.left) / std::max<LONG>(1,rect.right - rect.left) * (xmax - xmin),snap,xmin,xmax);
        point.y = snapped(ymin + static_cast<double>(rect.bottom - at.y) / std::max<LONG>(1,rect.bottom - rect.top) * (ymax - ymin),snap,ymin,ymax);
        JsonArray values; for (const auto& current : points) { JsonObject item; item.SetNamedValue(L"x",JsonValue::CreateNumberValue(current.x)); item.SetNamedValue(L"y",JsonValue::CreateNumberValue(current.y)); values.Append(item); }
        state.SetNamedValue(L"points",values); changed(state); if (sync_fields) sync_fields(); InvalidateRect(window,nullptr,FALSE);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
        auto* self = reinterpret_cast<Canvas*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Canvas*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); self->window = window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(window,message,wparam,lparam);
        if(message==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(wparam));return 0;}
        if(message==WM_LBUTTONDBLCLK){
            // Windows sends a complete first click before identifying a double
            // click. Keep zoom/expand input-neutral without delaying ordinary
            // number-line clicks or losing their existing immediate response.
            if(self->number_line_click&&self->changed){self->state=*self->number_line_click;self->changed(self->state);self->number_line_click.reset();}
            self->dragging.reset();self->panning=false;if(GetCapture()==window)ReleaseCapture();if(!self->fullscreen)self->expand();else self->toggle_zoom({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)});return 0;}
        if(message==WM_KEYDOWN&&wparam==VK_ESCAPE&&self->fullscreen){self->close_expanded();return 0;}
        if(message==WM_SIZE){self->clamp_pan();return 0;}
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; const auto dc = BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
            const auto buffer=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,bounds.right),std::max(1L,bounds.bottom));const auto previous=SelectObject(buffer,bitmap);
            self->paint(buffer);BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,previous);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);return 0; }
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_LBUTTONDOWN && self->changed && IsWindowEnabled(window)) {
            POINT at=self->content_point({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)});
            for (std::size_t index = 0; index < self->points.size(); ++index) {
                const auto point = self->project(self->points[index].x,self->points[index].y);
                if (self->points[index].draggable && std::hypot(static_cast<double>(point.x - at.x),static_cast<double>(point.y - at.y)) < self->px(18)) { self->dragging = index; SetCapture(window); break; }
            }
            if (self->type == "number_line") {self->number_line_click=JsonObject::Parse(self->state.Stringify());self->number_line_click_start={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};SetCapture(window); self->pointer(at); }
            else if(!self->dragging&&self->zoom>1){self->panning=true;self->pan_last={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};SetCapture(window);}return 0;
        }
        if(message==WM_LBUTTONDOWN&&self->zoom>1){self->panning=true;self->pan_last={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};SetCapture(window);return 0;}
        if (message == WM_MOUSEMOVE && GetCapture() == window && (wparam & MK_LBUTTON)) {
            if(self->number_line_click&&(std::abs(GET_X_LPARAM(lparam)-self->number_line_click_start.x)>GetSystemMetrics(SM_CXDRAG)/2||std::abs(GET_Y_LPARAM(lparam)-self->number_line_click_start.y)>GetSystemMetrics(SM_CYDRAG)/2))self->number_line_click.reset();
            if(self->panning){self->pan_x+=GET_X_LPARAM(lparam)-self->pan_last.x;self->pan_y+=GET_Y_LPARAM(lparam)-self->pan_last.y;self->pan_last={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};self->clamp_pan();InvalidateRect(window,nullptr,FALSE);}
            else self->pointer(self->content_point({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}));return 0; }
        if (message == WM_LBUTTONUP && GetCapture() == window) { if(!self->panning)self->pointer(self->content_point({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}));self->panning=false;self->dragging.reset(); ReleaseCapture(); return 0; }
        if(message==WM_CAPTURECHANGED){self->panning=false;self->dragging.reset();return 0;}
        if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(window),message,wparam,lparam);
        return DefWindowProcW(window,message,wparam,lparam);
    }
};
}

struct MathPuzzleView::Impl {
    struct Cell { HWND window; int width = 0; bool text = false; };
    struct Row { std::vector<Cell> cells; int height = 42; int gap_after=8; };
    HWND host = nullptr,parent = nullptr;
    std::string locale; JsonObject block,answers,initial;
    MathPuzzleCallbacks callbacks;
    HMODULE rich = nullptr; HFONT font = nullptr,base_font = nullptr;
    Microsoft::WRL::ComPtr<IAccPropServices> accessibility;
    std::vector<Row> rows; std::vector<MathReadingRegion> regions; std::vector<std::unique_ptr<Canvas>> canvases;
    std::map<int,std::function<void(int)>> commands;
    std::map<HWND,std::pair<std::string,std::string>> editors;
    struct ExpressionBinding {HWND window=nullptr;std::string id,field;};
    std::vector<std::unique_ptr<ExpressionBinding>> expression_editors;
    std::map<HWND,std::string> keyboard_contexts;
    std::map<HWND,HWND> calculator_buttons;
    std::map<std::string,std::vector<std::string>> orders;
    std::map<std::string,std::vector<std::size_t>> order_rows;
    std::map<HWND,std::pair<std::string,std::string>> order_cards;
    struct MatchCard { std::string response,id; bool source = false; };
    std::map<HWND,MatchCard> match_cards;
    std::map<std::string,std::string> selected_sources;
    std::map<std::string,std::vector<HWND>> choice_buttons;
    std::map<HWND,std::string> choice_options;
    std::map<std::string,std::vector<Canvas*>> live_visuals;
    std::map<Canvas*,std::vector<std::pair<HWND,HWND>>> graph_fields;
    std::map<HWND,int> requested_heights;
    bool initializing = true,cards_enabled = true; int next_id = 100,current_width = 0,text_scale = 100;
    struct Drag { HWND window = nullptr; POINT start{}; ULONGLONG since = 0; bool moved = false; } drag;
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(host)),96); }
    std::string tr(std::string es,std::string en,std::string fr) const { return locale.starts_with("fr") ? fr : locale.starts_with("en") ? en : es; }
    Impl(HWND owner,std::string raw,std::string lang,std::string saved,MathPuzzleCallbacks events)
        : parent(owner),locale(std::move(lang)),callbacks(std::move(events)) {
        windows::ensure_winrt_apartment(); block = JsonObject::Parse(winrt::to_hstring(raw));
        if (!saved.empty()) { JsonObject restored; if (JsonObject::TryParse(winrt::to_hstring(saved),restored)) answers = restored.HasKey(L"answers") ? object(restored,L"answers") : restored; }
        rich = LoadLibraryW(L"Msftedit.dll"); if (!rich) throw std::runtime_error("RichEdit is unavailable");
        (void)CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(accessibility.GetAddressOf()));
    }
    ~Impl() { if (host) DestroyWindow(host); if (font) DeleteObject(font); if (base_font) DeleteObject(base_font); if (rich) FreeLibrary(rich); }
    HWND make(const wchar_t* name,std::string_view value,DWORD style = 0) {
        const auto item = CreateWindowExW(0,name,wide(value).c_str(),WS_CHILD | WS_VISIBLE | style,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(next_id++)),GetModuleHandleW(nullptr),nullptr);
        if (!item) throw std::runtime_error("Cannot create a mathematics control");
        SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(_wcsicmp(name,MSFTEDIT_CLASS)==0?base_font:font),FALSE); reader_theme::apply(item,reader_theme::Surface::card);
        if (_wcsicmp(name,MSFTEDIT_CLASS)==0) SendMessageW(item,EM_SETZOOM,text_scale,100);
        return item;
    }
    HWND label(std::string value,std::string key = {},RichTextRole role = RichTextRole::body,bool passive = true) {
        const auto item = make(MSFTEDIT_CLASS,"",ES_MULTILINE | ES_READONLY);
        set_rich_text(item,value,{},false,role); regions.push_back({key.empty() ? "text/" + std::to_string(regions.size()) : std::move(key),std::move(value),item});
        const auto& original = regions.back().source_text;
        if (original.find("\\(") != original.npos || original.find("\\[") != original.npos) set_native_math_inline_text(item,original,{},role);
        SendMessageW(item,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE);
        SetWindowSubclass(item,text_wheel_proc,14,passive?reinterpret_cast<DWORD_PTR>(&callbacks):0);
        return item;
    }
    void add_text(std::string value,std::string key = {},RichTextRole role = RichTextRole::body) { if (!value.empty()) rows.push_back({{{label(std::move(value),std::move(key),role),0,true}},26}); }
    HWND button(std::string value,std::function<void()> action,DWORD style = BS_PUSHBUTTON) {
        const auto item = make(L"BUTTON",value,WS_TABSTOP | style); commands[GetDlgCtrlID(item)] = [action = std::move(action)](int notification) { if (notification == BN_CLICKED) action(); }; return item;
    }
    JsonObject response(const std::string& id,std::string_view type) {
        const auto value = answers.TryLookup(winrt::to_hstring(id));
        auto value_object = value && value.ValueType() == JsonValueType::Object && text(value.GetObject(),L"type") == type ? value.GetObject() : answer(type);
        if (type == "text" && !value_object.HasKey(L"value")) value_object.SetNamedValue(L"value",string_value(""));
        if (type == "choice" && !value_object.HasKey(L"selected")) value_object.SetNamedValue(L"selected",JsonArray{});
        if ((type == "fields" || type == "sliders") && !value_object.HasKey(L"values")) value_object.SetNamedValue(L"values",JsonObject{});
        if (type == "matching" && !value_object.HasKey(L"pairs")) value_object.SetNamedValue(L"pairs",JsonObject{});
        answers.SetNamedValue(winrt::to_hstring(id),value_object); return value_object;
    }
    void change() {
        if (initializing) return;
        for (const auto& [id,plots] : live_visuals) for (const auto plot : plots) {
            plot->state = object(answers,winrt::to_hstring(id).c_str()); InvalidateRect(plot->window,nullptr,FALSE);
        }
        if (callbacks.changed) callbacks.changed();
    }
    void submit() { if (!initializing && callbacks.submit) callbacks.submit(stringify(answers)); }
    void keyboard_context(HWND item,const JsonObject& config,const JsonObject& validator,bool structured,std::string_view mode){
        auto context=JsonObject::Parse(config.Stringify());context.SetNamedValue(L"validator",validator);context.SetNamedValue(L"structured",JsonValue::CreateBooleanValue(structured));
        if(!context.HasKey(L"keyboard"))context.SetNamedValue(L"keyboard",string_value(mode));
        keyboard_contexts[item]=stringify(context);SetPropW(item,keyboard_context_property,&keyboard_contexts.at(item));
    }
    void layout_calculator(HWND field){
        const auto found=calculator_buttons.find(field);if(found==calculator_buttons.end())return;RECT area{};GetClientRect(field,&area);
        const int side=px(32);MoveWindow(found->second,std::max(0L,area.right-side),std::max(0L,(area.bottom-side)/2),side,std::min<LONG>(side,area.bottom),TRUE);
        SendMessageW(field,EM_SETMARGINS,EC_RIGHTMARGIN,MAKELPARAM(0,side+px(8)));
    }
    void calculator(HWND field,const JsonObject& config){
        if(!flag(config,L"calculator"))return;
        const auto item=CreateWindowExW(0,L"BUTTON",wide(tr("Abrir calculadora científica","Open Scientific Calculator","Ouvrir la calculatrice scientifique")).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,1,1,field,reinterpret_cast<HMENU>(static_cast<INT_PTR>(next_id++)),GetModuleHandleW(nullptr),nullptr);
        if(!item)throw std::runtime_error("Cannot create the permitted mathematical calculator action");
        reader_theme::apply(item,reader_theme::Surface::field);reader_theme::icon_button(item);calculator_buttons[field]=item;layout_calculator(field);
    }
    HWND input(const std::string& id,const std::string& field,JsonObject current,std::string hint = {},const JsonObject& config={},const JsonObject& validator={},std::string_view mode="NUMERIC") {
        const auto item = make(MSFTEDIT_CLASS,"",WS_TABSTOP | ES_AUTOHSCROLL);
        reader_theme::apply(item,reader_theme::Surface::field);
        const auto values = field.empty() ? current : object(current,L"values");
        set_edit_text(item,text(values,field.empty() ? L"value" : winrt::to_hstring(field).c_str()));
        if (!hint.empty()) SendMessageW(item,EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(wide(hint).c_str()));
        SendMessageW(item,EM_SETEVENTMASK,0,ENM_CHANGE);
        editors[item] = {id,field};keyboard_context(item,config,validator,false,mode);SetWindowSubclass(item,field_proc,11,reinterpret_cast<DWORD_PTR>(this));calculator(item,config);return item;
    }
    void edited(HWND item) {
        if (initializing) return; const auto found = editors.find(item); if (found == editors.end()) return;
        const auto& [id,field] = found->second; auto current = object(answers,winrt::to_hstring(id).c_str());
        if (field.empty()) current.SetNamedValue(L"value",string_value(edit_text(item)));
        else { auto values = object(current,L"values"); values.SetNamedValue(winrt::to_hstring(field),string_value(edit_text(item))); current.SetNamedValue(L"values",values); }
        answers.SetNamedValue(winrt::to_hstring(id),current); change();
    }
    void capture_expression(const ExpressionBinding& binding){
        const auto snapshot=inspect_math_expression_editor(binding.window);auto current=response(binding.id,binding.field.empty()?"text":"fields");
        if(binding.field.empty()){current.SetNamedValue(L"value",string_value(snapshot.latex));current.SetNamedValue(L"editorDraft",string_value(snapshot.draft_json));}
        else {auto values=object(current,L"values"),drafts=object(current,L"editorDrafts");values.SetNamedValue(winrt::to_hstring(binding.field),string_value(snapshot.latex));drafts.SetNamedValue(winrt::to_hstring(binding.field),string_value(snapshot.draft_json));current.SetNamedValue(L"values",values);current.SetNamedValue(L"editorDrafts",drafts);}
        answers.SetNamedValue(winrt::to_hstring(binding.id),current);
    }
    void capture_expressions(){for(const auto& editor:expression_editors)capture_expression(*editor);}
    HWND expression_input(const std::string& id,const std::string& field,JsonObject current,const JsonObject& config,const JsonObject& validator){
        if(!callbacks.expression_request)throw std::runtime_error("The structured mathematics editor requires its canonical model service");
        auto binding=std::make_unique<ExpressionBinding>();binding->id=id;binding->field=field;auto* identity=binding.get();
        MathExpressionEditorAccess access;access.request=callbacks.expression_request;access.expected_template=text(validator,L"expected");access.language=locale;access.accent_border=true;
        access.changed=[this,identity]{if(initializing||!identity->window)return;capture_expression(*identity);change();if(callbacks.layout_changed)callbacks.layout_changed();};
        access.submit=[this]{submit();};access.focus_changed=[this,identity](bool focused){if(!initializing&&callbacks.expression_focus)callbacks.expression_focus(identity->window,focused);};
        access.cancel=[this]{if(callbacks.back_requested)callbacks.back_requested();};
        const auto item=create_math_expression_editor(host,next_id++,std::move(access));identity->window=item;
        SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        const auto drafts=field.empty()?text(current,L"editorDraft"):text(object(current,L"editorDrafts"),winrt::to_hstring(field).c_str());
        const auto value=field.empty()?text(current,L"value"):text(object(current,L"values"),winrt::to_hstring(field).c_str());
        if((drafts.empty()||!restore_math_expression_draft(item,drafts))&&!set_math_expression_latex(item,value)){
            DestroyWindow(item);throw std::runtime_error("Cannot restore the existing structured mathematics response");}
        keyboard_context(item,config,validator,true,"ALGEBRA");expression_editors.push_back(std::move(binding));return item;
    }
    void canvas(const JsonObject& config,std::string type,JsonObject state = {},std::function<void(JsonObject)> changed = {},std::string live_id = {}) {
        auto plot = std::make_unique<Canvas>(); plot->config = config; plot->type = std::move(type); plot->state = state; plot->changed = std::move(changed);plot->locale=locale;
        plot->window = CreateWindowExW(0,canvas_class,L"",WS_CHILD | WS_VISIBLE | (plot->changed ? WS_TABSTOP : 0),0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(next_id++)),GetModuleHandleW(nullptr),plot.get());
        if (!plot->window) throw std::runtime_error("Cannot create a mathematics canvas");
        plot->initialize(); plot->scale_text(text_scale);
        const auto spacer=make(L"STATIC","");plot->expand_button=button(tr("Ampliar representación","Expand representation","Agrandir la représentation"),[visual=plot.get()]{visual->expand();});reader_theme::icon_button(plot->expand_button);
        rows.push_back({{{spacer},{plot->expand_button,48}},48,0});
        rows.push_back({{{plot->window}},1});
        if (!live_id.empty()) live_visuals[live_id].push_back(plot.get()); canvases.push_back(std::move(plot));
    }
    void representation(const JsonObject& spec) {
        const auto config = spec.HasKey(L"config") ? object(spec,L"config") : spec;
        const auto type = text(spec,L"type","text");
        if (type == "text" || type == "math_formula") {
            const auto source = text(config,L"latex",text(config,L"text",text(config,L"formula",text(config,L"value"))));
            if (!source.empty()) { const auto item = label(source); if (type == "math_formula") set_native_math_text(item,source);
                rows.push_back({{{item,0,true}},type == "math_formula" ? 56 : 26}); }
        }
        else if (type == "math_table") {
            const auto add = [&](const JsonArray& values) { Row row; for (const auto& value : values) {
                const auto visible = value.ValueType() == JsonValueType::String ? winrt::to_string(value.GetString()) : stringify(value);
                row.cells.push_back({label(visible),0,true}); } if (!row.cells.empty()) rows.push_back(std::move(row)); };
            const auto headers = array(config,L"headers"); if (headers.Size()) add(headers);
            for (const auto& value : array(config,L"rows")) if (value.ValueType() == JsonValueType::Array) add(value.GetArray());
        } else if (type == "rich_answer_option") { for (const auto& value : array(config,L"content")) representation(value.GetObject()); }
        else { canvas(config,type); add_text(text(config,L"description")); }
    }
    void option_content(const JsonObject& option) {
        if (!visible_label(option).empty()) add_text(visible_label(option));
        else for (const auto& value : array(option,L"content")) representation(value.GetObject());
    }
    void choose(const std::string& id,const std::string& selected,bool multiple) {
        auto current = response(id,"choice"); auto values = array(current,L"selected"); std::vector<std::string> ids;
        for (const auto& value : values) ids.push_back(winrt::to_string(value.GetString()));
        if (!multiple) ids = {selected};
        else if (const auto found = std::ranges::find(ids,selected); found == ids.end()) ids.push_back(selected); else ids.erase(found);
        JsonArray chosen; for (const auto& value : ids) chosen.Append(string_value(value)); current.SetNamedValue(L"selected",chosen); answers.SetNamedValue(winrt::to_hstring(id),current);
        for (const auto item : choice_buttons[id])
            SendMessageW(item,BM_SETCHECK,std::ranges::find(ids,choice_options.at(item)) != ids.end() ? BST_CHECKED : BST_UNCHECKED,0);
        change();
    }
    HWND dropdown(const std::string& id,const std::string& field,const JsonArray& choices,JsonObject current,bool matching = false) {
        const auto values = object(current,matching ? L"pairs" : L"values"); const auto selection = text(values,winrt::to_hstring(field).c_str());
        std::string caption = tr("Seleccionar…","Select…","Sélectionner…");
        for (const auto& value : choices) if (text(value.GetObject(),L"id") == selection) caption = visible_label(value.GetObject());
        const auto item = make(L"BUTTON",caption,WS_TABSTOP | BS_MULTILINE | BS_PUSHBUTTON);
        commands[GetDlgCtrlID(item)] = [this,id,field,choices,item,matching](int notification) {
            if (notification != BN_CLICKED) return;
            struct Popup { HMENU handle = CreatePopupMenu(); ~Popup() { if (handle) DestroyMenu(handle); } } menu;
            if (!menu.handle) throw std::runtime_error("Cannot create the native mathematical choice menu");
            for (std::uint32_t index = 0; index < choices.Size(); ++index) {
                auto label = wide(visible_label(choices.GetObjectAt(index)));
                for (std::size_t position = 0; (position = label.find(L'&',position)) != label.npos; position += 2) label.insert(position,1,L'&');
                AppendMenuW(menu.handle,MF_STRING,index + 1,label.c_str());
            }
            const auto style = prepare_native_menu(menu.handle,host);
            RECT rect{}; GetWindowRect(item,&rect);
            const auto selected = TrackPopupMenuEx(menu.handle,TPM_RETURNCMD | TPM_LEFTALIGN,rect.left,rect.bottom,host,nullptr);
            if (selected <= 0 || static_cast<std::uint32_t>(selected) > choices.Size()) return;
            const auto option = choices.GetObjectAt(selected - 1); auto value = object(answers,winrt::to_hstring(id).c_str());
            auto pairs = object(value,matching ? L"pairs" : L"values"); pairs.SetNamedValue(winrt::to_hstring(field),string_value(text(option,L"id")));
            value.SetNamedValue(matching ? L"pairs" : L"values",pairs); answers.SetNamedValue(winrt::to_hstring(id),value);
            SetWindowTextW(item,wide(visible_label(option)).c_str()); change();
        }; return item;
    }
    void reorder(const std::string& id,std::size_t from,std::size_t to) {
        if (!cards_enabled) return;
        auto& ids = orders[id]; if (from >= ids.size() || to >= ids.size() || from == to) return;
        auto& indices = order_rows[id]; const auto first = indices.front();
        auto moved = std::move(rows[first + from]); rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(first + from));
        rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(first + to),std::move(moved));
        const auto selected = ids[from]; ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(from)); ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(to),selected);
        JsonArray values; for (const auto& current : ids) values.Append(string_value(current)); auto current = response(id,"order");
        current.SetNamedValue(L"ids",values); answers.SetNamedValue(winrt::to_hstring(id),current); arrange(current_width); change();
        if (callbacks.layout_changed) callbacks.layout_changed();
    }
    void match(const std::string& id,const std::string& source,const std::string& target) {
        if (!cards_enabled) return;
        auto current = response(id,"matching"); auto pairs = object(current,L"pairs");
        pairs.SetNamedValue(winrt::to_hstring(source),string_value(target)); current.SetNamedValue(L"pairs",pairs);
        answers.SetNamedValue(winrt::to_hstring(id),current); selected_sources.erase(id); InvalidateRect(host,nullptr,FALSE); change();
    }
    void paint_matches(HDC dc) const {
        const auto pen = CreatePen(PS_SOLID,px(2),reader_theme::accent_ink(host,reader_theme::palette(host).card)); const auto previous = SelectObject(dc,pen);
        for (const auto& [source_window,source] : match_cards) if (source.source) {
            const auto state = object(answers,winrt::to_hstring(source.response).c_str()); const auto target = text(object(state,L"pairs"),winrt::to_hstring(source.id).c_str());
            for (const auto& [target_window,card] : match_cards) if (!card.source && card.response == source.response && card.id == target) {
                RECT a{},b{}; GetWindowRect(source_window,&a); GetWindowRect(target_window,&b);
                POINT from{a.right,(a.top + a.bottom) / 2},to{b.left,(b.top + b.bottom) / 2}; ScreenToClient(host,&from); ScreenToClient(host,&to);
                MoveToEx(dc,from.x,from.y,nullptr); LineTo(dc,to.x,to.y);
            }
        }
        SelectObject(dc,previous); DeleteObject(pen);
    }
    void response_controls(const JsonObject& spec) {
        const auto id = text(spec,L"id"),type = text(spec,L"type"); add_text(text(spec,L"label"),id + "/label");
        if (type == "math_multiple_choice_single" || type == "math_multiple_choice_multi") {
            const bool multiple = type == "math_multiple_choice_multi"; auto current = response(id,"choice"); const auto selected = array(current,L"selected");
            for (const auto& value : array(spec,L"options")) {
                const auto option = value.GetObject(); const auto option_id = text(option,L"id");
                const auto toggle = button("",[this,id,option_id,multiple] { choose(id,option_id,multiple); },multiple ? BS_AUTOCHECKBOX : BS_AUTORADIOBUTTON);
                choice_options[toggle] = option_id; choice_buttons[id].push_back(toggle);
                if (accessibility) accessibility->SetHwndPropStr(toggle,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,PROPID_ACC_NAME,wide(visible_label(option)).c_str());
                for (const auto& chosen : selected) if (chosen.GetString() == winrt::to_hstring(option_id)) SendMessageW(toggle,BM_SETCHECK,BST_CHECKED,0);
                const auto title = visible_label(option); if (!title.empty()) rows.push_back({{{toggle,28},{label(title,id + "/option/" + option_id),0,true}},38});
                else { rows.push_back({{{toggle,28}},28}); option_content(option); }
            }
        } else if (type == "math_numeric_input" || type == "math_expression_input") {
            if (array(spec,L"fields").Size()) fields(spec,id);
            else { const auto current = response(id,"text");
                if(type=="math_expression_input"&&text(spec,L"inputMode","simple")=="structured")rows.push_back({{{expression_input(id,"",current,spec,object(spec,L"validator"))}},44});
                else rows.push_back({{{input(id,"",current,text(spec,L"hint"),spec,object(spec,L"validator"),type=="math_expression_input"?"ALGEBRA":"NUMERIC")}},42}); }
        } else if (type == "math_multi_input" || type == "math_structured_answer" || type == "math_equation_fill_blanks") fields(spec,id);
        else if (type == "math_matching_drag") {
            (void)response(id,"matching"); const auto sources = array(spec,L"sources"),targets = array(spec,L"targets");
            add_text(tr("Arrastra una tarjeta hacia su pareja o selecciona las dos tarjetas.","Drag a card to its partner or select both cards.","Faites glisser une carte vers sa correspondante ou sélectionnez les deux cartes."),id + "/instruction");
            for (std::uint32_t index = 0; index < std::max(sources.Size(),targets.Size()); ++index) {
                Row row;
                for (const auto source : {true,false}) {
                    const auto& items = source ? sources : targets;
                    if (index >= items.Size()) { row.cells.push_back({label(""),0,true}); continue; }
                    const auto item = items.GetObjectAt(index); const auto key = text(item,L"id");
                    auto visible = visible_label(item); if (visible.empty()) for (const auto& content : array(item,L"content")) visible += text(content.GetObject(),L"text") + ' ';
                    const auto card = label(visible,id + (source ? "/source/" : "/target/") + key,RichTextRole::body,false);
                    match_cards[card] = {id,key,source}; SetWindowSubclass(card,match_proc,13,reinterpret_cast<DWORD_PTR>(this));
                    row.cells.push_back({card,0,true});
                }
                rows.push_back(std::move(row));
            }
        } else if (type == "math_ordering_drag") {
            const auto current = response(id,"order"); auto values = array(current,L"ids"); if (!values.Size()) values = array(spec,L"initialOrder");
            const auto items = array(spec,L"items"); std::map<std::string,JsonObject> cards;
            for (const auto& value : items) cards[text(value.GetObject(),L"id")] = value.GetObject();
            auto& ids = orders[id]; for (const auto& value : values) if (cards.contains(winrt::to_string(value.GetString()))) ids.push_back(winrt::to_string(value.GetString()));
            for (const auto& value : items) { const auto key = text(value.GetObject(),L"id"); if (std::ranges::find(ids,key) == ids.end()) ids.push_back(key); }
            JsonArray initial_ids; for (const auto& key : ids) initial_ids.Append(string_value(key)); current.SetNamedValue(L"ids",initial_ids); answers.SetNamedValue(winrt::to_hstring(id),current);
            add_text(tr("Arrastra las tarjetas o utiliza las flechas para ordenarlas.","Drag the cards or use the arrows to order them.","Faites glisser les cartes ou utilisez les flèches pour les ordonner."),id + "/instruction");
            for (const auto& key : ids) {
                const auto item = cards.at(key); auto visible = visible_label(item);
                if (visible.empty()) for (const auto& content : array(item,L"content")) visible += text(content.GetObject(),L"text") + ' ';
                const auto card = label(visible,id + "/item/" + key,RichTextRole::body,false); order_cards[card] = {id,key}; SetWindowSubclass(card,card_proc,12,reinterpret_cast<DWORD_PTR>(this));
                const auto move = [this,id,key](int delta) { const auto& current_ids = orders[id]; const auto at = static_cast<std::size_t>(std::ranges::find(current_ids,key) - current_ids.begin());
                    if ((delta < 0 && at) || (delta > 0 && at + 1 < current_ids.size())) reorder(id,at,static_cast<std::size_t>(static_cast<std::ptrdiff_t>(at) + delta)); };
                order_rows[id].push_back(rows.size()); rows.push_back({{{card,0,true},{button("↑",[move] { move(-1); }),34},{button("↓",[move] { move(1); }),34}},48});
            }
        } else if (type == "math_number_line") {
            auto current = response(id,"number_line"); if (!current.HasKey(L"value")) {
                current.SetNamedValue(L"value",JsonValue::CreateNumberValue(number(spec,L"value"))); current.SetNamedValue(L"closed",JsonValue::CreateBooleanValue(flag(spec,L"closed")));
                current.SetNamedValue(L"direction",string_value(text(spec,L"direction","none"))); answers.SetNamedValue(winrt::to_hstring(id),current);
            }
            canvas(spec,"number_line",current,[this,id](JsonObject value) { answers.SetNamedValue(winrt::to_hstring(id),value); change(); },id);
            const auto closed = button(tr("Extremo abierto/cerrado","Open/closed endpoint","Extrémité ouverte/fermée"),[this,id] { auto value = response(id,"number_line"); value.SetNamedValue(L"closed",JsonValue::CreateBooleanValue(!flag(value,L"closed"))); answers.SetNamedValue(winrt::to_hstring(id),value); change(); });
            const auto left = button("←",[this,id] { auto value = response(id,"number_line"); value.SetNamedValue(L"direction",string_value("left")); answers.SetNamedValue(winrt::to_hstring(id),value); change(); });
            const auto right = button("→",[this,id] { auto value = response(id,"number_line"); value.SetNamedValue(L"direction",string_value("right")); answers.SetNamedValue(winrt::to_hstring(id),value); change(); });
            rows.push_back({{{closed,0},{left,50},{right,50}},42});
        } else if (type == "math_slider_interaction") {
            auto current = response(id,"sliders"),values = object(current,L"values");
            for (const auto& value : array(spec,L"parameters")) {
                const auto parameter = value.GetObject(); const auto key = text(parameter,L"id");
                if (!values.HasKey(winrt::to_hstring(key))) values.SetNamedValue(winrt::to_hstring(key),JsonValue::CreateNumberValue(number(parameter,L"value",number(parameter,L"min"))));
            }
            current.SetNamedValue(L"values",values); answers.SetNamedValue(winrt::to_hstring(id),current);
            for (const auto& value : array(spec,L"parameters")) {
                const auto parameter = value.GetObject(); const auto key = text(parameter,L"id");
                add_text(visible_label(parameter),id + "/parameter/" + key); const auto slider = make(TRACKBAR_CLASSW,"",WS_TABSTOP | TBS_AUTOTICKS);
                const auto low = number(parameter,L"min"), high = number(parameter,L"max",10), step = number(parameter,L"step",1);
                const auto steps = static_cast<int>(std::clamp(std::round((high - low) / step),1.0,100000.0));
                SendMessageW(slider,TBM_SETRANGEMAX,FALSE,steps); SendMessageW(slider,TBM_SETPOS,TRUE,static_cast<LPARAM>(std::lround((number(values,winrt::to_hstring(key).c_str(),low) - low) / step)));
                const auto value_label = label(decimal(number(values,winrt::to_hstring(key).c_str(),low)),id + "/value/" + key);
                commands[GetDlgCtrlID(slider)] = [this,id,key,slider,value_label,low,step](int) {
                    const auto number_value = low + static_cast<double>(SendMessageW(slider,TBM_GETPOS,0,0)) * step;
                    auto current_answer = response(id,"sliders"),current_values = object(current_answer,L"values"); current_values.SetNamedValue(winrt::to_hstring(key),JsonValue::CreateNumberValue(number_value));
                    current_answer.SetNamedValue(L"values",current_values); answers.SetNamedValue(winrt::to_hstring(id),current_answer);
                    const auto displayed = decimal(number_value); set_rich_text(value_label,displayed); for (auto& region : regions) if (region.target == value_label) region.source_text = displayed; change();
                }; rows.push_back({{{slider,0},{value_label,80,true}},44});
            }
            if (spec.HasKey(L"visual")) canvas(object(spec,L"visual"),"sliders_visual",current,{},id);
        } else if (type == "math_coordinate_graph_builder" || type == "math_geometry_builder") {
            auto current = response(id,"graph"); if (!current.HasKey(L"points")) current.SetNamedValue(L"points",array(spec,L"points"));
            answers.SetNamedValue(winrt::to_hstring(id),current);
            canvas(spec,"graph",current,[this,id](JsonObject value) { answers.SetNamedValue(winrt::to_hstring(id),value); change(); });
            auto* plot = canvases.back().get();
            plot->sync_fields = [this,plot] {
                const auto previous = initializing; initializing = true;
                for (std::size_t index = 0; index < graph_fields[plot].size(); ++index) {
                    set_edit_text(graph_fields[plot][index].first,decimal(plot->points[index].x));
                    set_edit_text(graph_fields[plot][index].second,decimal(plot->points[index].y));
                }
                initializing = previous;
            };
            add_text(tr("Edita las coordenadas o arrastra los puntos.","Edit the coordinates or drag the points.","Modifiez les coordonnées ou faites glisser les points."),id + "/coordinates-instruction");
            for (std::size_t index = 0; index < plot->points.size(); ++index) {
                const auto editable = plot->points[index].draggable;
                const auto x = make(MSFTEDIT_CLASS,decimal(plot->points[index].x),WS_TABSTOP | ES_AUTOHSCROLL | (editable ? 0 : ES_READONLY));
                const auto y = make(MSFTEDIT_CLASS,decimal(plot->points[index].y),WS_TABSTOP | ES_AUTOHSCROLL | (editable ? 0 : ES_READONLY));
                graph_fields[plot].push_back({x,y});
                for (const auto item : {x,y}) { reader_theme::apply(item,reader_theme::Surface::field);keyboard_context(item,{}, {},false,"NUMERIC");SendMessageW(item,EM_SETEVENTMASK,0,ENM_CHANGE); SetWindowSubclass(item,field_proc,11,reinterpret_cast<DWORD_PTR>(this)); }
                const auto sync = [this,id,plot,index,x,y](int notification) {
                    if (notification != EN_CHANGE || initializing) return;
                    try { std::size_t endx = 0,endy = 0; const auto tx = edit_text(x),ty = edit_text(y); const auto xx = std::stod(tx,&endx),yy = std::stod(ty,&endy);
                        if (endx != tx.size() || endy != ty.size() || !std::isfinite(xx) || !std::isfinite(yy)) return;
                        auto& point = plot->points[index]; point.x = snapped(xx,number(plot->config,L"snap",1),plot->xmin,plot->xmax);
                        point.y = snapped(yy,number(plot->config,L"snap",1),plot->ymin,plot->ymax);
                        auto answer_value = response(id,"graph"); JsonArray points;
                        for (const auto& item : plot->points) { JsonObject coordinates; coordinates.SetNamedValue(L"x",JsonValue::CreateNumberValue(item.x)); coordinates.SetNamedValue(L"y",JsonValue::CreateNumberValue(item.y)); points.Append(coordinates); }
                        answer_value.SetNamedValue(L"points",points); answers.SetNamedValue(winrt::to_hstring(id),answer_value); InvalidateRect(plot->window,nullptr,FALSE); change();
                    } catch (const std::exception&) { /* An incomplete coordinate remains editable. */ }
                };
                commands[GetDlgCtrlID(x)] = sync; commands[GetDlgCtrlID(y)] = sync;
                rows.push_back({{{label("P" + std::to_string(index + 1)),48,true},{label("x",id + "/x/" + std::to_string(index)),20,true},{x,0},
                    {label("y",id + "/y/" + std::to_string(index)),20,true},{y,0}},42});
            }
        } else throw std::invalid_argument("Native math control is not yet implemented: " + type);
        if (!text(spec,L"unit").empty()) add_text(tr("Unidad: ","Unit: ","Unité : ") + text(spec,L"unit"),id + "/unit");
        add_text(text(spec,L"instruction"),id + "/instruction-authored");
    }
    void fields(const JsonObject& spec,const std::string& id) {
        auto current = response(id,"fields"),values = object(current,L"values"); current.SetNamedValue(L"values",values); answers.SetNamedValue(winrt::to_hstring(id),current);
        std::map<std::string,JsonObject> definitions; for (const auto& value : array(spec,L"fields")) definitions[text(value.GetObject(),L"id")] = value.GetObject();
        std::map<std::string,HWND> created;
        const auto field = [&](const std::string& key) -> Cell {
            if (!definitions.contains(key)) throw std::invalid_argument("A displayed math field has no definition");
            const auto config = definitions.at(key); const auto kind = text(config,L"type","numeric");
            if (kind == "fixed") return {label(text(config,L"text"),id + "/fixed/" + key),0,true};
            auto keyboard_config=JsonObject::Parse(config.Stringify());for(const auto property:{L"keyboard",L"keyboardSymbols"})if(!keyboard_config.HasKey(property)&&spec.HasKey(property))keyboard_config.SetNamedValue(property,spec.GetNamedValue(property));
            const auto validator=object(object(object(spec,L"validator"),L"fields"),winrt::to_hstring(key).c_str());
            if (!created.contains(key)) created[key] = kind == "dropdown" ? dropdown(id,key,array(config,L"options"),current)
                :kind=="expression"&&text(config,L"inputMode","simple")=="structured"?expression_input(id,key,current,keyboard_config,validator)
                :input(id,key,current,text(config,L"hint"),keyboard_config,validator,kind=="expression"?"ALGEBRA":"NUMERIC");
            return {created.at(key),0,false};
        };
        const auto segments = array(spec,L"segments"); const auto table = array(spec,L"rows");
        if (segments.Size()) {
            Row row; for (const auto& value : segments) { const auto segment = value.GetObject();
                if (segment.HasKey(L"field")) row.cells.push_back(field(text(segment,L"field")));
                else row.cells.push_back({label(text(segment,L"text")),0,true}); }
            if (!row.cells.empty()) rows.push_back(std::move(row));
        } else if (table.Size()) {
            const auto headers = array(spec,L"headers"); if (headers.Size()) { Row row; for (const auto& value : headers) row.cells.push_back({label(winrt::to_string(value.GetString())),0,true}); rows.push_back(std::move(row)); }
            for (const auto& row_value : table) { Row row; for (const auto& value : row_value.GetArray())
                if (value.ValueType() == JsonValueType::Object) row.cells.push_back(field(text(value.GetObject(),L"field")));
                else row.cells.push_back({label(value.ValueType() == JsonValueType::String ? winrt::to_string(value.GetString()) : stringify(value)),0,true});
                if (!row.cells.empty()) rows.push_back(std::move(row)); }
        } else for (const auto& value : array(spec,L"fields")) {
            const auto config = value.GetObject(); add_text(text(config,L"label"),id + "/field/" + text(config,L"id"));
            rows.push_back({{field(text(config,L"id"))},42});
            if (!text(config,L"unit").empty()) add_text(tr("Unidad: ","Unit: ","Unité : ") + text(config,L"unit"));
        }
    }
    void build() {
        // Weight is selected BEFORE native math is built and aligned: TOM
        // may encode bold numerals with supplementary mathematical digits.
        add_text(text(block,L"prompt",text(block,L"question")),"prompt",RichTextRole::prompt);
        for (const auto& value : array(block,L"representations")) representation(value.GetObject());
        for (const auto& value : array(block,L"tools")) representation(value.GetObject());
        for (const auto& value : array(block,L"responses")) response_controls(value.GetObject());
        initial = JsonObject::Parse(answers.Stringify()); initializing = false;
    }
    int arrange(int width,bool move = true) {
        current_width = std::max(px(100),width); const auto gap = px(8); int y = px(8);
        for (const auto& row : rows) {
            int fixed = 0,flex = 0; for (const auto& cell : row.cells) if (cell.width) fixed += px(cell.width); else ++flex;
            const auto flexible = std::max(px(30),(current_width - px(16) - fixed - gap * (static_cast<int>(row.cells.size()) - 1)) / std::max(1,flex));
            int height = px(row.height),x = px(8);
            for(const auto& cell:row.cells)if(std::ranges::any_of(expression_editors,[&](const auto& value){return value->window==cell.window;}))height=std::max(height,math_expression_editor_height(cell.window));
            for(const auto& cell:row.cells)if(const auto canvas=std::ranges::find_if(canvases,[&](const auto& value){return value->window==cell.window;});canvas!=canvases.end())
                height=std::max(height,static_cast<int>(std::lround((cell.width?px(cell.width):flexible)/(*canvas)->aspect_ratio())));
            for (const auto& cell : row.cells) if (cell.text || (GetWindowLongPtrW(cell.window,GWL_STYLE) & BS_MULTILINE) || editors.contains(cell.window)) {
                const auto cell_width = cell.width ? px(cell.width) : flexible;
                if (cell.text) {
                    SetWindowPos(cell.window,nullptr,0,0,cell_width,std::max(1,height),SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOCOPYBITS);
                    SendMessageW(cell.window,EM_REQUESTRESIZE,0,0);
                    if (const auto found = requested_heights.find(cell.window); found != requested_heights.end()) { height = std::max(height,found->second + px(4)); continue; }
                }
                const auto dc = GetDC(cell.window); const auto old = SelectObject(dc,font);
                const auto count = GetWindowTextLengthW(cell.window); std::wstring value(static_cast<std::size_t>(count) + 1,L'\0'); value.resize(GetWindowTextW(cell.window,value.data(),static_cast<int>(value.size())));
                RECT text_rect{0,0,cell_width,0};
                DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&text_rect,DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                height = std::max(height,static_cast<int>(text_rect.bottom) + px(8)); SelectObject(dc,old); ReleaseDC(cell.window,dc);
            }
            if (move) for (const auto& cell : row.cells) { const auto cell_width = cell.width ? px(cell.width) : flexible;
                if(GetParent(cell.window)==host)SetWindowPos(cell.window,nullptr,x,y,cell_width,height,SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS); x += cell_width + gap; }
            y += height + px(row.gap_after);
        }
        if (move) RedrawWindow(host,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return y;
    }
    static LRESULT CALLBACK field_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
        auto* self = reinterpret_cast<Impl*>(value);
        if(message==WM_SIZE){const auto result=DefSubclassProc(item,message,wparam,lparam);self->layout_calculator(item);return result;}
        if(message==WM_DRAWITEM&&lparam){const auto& drawing=*reinterpret_cast<DRAWITEMSTRUCT*>(lparam);if(const auto button=self->calculator_buttons.find(item);button!=self->calculator_buttons.end()&&drawing.hwndItem==button->second){
            (void)reader_theme::draw_button(drawing);auto icon=drawing.rcItem;InflateRect(&icon,-self->px(4),-self->px(4));draw_icon(drawing.hDC,icon,Icon::calculator,IsWindowEnabled(item)?reader_theme::palette(item).accent:reader_theme::palette(item).muted);return TRUE;}}
        if(message==WM_COMMAND&&HIWORD(wparam)==BN_CLICKED){const auto button=self->calculator_buttons.find(item);if(button!=self->calculator_buttons.end()&&reinterpret_cast<HWND>(lparam)==button->second){if(!self->initializing&&IsWindowEnabled(item)&&self->callbacks.calculator_requested)self->callbacks.calculator_requested(item);return 0;}}
        if((message==WM_SETFOCUS||message==WM_KILLFOCUS)&&!self->initializing&&self->callbacks.expression_focus)self->callbacks.expression_focus(item,message==WM_SETFOCUS);
        if (message == WM_MOUSEWHEEL && !own_vertical_scroll(item)) return SendMessageW(GetParent(item),message,wparam,lparam);
        if (message == WM_GETDLGCODE) {
            const auto* key = reinterpret_cast<const MSG*>(lparam);
            const auto original = DefSubclassProc(item,message,wparam,lparam);
            return key && key->message == WM_KEYDOWN && key->wParam == VK_RETURN ? original | DLGC_WANTMESSAGE : original;
        }
        if (message == WM_KEYDOWN && wparam == VK_RETURN) { self->submit(); return 0; }
        if (message == WM_CHAR && (wparam == L'\r' || wparam == L'\n')) return 0;
        if (message == WM_NCDESTROY) {self->calculator_buttons.erase(item);RemoveWindowSubclass(item,field_proc,11);}
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK card_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
        auto* self = reinterpret_cast<Impl*>(value);
        if (message == WM_LBUTTONDOWN && self->cards_enabled && IsWindowEnabled(item)) { self->drag = {item,{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)},GetTickCount64(),false}; ClientToScreen(item,&self->drag.start); SetCapture(item); return 0; }
        if (message == WM_MOUSEMOVE && self->drag.window == item && (wparam & MK_LBUTTON) && GetTickCount64() - self->drag.since >= 20) {
            POINT at{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ClientToScreen(item,&at);
            if (std::abs(at.y - self->drag.start.y) > self->px(8)) self->drag.moved = true;
            if (self->drag.moved) { const auto [id,key] = self->order_cards.at(item); const auto& ids = self->orders[id];
                const auto from = static_cast<std::size_t>(std::ranges::find(ids,key) - ids.begin());
                for (std::size_t to = 0; to < ids.size(); ++to) { const auto& row = self->rows[self->order_rows[id][to]]; RECT rect{}; GetWindowRect(row.cells.front().window,&rect);
                    if (at.y >= rect.top && at.y < rect.bottom) { self->reorder(id,from,to); break; } }
            } return 0;
        }
        if (message == WM_LBUTTONUP && self->drag.window == item) { self->drag = {}; if (GetCapture() == item) ReleaseCapture(); return 0; }
        if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(self->host),message,wparam,lparam);
        if (message == WM_NCDESTROY) { if (self->drag.window == item) self->drag = {}; RemoveWindowSubclass(item,card_proc,12); }
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK match_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
        auto* self = reinterpret_cast<Impl*>(value);
        if (message == WM_LBUTTONDOWN && self->cards_enabled && IsWindowEnabled(item)) {
            const auto& card = self->match_cards.at(item);
            if (card.source) self->selected_sources[card.response] = card.id;
            self->drag = {item,{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)},GetTickCount64(),false}; ClientToScreen(item,&self->drag.start); SetCapture(item); return 0;
        }
        if (message == WM_MOUSEMOVE && self->drag.window == item && (wparam & MK_LBUTTON)) {
            POINT at{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ClientToScreen(item,&at);
            self->drag.moved = self->drag.moved || (GetTickCount64() - self->drag.since >= 20 && std::hypot(static_cast<double>(at.x - self->drag.start.x),static_cast<double>(at.y - self->drag.start.y)) > self->px(8));
            return 0;
        }
        if (message == WM_LBUTTONUP && self->drag.window == item) {
            POINT at{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ClientToScreen(item,&at);
            const auto origin = self->match_cards.at(item); const auto can_drop = !self->drag.moved || GetTickCount64() - self->drag.since >= 20;
            self->drag = {}; if (GetCapture() == item) ReleaseCapture();
            if (can_drop) for (const auto& [target,card] : self->match_cards) {
                if (card.source || card.response != origin.response) continue; RECT rectangle{}; GetWindowRect(target,&rectangle);
                if (PtInRect(&rectangle,at) && self->selected_sources.contains(card.response)) { self->match(card.response,self->selected_sources.at(card.response),card.id); break; }
            }
            return 0;
        }
        if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(self->host),message,wparam,lparam);
        if (message == WM_NCDESTROY) { if (self->drag.window == item) self->drag = {}; RemoveWindowSubclass(item,match_proc,13); }
        return DefSubclassProc(item,message,wparam,lparam);
    }
    void initialize() {
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = view_class; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register native math view");
        cls.lpfnWndProc = Canvas::procedure; cls.lpszClassName = canvas_class;cls.style=CS_DBLCLKS;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register native math canvas");
        host = CreateWindowExW(WS_EX_CONTROLPARENT,view_class,L"",WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
        if (!host) throw std::runtime_error("Cannot create native math view");
        SetWindowSubclass(host,text_wheel_proc,14,reinterpret_cast<DWORD_PTR>(&callbacks));
        font = CreateFontW(-MulDiv(18,static_cast<int>(GetDpiForWindow(host)),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        base_font = CreateFontW(-MulDiv(18,static_cast<int>(GetDpiForWindow(host)),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        reader_theme::apply(host,reader_theme::Surface::card); build();
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); self->host = window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(window,message,wparam,lparam);
        LRESULT themed{}; if (reader_theme::control_color(message,wparam,lparam,themed)) return themed;
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; const auto dc = BeginPaint(window,&paint); self->paint_matches(dc); EndPaint(window,&paint); return 0; }
        if (message == WM_ERASEBKGND) { RECT rect{}; GetClientRect(window,&rect); FillRect(reinterpret_cast<HDC>(wparam),&rect,reader_theme::brush(reader_theme::Surface::card)); return TRUE; }
        try {
            if (message == WM_MEASUREITEM && measure_native_menu(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
            if (message == WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                for(const auto& canvas:self->canvases)if(canvas->expand_button==item.hwndItem){(void)reader_theme::draw_button(item);auto icon=item.rcItem;InflateRect(&icon,-self->px(12),-self->px(12));draw_icon(item.hDC,icon,Icon::expand,reader_theme::palette(window).high_contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(190,195,205));return TRUE;}
                if(draw_native_menu(item)||reader_theme::draw_button(item))return TRUE;}
            if (message == WM_MENUCHAR) {
                LRESULT result = 0;
                if (native_menu_character(reinterpret_cast<HMENU>(lparam),static_cast<wchar_t>(LOWORD(wparam)),result)) return result;
            }
            if (message == WM_COMMAND) {
                const auto item = reinterpret_cast<HWND>(lparam); if (HIWORD(wparam) == EN_CHANGE && self->editors.contains(item)) self->edited(item);
                else if (const auto found = self->commands.find(LOWORD(wparam)); found != self->commands.end()) found->second(HIWORD(wparam)); return 0;
            }
            if (message == WM_HSCROLL && lparam) { const auto found = self->commands.find(GetDlgCtrlID(reinterpret_cast<HWND>(lparam))); if (found != self->commands.end()) found->second(0); return 0; }
            if (message == WM_NOTIFY && lparam) { const auto* header = reinterpret_cast<NMHDR*>(lparam);
                if (header->code == EN_REQUESTRESIZE) { const auto* request = reinterpret_cast<REQRESIZE*>(lparam); self->requested_heights[header->hwndFrom] = static_cast<int>(request->rc.bottom - request->rc.top); } return 0; }
            if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(window),message,wparam,lparam);
        } catch (const std::exception& error) { OutputDebugStringW(wide(error.what()).c_str()); }
        return DefWindowProcW(window,message,wparam,lparam);
    }
    void clear() {
        initializing = true;
        for(const auto& canvas:canvases)canvas->close_expanded();
        for (auto item = GetWindow(host,GW_CHILD); item;) { const auto next = GetWindow(item,GW_HWNDNEXT); DestroyWindow(item); item = next; }
        canvases.clear(); rows.clear(); regions.clear(); commands.clear(); editors.clear();expression_editors.clear();keyboard_contexts.clear();calculator_buttons.clear(); orders.clear(); order_rows.clear(); order_cards.clear(); match_cards.clear(); selected_sources.clear(); choice_buttons.clear(); choice_options.clear(); live_visuals.clear(); graph_fields.clear(); requested_heights.clear(); next_id = 100;
    }
};

MathPuzzleView::MathPuzzleView(HWND parent,std::string raw,std::string locale,std::string saved,MathPuzzleCallbacks callbacks)
    : impl_(std::make_unique<Impl>(parent,std::move(raw),std::move(locale),std::move(saved),std::move(callbacks))) { impl_->initialize(); }
std::string math_field_keyboard_context(HWND field){const auto value=static_cast<const std::string*>(GetPropW(field,keyboard_context_property));return value?*value:std::string{};}
bool apply_math_calculator_result(HWND field,std::string_view latex){
    if(!IsWindow(field)||!IsWindowEnabled(field)||math_field_keyboard_context(field).empty())return false;
    wchar_t type[96]{};GetClassNameW(field,type,96);
    if(std::wstring_view(type)==L"LearnGameAndEngineDev.MathExpressionEditor")return set_math_expression_latex(field,latex);
    if(_wcsnicmp(type,L"RichEdit",8)!=0)return false;
    const auto replacement=wide(latex);SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(replacement.c_str()));
    const auto end=GetWindowTextLengthW(field);SendMessageW(field,EM_SETSEL,end,end);SetFocus(field);return true;
}
MathPuzzleView::~MathPuzzleView() { if (impl_) impl_->clear(); }
HWND MathPuzzleView::window() const { return impl_->host; }
int MathPuzzleView::height_for_width(int width) { return impl_->arrange(width,false); }
void MathPuzzleView::layout(int x,int y,int width) { const auto height = impl_->arrange(width); MoveWindow(impl_->host,x,y,width,height,TRUE); }
std::string MathPuzzleView::answers_json() const { impl_->capture_expressions();return stringify(impl_->answers); }
std::string MathPuzzleView::answer_identity_json() const {
    impl_->capture_expressions();auto values=JsonObject::Parse(impl_->answers.Stringify());
    for(const auto& response:values)if(response.Value().ValueType()==JsonValueType::Object){auto value=response.Value().GetObject();value.Remove(L"editorDraft");value.Remove(L"editorDrafts");}
    return stringify(values);
}
std::vector<MathReadingRegion> MathPuzzleView::reading_regions() const {
    auto result = impl_->regions;
    std::ranges::stable_sort(result,[](const auto& left,const auto& right) { RECT a{},b{}; GetWindowRect(left.target,&a); GetWindowRect(right.target,&b);
        return a.top != b.top ? a.top < b.top : a.left < b.left; });
    return result;
}
void MathPuzzleView::set_enabled(bool enabled) {
    impl_->cards_enabled = enabled;
    if (!enabled) {
        if (impl_->drag.window && GetCapture() == impl_->drag.window) ReleaseCapture();
        impl_->drag = {}; impl_->selected_sources.clear();
    }
    for (auto item = GetWindow(impl_->host,GW_CHILD); item; item = GetWindow(item,GW_HWNDNEXT)) {
        const bool view_control=std::ranges::any_of(impl_->canvases,[item](const auto& canvas){return canvas->expand_button==item;});
        const bool expression=std::ranges::any_of(impl_->expression_editors,[item](const auto& value){return value->window==item;});
        const bool interactive = impl_->editors.contains(item) || expression || (impl_->commands.contains(GetDlgCtrlID(item))&&!view_control)
            || std::ranges::any_of(impl_->canvases,[item](const auto& canvas) { return canvas->window == item && canvas->changed; });
        if (interactive) EnableWindow(item,enabled);
    }
    for(const auto& canvas:impl_->canvases)if(canvas->changed)EnableWindow(canvas->window,enabled);
}
void MathPuzzleView::set_text_scale(int percentage) {
    percentage=std::clamp(percentage,75,150);if(impl_->text_scale==percentage)return;impl_->text_scale=percentage;
    const auto next=CreateFontW(-MulDiv(18*percentage,static_cast<int>(GetDpiForWindow(impl_->host)),96*100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
    if(!next)throw std::runtime_error("Cannot scale mathematical text");
    for(auto item=GetWindow(impl_->host,GW_CHILD);item;item=GetWindow(item,GW_HWNDNEXT)) {
        wchar_t name[32]{};GetClassNameW(item,name,32);
        if(_wcsnicmp(name,L"RichEdit",8)==0)SendMessageW(item,EM_SETZOOM,percentage,100);
        else SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
    }
    if(impl_->font)DeleteObject(impl_->font);impl_->font=next;
    for(const auto& canvas:impl_->canvases)canvas->scale_text(percentage);
    impl_->requested_heights.clear();impl_->arrange(impl_->current_width);
}
void MathPuzzleView::reset() { const auto width = impl_->current_width; impl_->clear(); impl_->answers = JsonObject{}; impl_->build(); impl_->arrange(width); if (impl_->callbacks.layout_changed) impl_->callbacks.layout_changed(); impl_->change(); }
} // namespace lgaed::presentation
