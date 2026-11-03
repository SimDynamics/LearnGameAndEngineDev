#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/internal_window.hpp"
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
#include <gdiplus.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <map>
#include <iterator>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr wchar_t view_class[] = L"LearnGameAndEngineDev.MathPuzzle";
constexpr wchar_t canvas_class[] = L"LearnGameAndEngineDev.MathCanvas";
constexpr wchar_t canvas_fullscreen_class[] = L"LearnGameAndEngineDev.MathCanvasFullscreen";
constexpr wchar_t canvas_controls_class[] = L"LearnGameAndEngineDev.MathCanvasControls";
constexpr wchar_t content_class[]=L"LearnGameAndEngineDev.MathContent";
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
double graph_coordinate(double value,double step,double low,double high){
    if(value<=low)return low;if(value>=high)return high;
    return std::clamp(step>0?std::round(value/step)*step:value,low,high);
}
std::optional<double> coordinate(std::string value) {
    const auto first=value.find_first_not_of(" \t\r\n\f\v"),last=value.find_last_not_of(" \t\r\n\f\v");if(first==value.npos)return {};
    value=value.substr(first,last-first+1);std::replace(value.begin(),value.end(),',','.');
    try{std::size_t end=0;const auto result=std::stod(value,&end);if(end==value.size()&&std::isfinite(result))return result;}catch(const std::exception&){}
    return {};
}
std::string decimal(double value) {
    auto result = std::to_string(value); while (result.size() > 1 && result.back() == '0') result.pop_back();
    if (result.back() == '.') result.pop_back(); return result;
}
std::string coordinate_caption(double value){
    if(std::abs(value)<1e-9)value=0;
    char buffer[96]{};std::snprintf(buffer,sizeof(buffer),std::abs(value)<1e12&&value==std::floor(value)?"%.0f":"%.4f",value);
    std::string result=buffer;if(result.find('.')!=result.npos){while(result.back()=='0')result.pop_back();if(result.back()=='.')result.pop_back();}return result;
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
    std::function<std::string(std::string_view)> layout_request;
    JsonObject drawing,inline_drawing;
    int inline_drawing_width=0,inline_drawing_height=0;
    int drawing_width=0,drawing_height=0,text_scale=100;
    std::vector<PlotPoint> points;
    std::optional<std::size_t> dragging;
    double xmin = -5,xmax = 5,ymin = -5,ymax = 5;
    double zoom=1,pan_x=0,pan_y=0;
    POINT pan_last{};bool panning=false;
    std::optional<JsonObject> number_line_click;
    POINT number_line_click_start{};
    std::string rigid_mode="move";bool rigid_dragging=false;POINT rigid_previous{};
    bool construction_locked=false,segment_dragging=false;
    bool segment_failed=false;
    std::string segment_description;
    std::optional<unsigned> segment_point;
    POINT segment_previous{};
    std::function<void(JsonObject)> draft_changed;
    std::function<void()> refresh_segment_controls;
    std::function<void()> segment_controls_layout_changed;
    bool segment_captions_dirty=false;
    int auxiliary_columns=3;
    HWND auxiliary_host=nullptr;
    std::vector<RECT> auxiliary_positions;
    int auxiliary_scroll=0,auxiliary_extent=0,auxiliary_page=0;
    std::vector<HWND> auxiliary_controls;std::vector<RECT> auxiliary_rectangles;
    std::function<LRESULT(UINT,WPARAM,LPARAM)> auxiliary_dispatch;
    ~Canvas() { close_expanded(false);if (scaled_font) DeleteObject(scaled_font); }
    std::string tr(const char* es,const char* en,const char* fr)const{return locale.starts_with("fr")?fr:locale.starts_with("en")?en:es;}
    void clamp_pan(){if(type=="segment_transport")return;RECT client{};GetClientRect(window,&client);const auto x=std::max(0.,client.right*(zoom-1)/2),y=std::max(0.,client.bottom*(zoom-1)/2);pan_x=std::clamp(pan_x,-x,x);pan_y=std::clamp(pan_y,-y,y);}
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
        if(type=="rigid_angle")return 1.1;
        if(type=="segment_transport")return 1.3;
        if(type=="geometry_2d_diagram"||type=="geometry_3d_diagram"||type=="venn_diagram")return 1.2;
        if(type=="math_coordinate_graph"||type=="coordinate_graph"||type=="graph"||(type=="sliders_visual"&&text(config,L"type")=="graph"))return std::clamp((xmax-xmin)/(ymax-ymin),.6,4.);
        if(type=="sliders_visual")return 1.2;
        return 1.;
    }
    void update_slider_points(){
        if(type!="sliders_visual"||text(config,L"type")!="graph")return;
        const auto values=object(state,L"values");const auto parameters=array(config,L"parameters");
        const auto default_id=[&](unsigned at){return at<parameters.Size()?text(parameters.GetObjectAt(at),L"id"):std::string{};};
        const auto slope=number(values,winrt::to_hstring(text(config,L"slopeParameter",default_id(0))).c_str(),1);
        const auto intercept=number(values,winrt::to_hstring(text(config,L"interceptParameter",default_id(1))).c_str(),0);
        points.clear();PlotPoint first,second;first.y=intercept;second.x=1;second.y=intercept+slope;points.push_back(first);points.push_back(second);
    }
    int segment_columns(int width,int gap=0)const{
        int minimum=px(100);
        for(const auto control:auxiliary_controls){const auto dc=GetDC(control);const auto font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));const auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
            std::wstring caption(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');caption.resize(GetWindowTextW(control,caption.data(),static_cast<int>(caption.size())));
            for(std::size_t first=0;first<caption.size();){while(first<caption.size()&&iswspace(caption[first]))++first;auto last=first;while(last<caption.size()&&!iswspace(caption[last]))++last;if(last>first){SIZE size{};GetTextExtentPoint32W(dc,caption.data()+first,static_cast<int>(last-first),&size);minimum=std::max(minimum,static_cast<int>(size.cx)+px(28));}first=last;}
            SelectObject(dc,old);ReleaseDC(control,dc);
        }
        return std::clamp((width+gap)/std::max(1,minimum+gap),1,3);
    }
    int segment_control_height(HWND control,int width)const{
        const auto dc=GetDC(control);const auto font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));const auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
        std::wstring caption(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');caption.resize(GetWindowTextW(control,caption.data(),static_cast<int>(caption.size())));
        RECT measured{0,0,std::max(1,width-px(28)),0};DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,old);ReleaseDC(control,dc);return std::max(px(48),static_cast<int>(measured.bottom-measured.top)+px(16));
    }
    void scroll_auxiliary(int position){
        auxiliary_scroll=std::clamp(position,0,std::max(0,auxiliary_extent-auxiliary_page));
        for(std::size_t index=0;index<auxiliary_controls.size()&&index<auxiliary_positions.size();++index){const auto& box=auxiliary_positions[index];MoveWindow(auxiliary_controls[index],box.left,box.top-auxiliary_scroll,box.right-box.left,box.bottom-box.top,TRUE);}
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL,0,std::max(0,auxiliary_extent-1),static_cast<UINT>(std::max(1,auxiliary_page)),auxiliary_scroll};SetScrollInfo(auxiliary_host,SB_VERT,&info,TRUE);
    }
    void reveal_auxiliary(HWND control){
        const auto found=std::ranges::find(auxiliary_controls,control);if(found==auxiliary_controls.end())return;const auto index=static_cast<std::size_t>(found-auxiliary_controls.begin());if(index>=auxiliary_positions.size())return;
        const auto& box=auxiliary_positions[index];if(box.top<auxiliary_scroll)scroll_auxiliary(box.top);else if(box.bottom>auxiliary_scroll+auxiliary_page)scroll_auxiliary(box.bottom-auxiliary_page);
    }
    static LRESULT CALLBACK controls_proc(HWND target,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Canvas*>(GetWindowLongPtrW(target,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Canvas*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(target,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(target,message,wp,lp);
        if(message==WM_CLOSE||(message==WM_KEYDOWN&&wp==VK_ESCAPE)){self->close_expanded();return 0;}
        if(message==WM_VSCROLL){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(target,SB_VERT,&info);int next=self->auxiliary_scroll;switch(LOWORD(wp)){case SB_LINEUP:next-=self->px(32);break;case SB_LINEDOWN:next+=self->px(32);break;case SB_PAGEUP:next-=self->auxiliary_page;break;case SB_PAGEDOWN:next+=self->auxiliary_page;break;case SB_THUMBPOSITION:case SB_THUMBTRACK:next=info.nTrackPos;break;case SB_TOP:next=0;break;case SB_BOTTOM:next=self->auxiliary_extent;break;}self->scroll_auxiliary(next);return 0;}
        if(message==WM_MOUSEWHEEL){self->scroll_auxiliary(self->auxiliary_scroll-MulDiv(GET_WHEEL_DELTA_WPARAM(wp),self->px(48),WHEEL_DELTA));return 0;}
        if(message==WM_ERASEBKGND){RECT box{};GetClientRect(target,&box);FillRect(reinterpret_cast<HDC>(wp),&box,reader_theme::brush(reader_theme::Surface::card));return 1;}
        if(self->auxiliary_dispatch&&(message==WM_COMMAND||message==WM_DRAWITEM||message==WM_NOTIFY||message==WM_CTLCOLORBTN))return self->auxiliary_dispatch(message,wp,lp);
        return DefWindowProcW(target,message,wp,lp);
    }
    void expanded_layout(){
        if(!fullscreen)return;RECT area{};GetClientRect(fullscreen,&area);const int row=px(48),available=std::max(1,static_cast<int>(area.bottom)-row);
        if(type=="segment_transport"&&auxiliary_host){
            const auto width=std::max(1,static_cast<int>(area.right));const auto interior=std::max(1,width-std::max(px(18),GetSystemMetrics(SM_CXVSCROLL)));const auto gap=px(4);const auto columns=segment_columns(interior,gap);int y=0;auxiliary_positions.clear();
            for(std::size_t first=0;first<auxiliary_controls.size();first+=columns){const auto count=static_cast<int>(std::min<std::size_t>(columns,auxiliary_controls.size()-first));const auto cell_width=std::max(1,(interior-gap*(count-1))/count);int height=px(48);for(int index=0;index<count;++index)height=std::max(height,segment_control_height(auxiliary_controls[first+index],cell_width));for(int index=0;index<count;++index)auxiliary_positions.push_back({index*(cell_width+gap),y,index*(cell_width+gap)+cell_width,y+height});y+=height+gap;}
            auxiliary_extent=std::max(0,y-gap);const auto minimum_canvas=std::max(1,std::min(px(180),available*2/3));auxiliary_page=std::max(1,std::min(auxiliary_extent,available-minimum_canvas));
            MoveWindow(auxiliary_host,0,0,width,auxiliary_page,TRUE);scroll_auxiliary(auxiliary_scroll);MoveWindow(window,0,auxiliary_page,width,std::max(1,available-auxiliary_page),FALSE);prepare_drawing();InvalidateRect(window,nullptr,FALSE);
            MoveWindow(GetDlgItem(fullscreen,canvas_close_id),std::max(0L,area.right-row),available,row,row,TRUE);return;
        }
        const auto ratio=aspect_ratio();const int width=std::max(1,std::min(static_cast<int>(area.right),static_cast<int>(available*ratio))),height=std::max(1,std::min(available,static_cast<int>(width/ratio)));
        const int left=(area.right-width)/2,top=(available-height)/2;
        const auto columns=std::max(1,auxiliary_columns);
        const auto row_count=(static_cast<int>(auxiliary_controls.size())+columns-1)/columns;
        const auto control_height=type=="segment_transport"?std::max(row,px(32*text_scale/100+20)):row;
        const auto controls=row_count*control_height;
        for(int index=0;index<static_cast<int>(auxiliary_controls.size());++index){const int at_row=index/columns,at_column=index%columns,count=std::min(columns,static_cast<int>(auxiliary_controls.size())-at_row*columns);MoveWindow(auxiliary_controls[index],left+width*at_column/count,top+at_row*control_height,width*(at_column+1)/count-width*at_column/count,control_height,TRUE);}
        MoveWindow(window,left,top+controls,width,std::max(1,height-controls),FALSE);prepare_drawing();InvalidateRect(window,nullptr,FALSE);
        MoveWindow(GetDlgItem(fullscreen,canvas_close_id),std::max(0L,area.right-row),available,row,row,TRUE);clamp_pan();
    }
    void close_expanded(bool refresh=true){
        if(!fullscreen)return;const auto frame=fullscreen;fullscreen=nullptr;zoom=1;pan_x=pan_y=0;panning=false;dragging.reset();segment_dragging=false;segment_point.reset();
        if(GetCapture()==window)ReleaseCapture();
        if(IsWindow(window)&&IsWindow(inline_parent)){SetParent(window,inline_parent);drawing=inline_drawing;drawing_width=inline_drawing_width;drawing_height=inline_drawing_height;MoveWindow(window,inline_rectangle.left,inline_rectangle.top,inline_rectangle.right-inline_rectangle.left,inline_rectangle.bottom-inline_rectangle.top,TRUE);
            for(std::size_t index=0;index<auxiliary_controls.size();++index)if(IsWindow(auxiliary_controls[index])&&index<auxiliary_rectangles.size()){SetParent(auxiliary_controls[index],inline_parent);const auto& rect=auxiliary_rectangles[index];MoveWindow(auxiliary_controls[index],rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,TRUE);}}
        auxiliary_host=nullptr;auxiliary_positions.clear();auxiliary_scroll=auxiliary_extent=auxiliary_page=0;
        if(IsWindow(frame))DestroyWindow(frame);if(refresh&&IsWindow(window)){drawing_width=drawing_height=0;prepare_drawing();InvalidateRect(window,nullptr,FALSE);}if(IsWindow(previous_focus)&&IsWindowEnabled(previous_focus))SetFocus(previous_focus);
    }
    static LRESULT CALLBACK exit_key_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
        auto* self=reinterpret_cast<Canvas*>(data);
        if(self&&self->auxiliary_host&&GetParent(item)==self->auxiliary_host){if(message==WM_SETFOCUS)self->reveal_auxiliary(item);if(message==WM_MOUSEWHEEL)return SendMessageW(self->auxiliary_host,message,wp,lp);}
        if(message==WM_KEYDOWN&&wp==VK_ESCAPE){SendMessageW(GetParent(item),WM_CLOSE,0,0);return 0;}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(item,exit_key_proc,id);
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK fullscreen_proc(HWND target,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Canvas*>(GetWindowLongPtrW(target,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Canvas*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(target,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(target,message,wp,lp);
        if(message==WM_SIZE||message==WM_DPICHANGED_AFTERPARENT){self->expanded_layout();return 0;}
        // IsDialogMessage routes Escape over ordinary controls as IDCANCEL;
        // canvas-local Escape still arrives directly when requested by a view.
        if(message==WM_CLOSE||(message==WM_COMMAND&&(LOWORD(wp)==canvas_close_id||LOWORD(wp)==IDCANCEL))||(message==WM_KEYDOWN&&wp==VK_ESCAPE)){self->close_expanded();return 0;}
        if(message==WM_ERASEBKGND){RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::palette(target).high_contrast?GetSysColorBrush(COLOR_WINDOW):reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));return 1;}
        if(message==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item.CtlID==canvas_close_id){
            FillRect(item.hDC,&item.rcItem,reader_theme::palette(target).high_contrast?GetSysColorBrush(COLOR_WINDOW):reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));auto icon=item.rcItem;InflateRect(&icon,-self->px(12),-self->px(12));draw_icon(item.hDC,icon,Icon::close,reader_theme::palette(target).high_contrast?GetSysColor(COLOR_WINDOWTEXT):RGB(190,195,205));return TRUE;}}
        if(self->auxiliary_dispatch&&(message==WM_COMMAND||message==WM_DRAWITEM||message==WM_NOTIFY||message==WM_CTLCOLORBTN))return self->auxiliary_dispatch(message,wp,lp);
        return DefWindowProcW(target,message,wp,lp);
    }
    void expand(){
        if(fullscreen||!IsWindow(window))return;previous_focus=GetFocus();inline_parent=GetParent(window);GetWindowRect(window,&inline_rectangle);MapWindowPoints(nullptr,inline_parent,reinterpret_cast<POINT*>(&inline_rectangle),2);inline_drawing=drawing;inline_drawing_width=drawing_width;inline_drawing_height=drawing_height;
        WNDCLASSEXW definition{sizeof(definition)};definition.lpfnWndProc=fullscreen_proc;definition.hInstance=GetModuleHandleW(nullptr);definition.lpszClassName=canvas_fullscreen_class;definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassExW(&definition)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register the expanded mathematics viewport");
        fullscreen=create_internal_window(inline_parent,canvas_fullscreen_class,wide(tr("Representación ampliada","Expanded representation","Représentation agrandie")).c_str(),this);
        reader_theme::apply(fullscreen,reader_theme::Surface::card);reader_theme::set_accent(fullscreen,reader_theme::palette(inline_parent).accent);
        const auto close=CreateWindowExW(0,L"BUTTON",wide(tr("Cerrar representación ampliada","Close expanded representation","Fermer la représentation agrandie")).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,1,1,fullscreen,reinterpret_cast<HMENU>(static_cast<INT_PTR>(canvas_close_id)),GetModuleHandleW(nullptr),nullptr);
        if(!close){DestroyWindow(fullscreen);fullscreen=nullptr;throw std::runtime_error("Cannot create the expanded mathematics exit control");}
        reader_theme::apply(close,reader_theme::Surface::card);reader_theme::icon_button(close);
        SetWindowSubclass(close,exit_key_proc,19,0);
        if(type=="segment_transport"){
            WNDCLASSEXW controls_definition{sizeof(controls_definition)};controls_definition.lpfnWndProc=controls_proc;controls_definition.hInstance=GetModuleHandleW(nullptr);controls_definition.lpszClassName=canvas_controls_class;controls_definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);
            if(!RegisterClassExW(&controls_definition)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register the mathematics controls viewport");
            auxiliary_host=CreateWindowExW(WS_EX_CONTROLPARENT,canvas_controls_class,wide(tr("Controles de construcción","Construction controls","Commandes de construction")).c_str(),WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,1,1,fullscreen,nullptr,GetModuleHandleW(nullptr),this);
            if(!auxiliary_host)throw std::runtime_error("Cannot create the mathematics controls viewport");reader_theme::apply(auxiliary_host,reader_theme::Surface::card);
        }
        auxiliary_rectangles.clear();for(const auto item:auxiliary_controls){RECT rect{};GetWindowRect(item,&rect);MapWindowPoints(nullptr,inline_parent,reinterpret_cast<POINT*>(&rect),2);auxiliary_rectangles.push_back(rect);SetParent(item,auxiliary_host?auxiliary_host:fullscreen);SetWindowSubclass(item,exit_key_proc,19,type=="segment_transport"?reinterpret_cast<DWORD_PTR>(this):0);}
        SetParent(window,fullscreen);expanded_layout();ShowWindow(fullscreen,SW_SHOW);SetFocus(window);
    }
    void scale_text(int percentage) {
        text_scale=percentage;drawing_width=drawing_height=0;
        const auto base=type=="euclidean_diagram"||type=="rigid_angle"||type=="segment_transport"||type=="geometry_2d_diagram"||type=="geometry_3d_diagram"?15:type=="venn_diagram"?14:
            type=="math_number_line"||type=="number_line"?12:type=="graph"||type=="math_coordinate_graph"||type=="coordinate_graph"||(type=="sliders_visual"&&text(config,L"type")=="graph")?11:18;
        const auto next=CreateFontW(-MulDiv(base*percentage,static_cast<int>(GetDpiForWindow(window)),96*100),
            0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(next){if(scaled_font)DeleteObject(scaled_font);scaled_font=next;}InvalidateRect(window,nullptr,FALSE);
    }
    std::string segment_failure_message()const{
        return tr("No se pudo actualizar el dibujo. El intento se conserva; vuelve a intentarlo.","The drawing could not be updated. Your attempt is preserved; please try again.","Le dessin n’a pas pu être actualisé. Votre tentative est conservée ; réessayez.");
    }
    void segment_failure(bool failed){
        if(segment_failed==failed)return;segment_failed=failed;
        const auto description=segment_description+(failed?". "+segment_failure_message():"");
        SetWindowTextW(window,wide(description).c_str());
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,window,OBJID_CLIENT,CHILDID_SELF);
        InvalidateRect(window,nullptr,FALSE);
    }
    bool prepare_drawing(std::optional<JsonObject> gesture={}){
        if(type!="segment_transport"){prepare_drawing_impl(gesture);return true;}
        // The worker is a fallible technical boundary, including when resize
        // requests arrive through a window procedure. Publish no partial state.
        const auto saved_state=state,saved_drawing=drawing;
        const auto saved_width=drawing_width,saved_height=drawing_height;
        const auto recover=[&]{state=saved_state;drawing=saved_drawing;drawing_width=saved_width;drawing_height=saved_height;segment_failure(true);};
        try{
            if(prepare_drawing_impl(gesture))segment_failure(false);
            return !segment_failed;
        }catch(const winrt::hresult_error&){recover();return false;}
        catch(const std::exception&){recover();return false;}
    }
    bool prepare_drawing_impl(std::optional<JsonObject> gesture){
        if(type!="euclidean_diagram"&&type!="rigid_angle"&&type!="segment_transport")return false;
        RECT area{};GetClientRect(window,&area);if(area.right<=0||area.bottom<=0||(!gesture&&drawing_width==area.right&&drawing_height==area.bottom))return false;
        if(!layout_request)throw std::runtime_error("The Euclidean representation requires its original geometry layout service");
        const auto dc=GetDC(window);const auto previous=SelectObject(dc,scaled_font);TEXTMETRICW font_metrics{};GetTextMetricsW(dc,&font_metrics);
        const auto unit=static_cast<double>(GetDpiForWindow(window))/96.,edge=std::min(12*unit,std::min(area.right,area.bottom)*.08);
        const auto maximum=std::max(1.,std::min(area.right*.43,area.right-2*edge));JsonObject metrics;
        const auto measure=[&](const std::string& value){
            if(value.empty()||metrics.HasKey(winrt::to_hstring(value)))return;
            const auto original=wide(value);JsonArray lines;double width=0;std::size_t offset=0;
            while(offset<original.size()){
                auto end=offset,last_space=offset;SIZE extent{};
                while(end<original.size()){
                    auto next=end+1;if(IS_HIGH_SURROGATE(original[end])&&next<original.size()&&IS_LOW_SURROGATE(original[next]))++next;
                    GetTextExtentPoint32W(dc,original.data()+offset,static_cast<int>(next-offset),&extent);
                    if(extent.cx>maximum&&end>offset)break;
                    if(iswspace(original[end]))last_space=next;end=next;
                }
                if(type!="rigid_angle"&&end<original.size()&&last_space>offset)end=last_space;
                const auto line=original.substr(offset,end-offset);GetTextExtentPoint32W(dc,line.data(),static_cast<int>(line.size()),&extent);
                width=std::max(width,static_cast<double>(extent.cx));lines.Append(string_value(utf8(line)));offset=end;
            }
            JsonObject value_metrics;value_metrics.SetNamedValue(L"lines",lines);value_metrics.SetNamedValue(L"width",JsonValue::CreateNumberValue(width));
            value_metrics.SetNamedValue(L"lineHeight",JsonValue::CreateNumberValue(font_metrics.tmHeight+2*unit));value_metrics.SetNamedValue(L"ascent",JsonValue::CreateNumberValue(font_metrics.tmAscent));metrics.SetNamedValue(winrt::to_hstring(value),value_metrics);
        };
        const auto measure_diagram=[&](const JsonObject& diagram){for(const auto& item:array(diagram,L"points"))for(const auto& value:array(item.GetObject(),L"labels"))measure(winrt::to_string(value.GetString()));for(const auto key:{L"objects",L"circles",L"arcs"})for(const auto& item:array(diagram,key))measure(text(item.GetObject(),L"label"));};
        if(type=="rigid_angle"){measure_diagram(object(object(config,L"reference"),L"diagram"));measure_diagram(object(object(config,L"copy"),L"diagram"));}
        else if(type=="segment_transport"){
            for(const auto& value:array(config,L"pointLabels"))if(value.ValueType()==JsonValueType::String)measure(winrt::to_string(value.GetString()));
            for(const auto& value:array(config,L"sources")){const auto source=value.GetObject();measure(text(source,L"label"));for(const auto key:{L"from",L"to"}){const auto point=object(source,key);measure(text(point,L"label"));for(const auto& label:array(point,L"labels"))if(label.ValueType()==JsonValueType::String)measure(winrt::to_string(label.GetString()));}}
            const auto target=object(config,L"target");for(const auto key:{L"origin",L"end",L"through"}){const auto point=object(target,key);measure(text(point,L"label"));for(const auto& label:array(point,L"labels"))if(label.ValueType()==JsonValueType::String)measure(winrt::to_string(label.GetString()));}
        }else measure_diagram(config);
        SelectObject(dc,previous);ReleaseDC(window,dc);
        JsonObject request;request.SetNamedValue(L"action",string_value(type=="segment_transport"?"segment_layout":type=="rigid_angle"?"rigid_layout":"diagram_layout"));request.SetNamedValue(L"config",config);
        if(type=="rigid_angle"){request.SetNamedValue(L"placement",state);if(gesture)request.SetNamedValue(L"gesture",*gesture);}
        if(type=="segment_transport"){request.SetNamedValue(L"state",state);if(gesture)request.SetNamedValue(L"gesture",*gesture);}
        request.SetNamedValue(L"width",JsonValue::CreateNumberValue(area.right));request.SetNamedValue(L"height",JsonValue::CreateNumberValue(area.bottom));
        request.SetNamedValue(L"unit",JsonValue::CreateNumberValue(unit));request.SetNamedValue(L"textSize",JsonValue::CreateNumberValue(15*unit*text_scale/100));request.SetNamedValue(L"labelMetrics",metrics);
        const auto next_drawing=JsonObject::Parse(winrt::to_hstring(layout_request(stringify(request))));
        if(!next_drawing.HasKey(L"viewport"))throw std::runtime_error(text(next_drawing,L"error","The original Euclidean layout did not return its drawing"));
        if(type=="segment_transport"&&!next_drawing.HasKey(L"state"))throw std::runtime_error("The segment construction did not return its original state");
        drawing=next_drawing;
        if(type=="rigid_angle"&&gesture)state=object(drawing,L"placement");
        if(type=="segment_transport"){
            state=object(drawing,L"state");if(refresh_segment_controls)refresh_segment_controls();
        }
        drawing_width=area.right;drawing_height=area.bottom;
        return true;
    }
    std::string segment_mode()const{return text(object(state,L"interactionDraft"),L"mode","place");}
    void segment_changed(bool semantic){
        if(semantic){if(changed)changed(state);}else if(draft_changed)draft_changed(state);
        if(refresh_segment_controls)refresh_segment_controls();
        if(segment_captions_dirty){segment_captions_dirty=false;if(fullscreen)expanded_layout();else if(segment_controls_layout_changed)segment_controls_layout_changed();}
        InvalidateRect(window,nullptr,FALSE);
    }
    void segment_gesture(JsonObject gesture,bool semantic=false){
        const auto action=text(gesture,L"type");if(construction_locked&&(action=="place"||action=="move_point"))return;
        const auto previous=state.Stringify(),previous_points=array(state,L"points").Stringify();if(!prepare_drawing(gesture))return;
        if(previous!=state.Stringify())segment_changed(semantic&&previous_points!=array(state,L"points").Stringify());
    }
    void segment_zoom(POINT at,double delta){
        JsonObject gesture,current;current.SetNamedValue(L"x",JsonValue::CreateNumberValue(at.x));current.SetNamedValue(L"y",JsonValue::CreateNumberValue(at.y));gesture.SetNamedValue(L"type",string_value("zoom"));gesture.SetNamedValue(L"current",current);gesture.SetNamedValue(L"delta",JsonValue::CreateNumberValue(delta));segment_gesture(gesture);
    }
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96); }
    RECT bounds() const {
        RECT result{};GetClientRect(window,&result);
        if(type=="math_number_line"||type=="number_line"){result.left=px(28);result.right-=px(28);return result;}
        const auto dc=GetDC(window);const auto old=SelectObject(dc,scaled_font?scaled_font:GetStockObject(DEFAULT_GUI_FONT));TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
        const auto measure=[&](double value){const auto content=wide(coordinate_caption(value));SIZE size{};GetTextExtentPoint32W(dc,content.c_str(),static_cast<int>(content.size()),&size);return static_cast<double>(size.cx);};
        const auto widest_y=std::max({measure(ymin),measure(ymax),measure(0)}),edge_x=std::max(measure(xmin),measure(xmax))/2;
        SelectObject(dc,old);ReleaseDC(window,dc);
        const auto left=std::max(widest_y+px(8),edge_x+px(4)),right=result.right-std::max(edge_x+px(4),static_cast<double>(px(18)));
        const auto top=static_cast<double>(std::max<LONG>(metrics.tmHeight,px(16))),bottom=static_cast<double>(result.bottom-std::max<LONG>(metrics.tmHeight+px(12),px(30)));
        const auto aspect=(xmax-xmin)/(ymax-ymin),width=std::max(1.,std::min(right-left,(bottom-top)*aspect)),height=width/aspect;
        return {static_cast<LONG>(std::lround((left+right-width)/2)),static_cast<LONG>(std::lround((top+bottom-height)/2)),static_cast<LONG>(std::lround((left+right+width)/2)),static_cast<LONG>(std::lround((top+bottom+height)/2))};
    }
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
            point.draggable=index>=initial.Size()||flag(initial.GetObjectAt(index),L"draggable",true);
            if(type=="graph"&&changed){point.x=graph_coordinate(point.x,number(config,L"snap",1),xmin,xmax);point.y=graph_coordinate(point.y,number(config,L"snap",1),ymin,ymax);}
            for (const auto& label : array(item,L"labels")) if (label.ValueType() == JsonValueType::String) point.labels.push_back(winrt::to_string(label.GetString()));
            points.push_back(std::move(point));
        }
        if(points.empty()&&changed&&type=="graph"){
            const auto mode=text(config,L"mode","point");
            const std::vector<std::pair<double,double>> defaults=mode=="line"||mode=="parabola"?std::vector<std::pair<double,double>>{{0,0},{1,1}}:
                mode=="polygon"?std::vector<std::pair<double,double>>{{-2,-1},{2,-1},{0,2}}:
                mode=="scatter"?std::vector<std::pair<double,double>>{{-2,0},{0,1},{2,2}}:std::vector<std::pair<double,double>>{{0,0}};
            for(const auto [x,y]:defaults){PlotPoint point;point.x=graph_coordinate(x,number(config,L"snap",1),xmin,xmax);point.y=graph_coordinate(y,number(config,L"snap",1),ymin,ymax);point.draggable=true;points.push_back(point);}
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
        if (xmax <= xmin) xmax = xmin + 10; if (ymax <= ymin) ymax = ymin + 10;
        update_slider_points();
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
    void paint_euclidean(HDC dc)const{
        if(!drawing.HasKey(L"viewport"))return;
        const auto colors=reader_theme::palette(window);const auto unit=static_cast<double>(GetDpiForWindow(window))/96.;
        const auto position=[](const JsonObject& value){return POINT{static_cast<LONG>(std::lround(number(value,L"x"))),static_cast<LONG>(std::lround(number(value,L"y")))};};
        const bool rigid=type=="rigid_angle"||type=="segment_transport";const auto amber=colors.high_contrast?colors.text:RGB(255,190,95);
        const auto ink=[&](const JsonObject& item,const wchar_t* id=L"id"){return rigid&&text(item,id).starts_with(type=="segment_transport"?"tool:":"copy:")?amber:colors.accent;};
        const auto stroke=CreatePen(PS_SOLID,std::max(1,static_cast<int>(std::lround(2.2*unit))),colors.accent),copy_stroke=CreatePen(PS_SOLID,std::max(1,static_cast<int>(std::lround(2.2*unit))),amber);const auto old_pen=SelectObject(dc,stroke),old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        if(rigid){
            struct GdiRuntime{ULONG_PTR token{};GdiRuntime(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);}~GdiRuntime(){if(token)Gdiplus::GdiplusShutdown(token);}};static GdiRuntime runtime;
            Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);XFORM world{};if(GetWorldTransform(dc,&world)){Gdiplus::Matrix matrix(world.eM11,world.eM12,world.eM21,world.eM22,world.eDx,world.eDy);graphics.SetTransform(&matrix);}
            for(const auto& value:array(drawing,L"regions")){const auto item=value.GetObject(),center=object(item,L"center");const auto tint=ink(item);const auto radius=number(item,L"radius");
                const Gdiplus::RectF box(static_cast<Gdiplus::REAL>(number(center,L"x")-radius),static_cast<Gdiplus::REAL>(number(center,L"y")-radius),static_cast<Gdiplus::REAL>(radius*2),static_cast<Gdiplus::REAL>(radius*2));
                Gdiplus::SolidBrush fill(Gdiplus::Color(53,GetRValue(tint),GetGValue(tint),GetBValue(tint)));Gdiplus::Pen border(Gdiplus::Color(255,GetRValue(tint),GetGValue(tint),GetBValue(tint)),static_cast<Gdiplus::REAL>(unit*1.5));
                graphics.FillPie(&fill,box,static_cast<Gdiplus::REAL>(number(item,L"startDegrees")),static_cast<Gdiplus::REAL>(number(item,L"sweepDegrees")));graphics.DrawArc(&border,box,static_cast<Gdiplus::REAL>(number(item,L"startDegrees")),static_cast<Gdiplus::REAL>(number(item,L"sweepDegrees")));}
        }
        const auto blend=[](BYTE foreground,BYTE background){return (foreground*42+background*213)/255;};
        const auto disk=CreateSolidBrush(RGB(blend(GetRValue(colors.accent),GetRValue(colors.canvas)),blend(GetGValue(colors.accent),GetGValue(colors.canvas)),blend(GetBValue(colors.accent),GetBValue(colors.canvas))));
        const auto circle=[&](POINT center,double radius){const auto r=static_cast<LONG>(std::lround(radius));Ellipse(dc,center.x-r,center.y-r,center.x+r,center.y+r);};
        for(const auto& value:array(drawing,L"circles")){const auto item=value.GetObject();if(flag(item,L"filled"))SelectObject(dc,disk);circle(position(object(item,L"center")),number(item,L"radius"));SelectObject(dc,GetStockObject(NULL_BRUSH));}
        for(const auto& value:array(drawing,L"paths")){const auto item=value.GetObject();SelectObject(dc,ink(item)==amber?copy_stroke:stroke);line(dc,position(object(item,L"start")),position(object(item,L"end")));}
        for(const auto& value:array(drawing,L"arrows")){const auto item=value.GetObject(),tip=object(item,L"tip");SelectObject(dc,ink(item)==amber?copy_stroke:stroke);line(dc,position(tip),position(object(item,L"wingA")));line(dc,position(tip),position(object(item,L"wingB")));}
        const auto arc_pen=CreatePen(PS_SOLID,std::max(1,static_cast<int>(std::lround(3.6*unit))),colors.accent);SelectObject(dc,arc_pen);
        for(const auto& value:array(drawing,L"arcs")){const auto item=value.GetObject(),center=object(item,L"center");const auto radius=number(item,L"radius"),start=number(item,L"startDegrees"),sweep=number(item,L"sweepDegrees");
            const auto at=[&](double degrees){const auto angle=degrees*3.14159265358979323846/180;return POINT{static_cast<LONG>(std::lround(number(center,L"x")+radius*std::cos(angle))),static_cast<LONG>(std::lround(number(center,L"y")+radius*std::sin(angle)))};};
            auto from=at(start);for(int step=1;step<=96;++step){const auto to=at(start+sweep*step/96);line(dc,from,to);from=to;}}
        SelectObject(dc,GetStockObject(NULL_PEN));const auto background=CreateSolidBrush(colors.canvas),foreground=CreateSolidBrush(colors.high_contrast?colors.text:RGB(255,255,255)),copy_foreground=CreateSolidBrush(amber);
        if(rigid){const auto leader=CreatePen(PS_SOLID,std::max(1,px(1)),colors.text),copy_leader=CreatePen(PS_SOLID,std::max(1,px(1)),amber);
            for(const auto& value:array(drawing,L"labels")){const auto item=value.GetObject(),box=object(item,L"bounds");for(const auto& point_value:array(drawing,L"points")){const auto point=point_value.GetObject();if(text(point,L"id")!=text(item,L"id"))continue;const auto center=position(object(point,L"center"));SelectObject(dc,ink(item)==amber?copy_leader:leader);line(dc,{std::clamp<LONG>(center.x,static_cast<LONG>(number(box,L"left")),static_cast<LONG>(number(box,L"right"))),std::clamp<LONG>(center.y,static_cast<LONG>(number(box,L"top")),static_cast<LONG>(number(box,L"bottom")))},center);break;}}
            SelectObject(dc,GetStockObject(NULL_PEN));DeleteObject(leader);DeleteObject(copy_leader);}
        const auto protected_ink=CreateRectRgn(0,0,0,0);
        for(const auto& value:array(drawing,L"points")){const auto item=value.GetObject();const auto center=position(object(item,L"center"));SelectObject(dc,background);circle(center,5.6*unit);SelectObject(dc,rigid&&ink(item)==amber?copy_foreground:foreground);circle(center,4*unit);
            if(rigid){const auto radius=static_cast<int>(std::ceil(6.6*unit));POINT marker_bounds[2]{{center.x-radius,center.y-radius},{center.x+radius,center.y+radius}};LPtoDP(dc,marker_bounds,2);const auto marker=CreateEllipticRgn(marker_bounds[0].x,marker_bounds[0].y,marker_bounds[1].x,marker_bounds[1].y);CombineRgn(protected_ink,protected_ink,marker,RGN_OR);DeleteObject(marker);}}
        if(rigid)for(const auto& value:array(drawing,L"arrows")){const auto item=value.GetObject();const auto tip=position(object(item,L"tip"));for(const auto key:{L"wingA",L"wingB"}){const auto wing=position(object(item,key));const auto dx=static_cast<double>(wing.x-tip.x),dy=static_cast<double>(wing.y-tip.y),length=std::max(1.,std::hypot(dx,dy)),r=1.8*unit;POINT polygon[4]{{static_cast<LONG>(std::lround(tip.x-dy*r/length)),static_cast<LONG>(std::lround(tip.y+dx*r/length))},{static_cast<LONG>(std::lround(wing.x-dy*r/length)),static_cast<LONG>(std::lround(wing.y+dx*r/length))},{static_cast<LONG>(std::lround(wing.x+dy*r/length)),static_cast<LONG>(std::lround(wing.y-dx*r/length))},{static_cast<LONG>(std::lround(tip.x+dy*r/length)),static_cast<LONG>(std::lround(tip.y-dx*r/length))}};LPtoDP(dc,polygon,4);const auto arrow=CreatePolygonRgn(polygon,4,WINDING);CombineRgn(protected_ink,protected_ink,arrow,RGN_OR);DeleteObject(arrow);}}
        const auto previous_alignment=SetTextAlign(dc,TA_LEFT|TA_BASELINE);SetTextColor(dc,colors.high_contrast?colors.text:RGB(255,255,255));
        for(const auto& value:array(drawing,L"labels")){const auto item=value.GetObject(),box=object(item,L"bounds");SelectObject(dc,background);const auto saved=SaveDC(dc);if(rigid)ExtSelectClipRgn(dc,protected_ink,RGN_DIFF);
            RoundRect(dc,static_cast<int>(std::floor(number(box,L"left"))),static_cast<int>(std::floor(number(box,L"top"))),static_cast<int>(std::ceil(number(box,L"right"))),static_cast<int>(std::ceil(number(box,L"bottom"))),px(2),px(2));
            RestoreDC(dc,saved);SetTextColor(dc,rigid&&ink(item)==amber?amber:colors.high_contrast?colors.text:RGB(255,255,255));
            const auto lines=array(item,L"lines");for(unsigned index=0;index<lines.Size();++index){const auto content=std::wstring(lines.GetStringAt(index));TextOutW(dc,static_cast<int>(std::lround(number(box,L"left")+2*unit)),static_cast<int>(std::lround(number(box,L"top")+unit+number(item,L"ascent")+index*number(item,L"lineHeight"))),content.c_str(),static_cast<int>(content.size()));}}
        SetTextAlign(dc,previous_alignment);SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(stroke);DeleteObject(copy_stroke);DeleteObject(arc_pen);DeleteObject(disk);DeleteObject(background);DeleteObject(foreground);DeleteObject(copy_foreground);DeleteObject(protected_ink);
    }
    void paint(HDC dc) {
        const auto colors = reader_theme::palette(window); RECT client{}; GetClientRect(window,&client);
        FillRect(dc,&client,reader_theme::brush(reader_theme::Surface::canvas));const auto transform=SaveDC(dc);
        if(zoom>1){SetGraphicsMode(dc,GM_ADVANCED);XFORM matrix{static_cast<FLOAT>(zoom),0,0,static_cast<FLOAT>(zoom),static_cast<FLOAT>(client.right*(1-zoom)/2+pan_x),static_cast<FLOAT>(client.bottom*(1-zoom)/2+pan_y)};SetWorldTransform(dc,&matrix);}
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,colors.text);
        const auto font = scaled_font ? scaled_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)); const auto old_font = SelectObject(dc,font);
        const auto pen = CreatePen(PS_SOLID,px(2),reader_theme::accent_ink(window,colors.canvas)), grid = CreatePen(PS_SOLID,px(1),colors.high_contrast?colors.outline:RGB(51,56,65));
        const auto previous_pen = SelectObject(dc,pen), previous_brush = SelectObject(dc,GetStockObject(NULL_BRUSH));
        const auto rect = bounds();
        if (type == "math_number_line" || type == "number_line") {
            // The same geometry and label policy as MathNumberLineView: both
            // arrows, bounded tick density and an explicitly optional value.
            const auto low=number(config,L"min",-10),high=number(config,L"max",10)>low?number(config,L"max",10):low+20;
            const auto step=number(config,L"step",1)>0?number(config,L"step",1):1;
            const auto y=client.bottom/2;const auto x_at=[&](double value){return rect.left+static_cast<LONG>(std::lround((value-low)/(high-low)*(rect.right-rect.left)));};
            const auto saved=SaveDC(dc);const auto axis=CreatePen(PS_SOLID,px(2),colors.high_contrast?colors.text:RGB(204,204,204));SelectObject(dc,axis);
            const auto arrow=[&](LONG x,bool left){line(dc,{x,y},{x+(left?px(8):-px(8)),y-px(6)});line(dc,{x,y},{x+(left?px(8):-px(8)),y+px(6)});};
            line(dc,{rect.left,y},{rect.right,y});arrow(rect.left,true);arrow(rect.right,false);
            const auto alignment=SetTextAlign(dc,TA_CENTER|TA_BASELINE);SetTextColor(dc,colors.high_contrast?colors.text:RGB(204,204,204));
            const auto caption=[&](std::string value,LONG x,LONG baseline){const auto source=wide(value);TextOutW(dc,x,baseline,source.c_str(),static_cast<int>(source.size()));};
            const auto tick_step=step*std::max(1.,std::ceil((high-low)/step/16.));
            for(unsigned index=0;index<=18;++index){const auto value=low+index*tick_step;if(value>high+1e-10)break;const auto x=x_at(value);line(dc,{x,y-px(5)},{x,y+px(5)});caption(coordinate_caption(value),x,y+px(24));}
            const auto value=std::clamp(number(state,L"value",number(config,L"value")),low,high);const auto x=x_at(value);
            const auto direction=text(state,L"direction",text(config,L"direction","none"));const auto ray=CreatePen(PS_SOLID,px(5),colors.accent);SelectObject(dc,ray);
            if(direction=="left"||direction=="right"){const auto end=direction=="left"?rect.left:rect.right;line(dc,{x,y},{end,y});arrow(end,direction=="left");}
            const auto background=CreateSolidBrush(colors.canvas);SelectObject(dc,background);SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,x-px(8),y-px(8),x+px(8),y+px(8));
            const auto marker=CreatePen(PS_SOLID,px(3),colors.accent);const auto fill=CreateSolidBrush(colors.accent);SelectObject(dc,marker);SelectObject(dc,flag(state,L"closed",flag(config,L"closed",true))?fill:GetStockObject(NULL_BRUSH));Ellipse(dc,x-px(7),y-px(7),x+px(7),y+px(7));
            const auto mode=text(config,L"mode");HFONT value_font=nullptr;
            if(flag(config,L"showValueLabel",mode!="read_value"&&mode!="missing_value_hint")){
                value_font=CreateFontW(-MulDiv(15*text_scale,static_cast<int>(GetDpiForWindow(window)),96*100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
                if(value_font)SelectObject(dc,value_font);SetTextColor(dc,colors.high_contrast?colors.text:RGB(255,255,255));caption(coordinate_caption(value),x,y-px(21));}
            SetTextAlign(dc,alignment);RestoreDC(dc,saved);DeleteObject(axis);DeleteObject(ray);DeleteObject(background);DeleteObject(marker);DeleteObject(fill);if(value_font)DeleteObject(value_font);
        } else if (type == "euclidean_diagram"||type=="rigid_angle"||type=="segment_transport") {
            paint_euclidean(dc);
        } else if (type == "venn_diagram") {
            const auto w=client.right,h=client.bottom;const auto radius=std::min(w*.25,h*.32),center_y=h*.49,left_x=w*.4,right_x=w*.6;
            const auto frame=CreatePen(PS_SOLID,px(2),colors.high_contrast?colors.outline:RGB(98,109,121));SelectObject(dc,frame);Rectangle(dc,px(10),px(12),w-px(10),h-px(12));SelectObject(dc,pen);DeleteObject(frame);
            const auto circle=[&](double x){Ellipse(dc,static_cast<int>(std::lround(x-radius)),static_cast<int>(std::lround(center_y-radius)),static_cast<int>(std::lround(x+radius)),static_cast<int>(std::lround(center_y+radius)));};circle(left_x);circle(right_x);
            const auto previous_alignment=SetTextAlign(dc,TA_CENTER|TA_BASELINE);SetTextColor(dc,colors.high_contrast?colors.text:RGB(255,255,255));
            const auto caption=[&](std::string value,double x,double y){const auto source=wide(value);TextOutW(dc,static_cast<int>(std::lround(x)),static_cast<int>(std::lround(y)),source.c_str(),static_cast<int>(source.size()));};
            caption(text(config,L"leftLabel","A"),left_x-radius*.25,center_y-radius-px(8));caption(text(config,L"rightLabel","B"),right_x+radius*.25,center_y-radius-px(8));
            const auto lines=[&](const wchar_t* key,double x){const auto source=text(config,key);std::size_t at=0;for(unsigned index=0;index<4;++index){const auto end=source.find('\n',at);caption(source.substr(at,end==source.npos?end:end-at),x,center_y+index*px(18)*text_scale/100.);if(end==source.npos)break;at=end+1;}};
            lines(L"leftOnly",left_x-radius*.35);lines(L"intersection",w/2.);lines(L"rightOnly",right_x+radius*.35);caption(text(config,L"outside"),w/2.,h-px(25));SetTextAlign(dc,TA_LEFT|TA_BASELINE);caption(text(config,L"universeLabel","U"),px(18),px(30));SetTextAlign(dc,previous_alignment);
        } else if (type == "sliders_visual"&&text(config,L"type")!="graph") {
            const auto values=object(state,L"values");const auto parameters=array(config,L"parameters");const auto scale_id=text(config,L"scaleParameter");
            const auto dimension=[&](const wchar_t* key,unsigned at,double fallback){const auto default_id=at<parameters.Size()?text(parameters.GetObjectAt(at),L"id"):std::string{};
                return number(values,winrt::to_hstring(text(config,key,default_id!=scale_id?default_id:std::string{})).c_str(),fallback);};
            const auto scale=number(values,winrt::to_hstring(scale_id).c_str(),1);
            const auto width_value=dimension(L"widthParameter",0,number(config,L"baseWidth",3))*scale;
            const auto height_value=dimension(L"heightParameter",1,number(config,L"baseHeight",2))*scale;
            double maximum=10,scale_maximum=1;if(parameters.Size())maximum=0;
            for(const auto& value:parameters){const auto parameter=value.GetObject();maximum=std::max({maximum,std::abs(number(parameter,L"min")),std::abs(number(parameter,L"max"))});if(text(parameter,L"id")==scale_id)scale_maximum=number(parameter,L"max",1);}
            const auto reference=std::max(1.,maximum*std::max(1.,scale_maximum)),unit=std::min(client.right*.72,client.bottom*.66)/reference;
            const auto width=std::abs(width_value)*unit,height=std::abs(height_value)*unit;
            const RECT box{static_cast<LONG>(std::lround((client.right-width)/2)),static_cast<LONG>(std::lround((client.bottom-height)/2)),
                static_cast<LONG>(std::lround((client.right+width)/2)),static_cast<LONG>(std::lround((client.bottom+height)/2))};
            const auto blend=[](BYTE foreground,BYTE background){return (foreground*85+background*170)/255;};
            const auto fill=CreateSolidBrush(RGB(blend(GetRValue(colors.accent),GetRValue(colors.canvas)),blend(GetGValue(colors.accent),GetGValue(colors.canvas)),blend(GetBValue(colors.accent),GetBValue(colors.canvas))));
            const auto old=SelectObject(dc,fill);Rectangle(dc,box.left,box.top,box.right,box.bottom);SelectObject(dc,old);DeleteObject(fill);
            const auto caption=wide(decimal(width_value)+" × "+decimal(height_value));RECT label_bounds{0,client.bottom-px(35),client.right,client.bottom};
            LOGFONTW label_font{};GetObjectW(font,sizeof(label_font),&label_font);label_font.lfHeight=MulDiv(label_font.lfHeight,15,18);
            const auto caption_font=CreateFontIndirectW(&label_font);const auto before_font=SelectObject(dc,caption_font);
            const auto before_ink=SetTextColor(dc,colors.high_contrast?colors.text:RGB(255,255,255));
            DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&label_bounds,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX|DT_VCENTER);
            SetTextColor(dc,before_ink);SelectObject(dc,before_font);DeleteObject(caption_font);
        } else if (type == "geometry_2d_diagram" || type == "geometry_3d_diagram") {
            const bool solid=type=="geometry_3d_diagram";const auto shape=text(config,L"shape",solid?"cube":"rectangle");
            const auto left=client.right*.22,top=client.bottom*.18,right=client.right*.75,bottom=client.bottom*.72;
            const auto cx=(left+right)/2,cy=(top+bottom)/2,width=right-left,height=bottom-top;
            const auto ellipse=[&](double x1,double y1,double x2,double y2){Ellipse(dc,static_cast<int>(std::lround(x1)),static_cast<int>(std::lround(y1)),static_cast<int>(std::lround(x2)),static_cast<int>(std::lround(y2)));};
            const auto segment=[&](double x1,double y1,double x2,double y2){line(dc,{static_cast<LONG>(std::lround(x1)),static_cast<LONG>(std::lround(y1))},{static_cast<LONG>(std::lround(x2)),static_cast<LONG>(std::lround(y2))});};
            if(solid&&shape=="cylinder"){const auto oval=height*.22;ellipse(left,top,right,top+oval);ellipse(left,bottom-oval,right,bottom);segment(left,top+oval/2,left,bottom-oval/2);segment(right,top+oval/2,right,bottom-oval/2);}
            else if(solid&&shape=="cone"){const auto oval=height*.22;ellipse(left,bottom-oval,right,bottom);segment(cx,top,left,bottom-oval/2);segment(cx,top,right,bottom-oval/2);}
            else if(solid&&shape=="sphere"){const auto radius=std::min(width,height)/2;ellipse(cx-radius,cy-radius,cx+radius,cy+radius);ellipse(cx-radius,cy-radius/3,cx+radius,cy+radius/3);ellipse(cx-radius/3,cy-radius,cx+radius/3,cy+radius);}
            else if(solid){const auto offset=std::min(width,height)*.23;Rectangle(dc,static_cast<int>(left),static_cast<int>(top+offset),static_cast<int>(right-offset),static_cast<int>(bottom));Rectangle(dc,static_cast<int>(left+offset),static_cast<int>(top),static_cast<int>(right),static_cast<int>(bottom-offset));segment(left,top+offset,left+offset,top);segment(right-offset,top+offset,right,top);segment(left,bottom,left+offset,bottom-offset);segment(right-offset,bottom,right,bottom-offset);}
            else if(shape=="circle"){const auto radius=std::min(width,height)/2;ellipse(cx-radius,cy-radius,cx+radius,cy+radius);}
            else if(shape=="triangle"){const POINT vertices[]{{static_cast<LONG>(left),static_cast<LONG>(bottom)},{static_cast<LONG>(right),static_cast<LONG>(bottom)},{static_cast<LONG>(cx),static_cast<LONG>(top)}};Polygon(dc,vertices,3);}
            else Rectangle(dc,static_cast<int>(left),static_cast<int>(top),static_cast<int>(right),static_cast<int>(bottom));
            const auto labels=array(config,L"labels");const auto previous_alignment=SetTextAlign(dc,TA_CENTER|TA_BASELINE);SetTextColor(dc,colors.high_contrast?colors.text:RGB(255,255,255));
            for(unsigned index=0;index<std::min(3U,labels.Size());++index){const auto caption=std::wstring(labels.GetStringAt(index));SetTextAlign(dc,(index==0?TA_CENTER:index==1?TA_LEFT:TA_RIGHT)|TA_BASELINE);TextOutW(dc,index==0?client.right/2:index==1?px(10):client.right-px(10),index==0?client.bottom-px(15):static_cast<int>(client.bottom*(index==1?.5:.22)),caption.c_str(),static_cast<int>(caption.size()));}SetTextAlign(dc,previous_alignment);
        } else {
            SelectObject(dc,grid);
            const auto grid_step=std::max(number(config,L"gridStep",1)>0?number(config,L"gridStep",1):1,std::max(xmax-xmin,ymax-ymin)/100.);
            for(unsigned index=0;index<=101;++index){const auto x=std::ceil(xmin/grid_step)*grid_step+index*grid_step;if(x>xmax)break;line(dc,project(x,ymin),project(x,ymax));}
            for(unsigned index=0;index<=101;++index){const auto y=std::ceil(ymin/grid_step)*grid_step+index*grid_step;if(y>ymax)break;line(dc,project(xmin,y),project(xmax,y));}
            Rectangle(dc,rect.left,rect.top,rect.right,rect.bottom);
            const auto axes=CreatePen(PS_SOLID,std::max(1,px(3)/2),colors.high_contrast?colors.text:RGB(192,201,211));SelectObject(dc,axes);
            if(xmin<=0&&xmax>=0)line(dc,project(0,ymin),project(0,ymax));if(ymin<=0&&ymax>=0)line(dc,project(xmin,0),project(xmax,0));
            TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SetTextColor(dc,colors.high_contrast?colors.text:RGB(192,201,211));const auto alignment=SetTextAlign(dc,TA_CENTER|TA_BASELINE);
            const auto caption=[&](std::string content,LONG x,LONG y){const auto source=wide(content);TextOutW(dc,x,y,source.c_str(),static_cast<int>(source.size()));};
            const auto label_step=grid_step*std::max(1.,std::ceil(std::max(xmax-xmin,ymax-ymin)/grid_step/12.));
            for(unsigned index=0;index<=15;++index){const auto x=std::ceil(xmin/label_step)*label_step+index*label_step;if(x>xmax)break;caption(coordinate_caption(x),project(x,ymin).x,rect.bottom+metrics.tmAscent+px(4));}
            SetTextAlign(dc,TA_RIGHT|TA_BASELINE);for(unsigned index=0;index<=15;++index){const auto y=std::ceil(ymin/label_step)*label_step+index*label_step;if(y>ymax)break;caption(coordinate_caption(y),rect.left-px(6),project(xmin,y).y+(metrics.tmAscent-metrics.tmDescent)/2);}
            SetTextAlign(dc,TA_LEFT|TA_BASELINE);caption("x",rect.right+px(5),rect.bottom+metrics.tmAscent+px(4));caption("y",rect.left-px(4),std::max<LONG>(metrics.tmAscent,rect.top-px(6)));SetTextAlign(dc,alignment);SelectObject(dc,pen);DeleteObject(axes);
            const auto mode = type=="sliders_visual"?"line":text(config,L"mode","point"); const auto saved = SaveDC(dc); IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
            if (mode == "line" && points.size() >= 2) {
                const auto& first = points[0]; const auto& second = points[1];
                if (std::abs(second.x - first.x) < 1e-10) line(dc,project(first.x,ymin),project(first.x,ymax));
                else { const auto slope = (second.y - first.y) / (second.x - first.x);
                    line(dc,project(xmin,first.y + slope * (xmin - first.x)),project(xmax,first.y + slope * (xmax - first.x))); }
            } else if (mode == "polygon") {
                if(points.size()>=2){std::vector<POINT> polygon;for(const auto& point:points)polygon.push_back(project(point.x,point.y));
                    const auto blend=[](BYTE foreground,BYTE background){return static_cast<BYTE>((foreground*51+background*204)/255);};
                    const auto fill=CreateSolidBrush(RGB(blend(GetRValue(colors.accent),GetRValue(colors.canvas)),blend(GetGValue(colors.accent),GetGValue(colors.canvas)),blend(GetBValue(colors.accent),GetBValue(colors.canvas))));const auto old=SelectObject(dc,fill);Polygon(dc,polygon.data(),static_cast<int>(polygon.size()));SelectObject(dc,old);DeleteObject(fill);}
            } else if (mode == "parabola" && points.size() >= 2 && std::abs(points[1].x - points[0].x) > 1e-10) {
                const auto dx = points[1].x - points[0].x, a = (points[1].y - points[0].y) / (dx * dx);
                const auto value_at = [&](double x) { return a * (x - points[0].x) * (x - points[0].x) + points[0].y; };
                auto from = project(xmin,value_at(xmin));
                for (int step = 1; step <= 200; ++step) { const auto x = xmin + (xmax - xmin) * step / 200;
                    const auto to = project(x,value_at(x)); line(dc,from,to); from = to; }
            }
            RestoreDC(dc,saved);
        }
        if (type == "math_coordinate_graph" || type == "coordinate_graph" || type == "graph" || (type=="sliders_visual"&&text(config,L"type")=="graph")) {
            const auto saved=SaveDC(dc);IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);SelectObject(dc,GetStockObject(NULL_PEN));SelectObject(dc,GetStockObject(DC_BRUSH));
            TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SetTextAlign(dc,TA_LEFT|TA_BASELINE);
            for(std::size_t index=0;index<points.size();++index){const auto& point=points[index];const auto at=project(point.x,point.y);
                SetDCBrushColor(dc,changed&&point.draggable?colors.accent:colors.high_contrast?colors.text:RGB(204,204,204));Ellipse(dc,at.x-px(6),at.y-px(6),at.x+px(6),at.y+px(6));
                if(flag(config,L"showLabels",true)){const auto prefix=points.size()>1?(index<26?std::string(1,static_cast<char>('A'+index)):"P"+std::to_string(index+1))+": ":std::string{};
                    const auto content=wide(prefix+"("+coordinate_caption(point.x)+", "+coordinate_caption(point.y)+")");SIZE size{};GetTextExtentPoint32W(dc,content.c_str(),static_cast<int>(content.size()),&size);
                    const auto preferred=at.x+px(9)+size.cx<=rect.right?at.x+px(9):at.x-px(9)-size.cx;
                    const auto x=std::clamp(preferred,rect.left+px(2),std::max(rect.left+px(2),rect.right-size.cx-px(2))),y=std::clamp(at.y-px(9),rect.top+metrics.tmAscent,std::max(rect.top+metrics.tmAscent,rect.bottom-metrics.tmDescent));
                    SetTextColor(dc,colors.high_contrast?colors.text:RGB(255,255,255));TextOutW(dc,x,y,content.c_str(),static_cast<int>(content.size()));}}
            RestoreDC(dc,saved);SelectObject(dc,previous_brush);
        } else SelectObject(dc,previous_brush);
        if(type=="segment_transport"&&segment_failed){
            const auto saved=SaveDC(dc);SetTextAlign(dc,TA_LEFT|TA_TOP);const auto message=wide(segment_failure_message());
            RECT notice{px(8),px(8),std::max(px(9),static_cast<int>(client.right)-px(8)),px(8)};
            DrawTextW(dc,message.c_str(),static_cast<int>(message.size()),&notice,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
            auto background=notice;InflateRect(&background,px(4),px(4));FillRect(dc,&background,reader_theme::brush(reader_theme::Surface::card));
            SetTextColor(dc,colors.high_contrast?colors.text:colors.muted);DrawTextW(dc,message.c_str(),static_cast<int>(message.size()),&notice,DT_WORDBREAK|DT_NOPREFIX);
            RestoreDC(dc,saved);
        }
        SelectObject(dc,previous_pen); SelectObject(dc,old_font); DeleteObject(pen); DeleteObject(grid);RestoreDC(dc,transform);
    }
    void pointer(POINT at) {
        if (!changed) return;
        if(type=="segment_transport"){
            if(!segment_dragging)return;JsonObject gesture,previous,current;const auto mode=segment_mode();
            previous.SetNamedValue(L"x",JsonValue::CreateNumberValue(segment_previous.x));previous.SetNamedValue(L"y",JsonValue::CreateNumberValue(segment_previous.y));current.SetNamedValue(L"x",JsonValue::CreateNumberValue(at.x));current.SetNamedValue(L"y",JsonValue::CreateNumberValue(at.y));
            gesture.SetNamedValue(L"type",string_value(segment_point?"move_point":mode));gesture.SetNamedValue(L"previous",previous);gesture.SetNamedValue(L"current",current);
            if(segment_point)gesture.SetNamedValue(L"index",JsonValue::CreateNumberValue(*segment_point));
            segment_gesture(gesture,segment_point.has_value());segment_previous=at;return;
        }
        if(type=="rigid_angle"){
            if(!rigid_dragging)return;JsonObject gesture,previous,current;previous.SetNamedValue(L"x",JsonValue::CreateNumberValue(rigid_previous.x));previous.SetNamedValue(L"y",JsonValue::CreateNumberValue(rigid_previous.y));current.SetNamedValue(L"x",JsonValue::CreateNumberValue(at.x));current.SetNamedValue(L"y",JsonValue::CreateNumberValue(at.y));gesture.SetNamedValue(L"mode",string_value(rigid_mode));gesture.SetNamedValue(L"previous",previous);gesture.SetNamedValue(L"current",current);
            prepare_drawing(gesture);rigid_previous=at;changed(state);InvalidateRect(window,nullptr,FALSE);return;
        }
        const auto rect = bounds();
        if (type == "number_line") {
            const auto low = number(config,L"min",-5),high = number(config,L"max",5);
            const auto value = snapped(low + static_cast<double>(at.x - rect.left) / std::max<LONG>(1,rect.right - rect.left) * (high - low),number(config,L"step",1),low,high);
            state.SetNamedValue(L"value",JsonValue::CreateNumberValue(value)); changed(state);if(sync_fields)sync_fields();InvalidateRect(window,nullptr,FALSE); return;
        }
        if (!dragging || *dragging >= points.size()) return;
        auto& point = points[*dragging]; const auto snap = number(config,L"snap",1);
        point.x = graph_coordinate(xmin + static_cast<double>(at.x - rect.left) / std::max<LONG>(1,rect.right - rect.left) * (xmax - xmin),snap,xmin,xmax);
        point.y = graph_coordinate(ymin + static_cast<double>(rect.bottom - at.y) / std::max<LONG>(1,rect.bottom - rect.top) * (ymax - ymin),snap,ymin,ymax);
        JsonArray values; for (const auto& current : points) { JsonObject item; item.SetNamedValue(L"x",JsonValue::CreateNumberValue(current.x)); item.SetNamedValue(L"y",JsonValue::CreateNumberValue(current.y)); values.Append(item); }
        state.SetNamedValue(L"points",values); changed(state); if (sync_fields) sync_fields(); InvalidateRect(window,nullptr,FALSE);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
        auto* self = reinterpret_cast<Canvas*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Canvas*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); self->window = window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(window,message,wparam,lparam);
        if(message==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(wparam));return 0;}
        if(message==WM_LBUTTONDBLCLK){
            // Placement clicks remain construction clicks. Enlarging has its
            // own button, and only View mode interprets a double click as zoom.
            if(self->type=="segment_transport"){
                self->segment_dragging=false;self->segment_point.reset();if(GetCapture()==window)ReleaseCapture();
                if(self->segment_mode()=="view")self->segment_zoom({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)},1);return 0;
            }
            // Windows sends a complete first click before identifying a double
            // click. Keep zoom/expand input-neutral without delaying ordinary
            // number-line clicks or losing their existing immediate response.
            if(self->number_line_click&&self->changed){self->state=*self->number_line_click;self->changed(self->state);self->number_line_click.reset();}
            self->dragging.reset();self->panning=false;if(GetCapture()==window)ReleaseCapture();if(!self->fullscreen)self->expand();else self->toggle_zoom({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)});return 0;}
        if(message==WM_KEYDOWN&&wparam==VK_ESCAPE&&self->fullscreen){self->close_expanded();return 0;}
        if(message==WM_KEYDOWN&&self->type=="segment_transport"&&(wparam==VK_ADD||wparam==VK_OEM_PLUS||wparam==VK_SUBTRACT||wparam==VK_OEM_MINUS)){
            RECT area{};GetClientRect(window,&area);self->segment_zoom({area.right/2,area.bottom/2},wparam==VK_ADD||wparam==VK_OEM_PLUS?1:-1);return 0;
        }
        if(message==WM_SIZE){self->clamp_pan();return 0;}
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; const auto dc = BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
            const auto buffer=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,bounds.right),std::max(1L,bounds.bottom));const auto previous=SelectObject(buffer,bitmap);
            self->paint(buffer);BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,previous);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(window,&paint);return 0; }
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_LBUTTONDOWN && self->changed && IsWindowEnabled(window)) {
            if(self->type=="segment_transport"){
                const POINT at{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};const auto mode=self->segment_mode();
                self->segment_previous=at;self->segment_point.reset();
                if(mode=="place"){
                    if(self->construction_locked)return 0;
                    const auto active=number(object(self->state,L"interactionDraft"),L"activePoint");auto nearest=static_cast<double>(self->px(24));
                    // Selecting the next name permits even a coincident or
                    // too-short construction; only the evaluator judges it.
                    if(active<array(self->state,L"points").Size())for(const auto& value:array(self->drawing,L"constructedPoints")){const auto point=value.GetObject(),center=object(point,L"center");const auto distance=std::hypot(number(center,L"x")-at.x,number(center,L"y")-at.y);if(distance<=nearest){self->segment_point=static_cast<unsigned>(number(point,L"index"));nearest=distance;}}
                    if(!self->segment_point){
                        JsonObject gesture,current;current.SetNamedValue(L"x",JsonValue::CreateNumberValue(at.x));current.SetNamedValue(L"y",JsonValue::CreateNumberValue(at.y));gesture.SetNamedValue(L"type",string_value("place"));gesture.SetNamedValue(L"index",JsonValue::CreateNumberValue(active));gesture.SetNamedValue(L"current",current);
                        self->segment_gesture(gesture,true);const auto count=array(self->state,L"points").Size();if(active>=0&&active<count)self->segment_point=static_cast<unsigned>(active);
                    }
                    if(!self->segment_point){SetFocus(window);return 0;}
                }
                self->segment_dragging=true;SetFocus(window);SetCapture(window);return 0;
            }
            POINT at=self->content_point({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)});
            if(self->type=="rigid_angle"&&self->rigid_mode!="view"){self->rigid_dragging=true;self->rigid_previous=at;SetCapture(window);return 0;}
            auto nearest=static_cast<double>(self->px(32));
            for (std::size_t index = 0; index < self->points.size(); ++index) {
                const auto point = self->project(self->points[index].x,self->points[index].y);
                const auto distance=std::hypot(static_cast<double>(point.x-at.x),static_cast<double>(point.y-at.y));
                if(self->points[index].draggable&&distance<=nearest){self->dragging=index;nearest=distance;}
            }
            if(self->dragging)SetCapture(window);
            if (self->type == "number_line") {self->number_line_click=JsonObject::Parse(self->state.Stringify());self->number_line_click_start={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};SetCapture(window); self->pointer(at); }
            else if(!self->dragging&&self->zoom>1){self->panning=true;self->pan_last={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};SetCapture(window);}return 0;
        }
        if(message==WM_LBUTTONDOWN&&self->zoom>1){self->panning=true;self->pan_last={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};SetCapture(window);return 0;}
        if (message == WM_MOUSEMOVE && GetCapture() == window && (wparam & MK_LBUTTON)) {
            if(self->number_line_click&&(std::abs(GET_X_LPARAM(lparam)-self->number_line_click_start.x)>GetSystemMetrics(SM_CXDRAG)/2||std::abs(GET_Y_LPARAM(lparam)-self->number_line_click_start.y)>GetSystemMetrics(SM_CYDRAG)/2))self->number_line_click.reset();
            if(self->panning){self->pan_x+=GET_X_LPARAM(lparam)-self->pan_last.x;self->pan_y+=GET_Y_LPARAM(lparam)-self->pan_last.y;self->pan_last={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};self->clamp_pan();InvalidateRect(window,nullptr,FALSE);}
            else self->pointer(self->type=="segment_transport"?POINT{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}:self->content_point({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}));return 0; }
        if (message == WM_LBUTTONUP && GetCapture() == window) {self->rigid_dragging=false;self->segment_dragging=false;self->segment_point.reset();self->panning=false;self->dragging.reset();ReleaseCapture();return 0;}
        if(message==WM_CAPTURECHANGED){self->rigid_dragging=false;self->segment_dragging=false;self->segment_point.reset();self->panning=false;self->dragging.reset();return 0;}
        if(message==WM_MOUSEWHEEL&&self->type=="segment_transport"&&self->segment_mode()=="view"){
            POINT at{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ScreenToClient(window,&at);self->segment_zoom(at,static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam))/WHEEL_DELTA);return 0;
        }
        if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(window),message,wparam,lparam);
        return DefWindowProcW(window,message,wparam,lparam);
    }
};
}

struct MathPuzzleView::Impl {
    struct Cell { HWND window; int width = 0; bool text = false;int inset_x=0,inset_y=0; };
    struct Row { std::vector<Cell> cells; int height = 42; int gap_after=8; int indent=0; };
    struct Content {HWND window=nullptr;std::vector<Row> rows;bool horizontal=false,natural_columns=false;int padding_x=0,padding_y=0,gap=0,minimum_column=0,scroll=0,extent=0;POINT drag_origin{};int drag_scroll=0;bool dragging=false,moved=false;};
    HWND host = nullptr,parent = nullptr;
    std::string locale; JsonObject block,answers,initial;
    MathPuzzleCallbacks callbacks;
    HMODULE rich = nullptr; HFONT font = nullptr,base_font = nullptr;
    Microsoft::WRL::ComPtr<IAccPropServices> accessibility;
    std::vector<Row> rows; std::vector<MathReadingRegion> regions; std::vector<std::unique_ptr<Canvas>> canvases;
    std::map<HWND,std::unique_ptr<Content>> contents;
    std::map<HWND,HWND> card_children;
    std::map<HWND,int> requested_widths;
    std::map<HWND,int> intrinsic_widths;
    std::map<std::pair<std::string,std::string>,std::vector<NativeMathFragment>> typeset_fragments;
    std::map<HWND,std::pair<std::string,RichTextRole>> label_formats;
    std::map<int,std::function<void(int)>> commands;
    std::map<HWND,std::pair<std::string,std::string>> editors;
    struct ExpressionBinding {HWND window=nullptr;std::string id,field;};
    std::vector<std::unique_ptr<ExpressionBinding>> expression_editors;
    std::map<HWND,std::string> keyboard_contexts;
    std::map<HWND,HWND> calculator_buttons;
    std::map<HWND,bool> input_disabled_readonly;
    std::map<std::string,std::vector<std::string>> orders;
    std::map<std::string,std::vector<std::size_t>> order_rows;
    std::map<HWND,std::pair<std::string,std::string>> order_cards;
    struct MatchCard { std::string response,id; bool source = false; };
    std::map<HWND,MatchCard> match_cards;
    std::map<std::string,std::string> selected_sources;
    std::map<std::string,std::vector<HWND>> choice_buttons;
    std::map<HWND,std::string> choice_options;
    std::map<HWND,HWND> choice_labels;
    std::map<HWND,std::vector<HWND>> choice_cards;
    std::map<std::string,std::size_t> next_tap_positions;
    struct MatchLayout {
        std::size_t first = 0;
        Row available_heading,target_heading;
        std::vector<std::string> sources,targets;
        std::map<std::string,Row> source_rows,target_rows;
    };
    std::map<std::string,MatchLayout> matching_layouts;
    std::map<HWND,HWND> matching_empty_labels;
    HWND expand_button = nullptr,reset_button = nullptr;
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
        // MathPuzzleParser gives legacy standalone interactions the same stable
        // response ID. Normalize presentation only; never rewrite the lesson.
        if(text(block,L"type")!="math_puzzle") {
            auto response=JsonObject::Parse(block.Stringify());response.SetNamedValue(L"id",string_value("response"));
            JsonArray responses;responses.Append(response);block.SetNamedValue(L"responses",responses);
        }
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
    HWND label(std::string value,std::string key = {},RichTextRole role = RichTextRole::body,bool passive = true,std::string_view math_mode="auto") {
        const auto item = make(MSFTEDIT_CLASS,"",ES_MULTILINE | ES_READONLY);
        if(!reader_theme::palette(item).high_contrast)reader_theme::set_text_color(item,RGB(235,239,245));
        set_rich_text(item,value,{},false,role,locale); regions.push_back({key.empty() ? "text/" + std::to_string(regions.size()) : std::move(key),std::move(value),item});
        const auto& original = regions.back().source_text;
        label_formats[item]={std::string(math_mode),role};
        if(math_mode=="inline"||math_mode=="block")set_native_math_text(item,original,locale);
        else if(math_mode!="text"&&callbacks.expression_request){
            const auto fragment_key=std::pair{original,std::string(math_mode)};auto found=typeset_fragments.find(fragment_key);
            if(found==typeset_fragments.end()){JsonObject request;request.SetNamedValue(L"action",string_value("text_layout"));request.SetNamedValue(L"text",string_value(original));request.SetNamedValue(L"mode",string_value(math_mode));
                const auto reply=JsonObject::Parse(wide(callbacks.expression_request(stringify(request))));if(!reply.HasKey(L"fragments"))throw std::runtime_error(text(reply,L"error","The original mathematical text renderer returned no source ranges"));
                std::vector<NativeMathFragment> fragments;for(const auto& fragment_value:array(reply,L"fragments")){const auto fragment=fragment_value.GetObject();const auto start=number(fragment,L"start",-1),end=number(fragment,L"end",-1);if(start<0||end<=start||std::floor(start)!=start||std::floor(end)!=end||end>wide(original).size())throw std::runtime_error("The original mathematical text renderer returned an invalid source range");fragments.push_back({static_cast<std::size_t>(start),static_cast<std::size_t>(end),text(fragment,L"formula"),flag(fragment,L"literal")});}
                found=typeset_fragments.emplace(fragment_key,std::move(fragments)).first;
            }
            set_native_math_fragments(item,original,found->second,role,locale);
        }else if(math_mode!="text"&&(original.find("\\(")!=original.npos||original.find("\\[")!=original.npos))set_native_math_inline_text(item,original,{},role,locale);
        // MathWidgetUi.text uses normal platform line spacing, including
        // instructions, labels and tables; lesson prose uses a separate role.
        PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);paragraph.dwMask=PFM_LINESPACING;paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;
        SendMessageW(item,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
        SendMessageW(item,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE);
        SetWindowSubclass(item,text_wheel_proc,14,passive?reinterpret_cast<DWORD_PTR>(&callbacks):0);
        return item;
    }
    void add_text(std::string value,std::string key = {},RichTextRole role = RichTextRole::body) { if (!value.empty()) rows.push_back({{{label(std::move(value),std::move(key),role),0,true}},26}); }
    HWND button(std::string value,std::function<void()> action,DWORD style = BS_PUSHBUTTON) {
        const auto item = make(L"BUTTON",value,WS_TABSTOP | style); commands[GetDlgCtrlID(item)] = [this,action = std::move(action)](int notification) { if (!initializing&&notification == BN_CLICKED) action(); }; return item;
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
            auto next=object(answers,winrt::to_hstring(id).c_str());
            // Incomplete editing invalidates checking, not the last valid
            // marker displayed on the number line (Android preserves it).
            if(plot->type=="number_line"&&!next.HasKey(L"value")&&plot->state.HasKey(L"value"))next.SetNamedValue(L"value",plot->state.GetNamedValue(L"value"));
            plot->state = next;plot->update_slider_points();InvalidateRect(plot->window,nullptr,FALSE);
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
        reader_theme::apply(item,reader_theme::Surface::card);reader_theme::icon_button(item);calculator_buttons[field]=item;layout_calculator(field);
    }
    void input_ink(HWND item,COLORREF color=RGB(235,239,245)){
        if(!reader_theme::palette(item).high_contrast)reader_theme::set_text_color(item,color);
        // A RichEdit document owns its insertion color independently from its
        // parent's WM_CTLCOLOREDIT DC. Set both restored text and future keys.
        CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;format.crTextColor=reader_theme::foreground(item);
        SendMessageW(item,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));SendMessageW(item,EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
    }
    HWND input(const std::string& id,const std::string& field,JsonObject current,std::string hint = {},const JsonObject& config={},const JsonObject& validator={},std::string_view mode="NUMERIC") {
        const auto item = make(MSFTEDIT_CLASS,"",WS_TABSTOP | ES_AUTOHSCROLL);
        reader_theme::apply(item,reader_theme::Surface::card);
        const auto values = field.empty() ? current : object(current,L"values");
        set_edit_text(item,text(values,field.empty() ? L"value" : winrt::to_hstring(field).c_str()));
        input_ink(item);
        if(hint.empty())hint=tr("Respuesta","Answer","Réponse");
        set_edit_hint(item,hint,RGB(174,183,198));
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
        access.minimum_height=56;access.horizontal_padding=12;access.vertical_padding=10;
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
    template<class F>void visit_cells(const std::vector<Row>& source,const F& visit){
        for(const auto& row:source)for(const auto& cell:row.cells){visit(cell);if(const auto at=contents.find(cell.window);at!=contents.end())visit_cells(at->second->rows,visit);}
    }
    HWND group_content(std::size_t first,bool horizontal=false,bool natural_columns=false,int padding_x=0,int padding_y=0,int gap=0,int minimum_column=0){
        auto content=std::make_unique<Content>();content->horizontal=horizontal;content->natural_columns=natural_columns;content->padding_x=padding_x;content->padding_y=padding_y;content->gap=gap;content->minimum_column=minimum_column;
        content->rows.assign(std::make_move_iterator(rows.begin()+static_cast<std::ptrdiff_t>(first)),std::make_move_iterator(rows.end()));rows.erase(rows.begin()+static_cast<std::ptrdiff_t>(first),rows.end());
        content->window=CreateWindowExW(WS_EX_CONTROLPARENT,content_class,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|(horizontal?WS_TABSTOP:0),0,0,1,1,host,reinterpret_cast<HMENU>(static_cast<INT_PTR>(next_id++)),GetModuleHandleW(nullptr),this);
        if(!content->window)throw std::runtime_error("Cannot create the original mathematical content container");
        reader_theme::apply(content->window,reader_theme::Surface::card);
        for(const auto& row:content->rows)for(const auto& cell:row.cells){SetParent(cell.window,content->window);if(horizontal)SetWindowSubclass(cell.window,scroll_child_proc,19,reinterpret_cast<DWORD_PTR>(this));}
        const auto result=content->window;contents.emplace(result,std::move(content));rows.push_back({{{result}},0});return result;
    }
    void bind_card_children(HWND card){
        const auto found=contents.find(card);if(found==contents.end())return;
        visit_cells(found->second->rows,[&](const Cell& cell){wchar_t type[40]{};GetClassNameW(cell.window,type,40);if(_wcsicmp(type,L"BUTTON")==0)return;card_children[cell.window]=card;SetWindowSubclass(cell.window,card_child_proc,18,reinterpret_cast<DWORD_PTR>(this));});
    }
    int intrinsic_text_width(HWND item){
        if(const auto at=intrinsic_widths.find(item);at!=intrinsic_widths.end())return at->second;
        // These labels start life at1px. Disabling wrapping alone does not
        // expand RichEdit's initial formatting rectangle. Give its real text
        // engine an unbounded measurement surface before reading glyph edges.
        SendMessageW(item,EM_SETOPTIONS,ECOOP_OR,ECO_AUTOHSCROLL);SendMessageW(item,EM_SETTARGETDEVICE,0,1);
        const auto source=wide(edit_text(item));const auto dc=GetDC(item);const auto old=SelectObject(dc,font);SIZE extent{};TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);GetTextExtentPoint32W(dc,source.c_str(),static_cast<int>(source.size()),&extent);SelectObject(dc,old);ReleaseDC(item,dc);
        const auto measure_width=std::max<LONG>(px(256),extent.cx*2+metrics.tmHeight*2);RECT previous{};GetClientRect(item,&previous);
        SetWindowPos(item,nullptr,0,0,measure_width,std::max<LONG>(px(48),previous.bottom),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS);
        RECT measuring{0,0,measure_width,std::max<LONG>(px(48),previous.bottom)};SendMessageW(item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&measuring));SendMessageW(item,EM_REQUESTRESIZE,0,0);
        GETTEXTLENGTHEX length{GTL_NUMCHARS,1200};const auto count=SendMessageW(item,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
        POINT scroll{};SendMessageW(item,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));RECT format{};SendMessageW(item,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));int width=0;
        // EN_REQUESTRESIZE reports the control's current formatting width.
        // Native character edges retain the real unwrapped/TOM math advance,
        // including fractions, styled glyphs and the selected text scale.
        for(LRESULT at=0;at<=count;++at){POINT edge{};SendMessageW(item,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&edge),at);width=std::max(width,static_cast<int>(edge.x+scroll.x-format.left));}
        intrinsic_widths[item]=width;return width;
    }
    int content_layout(Content& content,int width,bool move){
        if(content.natural_columns){std::vector<int> columns;
            for(const auto& row:content.rows){if(columns.size()<row.cells.size())columns.resize(row.cells.size(),px(content.minimum_column));
                for(std::size_t index=0;index<row.cells.size();++index){const auto& cell=row.cells[index];if(cell.text){SendMessageW(cell.window,EM_SETTARGETDEVICE,0,1);SendMessageW(cell.window,EM_REQUESTRESIZE,0,0);}
                    const auto preferred=cell.text?intrinsic_text_width(cell.window)+px(12):cell.width?px(cell.width):px(content.minimum_column);
                    columns[index]=std::max(columns[index],preferred);}}
            for(auto& row:content.rows)for(std::size_t index=0;index<row.cells.size();++index)row.cells[index].width=MulDiv(columns[index],96,static_cast<int>(GetDpiForWindow(host)));
        }
        int extent=width;for(const auto& row:content.rows){int natural=px(content.padding_x*2+row.indent+content.gap*std::max(0,static_cast<int>(row.cells.size())-1));for(const auto& cell:row.cells)natural+=px(cell.width);extent=std::max(extent,natural);}
        content.extent=extent;content.scroll=std::clamp(content.scroll,0,std::max(0,extent-width));
        const auto height=arrange_rows(content.rows,content.window,extent,move,content.padding_x,content.padding_y,content.gap,-content.scroll);
        if(move){SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,extent-1),static_cast<UINT>(std::max(1,width)),content.scroll};SetScrollInfo(content.window,SB_HORZ,&info,TRUE);ShowScrollBar(content.window,SB_HORZ,content.horizontal&&extent>width);reader_theme::refresh_scrollbars(content.window);}
        return height+(content.horizontal&&extent>width?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(content.window)):0);
    }
    void canvas(const JsonObject& config,std::string type,JsonObject state = {},std::function<void(JsonObject)> changed = {},std::string live_id = {}) {
        auto plot = std::make_unique<Canvas>(); plot->config = config; plot->type = std::move(type); plot->state = state; plot->changed = std::move(changed);plot->locale=locale;plot->layout_request=callbacks.expression_request;
        plot->window = CreateWindowExW(0,canvas_class,L"",WS_CHILD | WS_VISIBLE | (plot->changed ? WS_TABSTOP : 0),0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(next_id++)),GetModuleHandleW(nullptr),plot.get());
        if (!plot->window) throw std::runtime_error("Cannot create a mathematics canvas");
        if(!text(config,L"description").empty())SetWindowTextW(plot->window,wide(text(config,L"description")).c_str());
        plot->initialize(); plot->scale_text(text_scale);
        const auto spacer=make(L"STATIC","");plot->expand_button=button(tr("Ampliar representación","Expand representation","Agrandir la représentation"),[visual=plot.get()]{visual->expand();});reader_theme::icon_button(plot->expand_button);
        rows.push_back({{{spacer},{plot->expand_button,48}},48,0});
        rows.push_back({{{plot->window}},1});
        if (!live_id.empty()) live_visuals[live_id].push_back(plot.get()); canvases.push_back(std::move(plot));
    }
    void representation(const JsonObject& spec) {
        const auto config = spec.HasKey(L"config") ? object(spec,L"config") : spec;
        const auto type = text(spec,L"type","text");
        const auto previous_canvases=canvases.size();
        if (type == "text" || type == "math_formula") {
            const auto source = text(config,L"latex",text(config,L"text",text(config,L"formula",text(config,L"value"))));
            if(!source.empty()){const bool formula=type=="math_formula",inline_formula=formula&&text(config,L"display","block")=="inline";const auto first=rows.size();
                const auto item=label(source,{},RichTextRole::body,true,formula?(inline_formula?"inline":"block"):text(config,L"mathMode","auto"));
                if(inline_formula){CHARFORMAT2W style{};style.cbSize=sizeof(style);style.dwMask=CFM_SIZE;style.yHeight=240;SendMessageW(item,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&style));}
                rows.push_back({{{item,0,true,formula&&!inline_formula?0:6,formula&&!inline_formula?0:6}},0,0});if(formula&&!inline_formula)group_content(first,true,true,6,6);}
        }
        else if (type == "math_table") {
            const auto first=rows.size();const auto add = [&](const JsonArray& values,bool header=false) { Row row;row.height=0;row.gap_after=2;for (const auto& value : values) {
                const auto visible = value.ValueType() == JsonValueType::String ? winrt::to_string(value.GetString()) : stringify(value);
                const auto item=label(visible,{},header?RichTextRole::prompt:RichTextRole::body);reader_theme::apply(item,header?reader_theme::Surface::card_header:reader_theme::Surface::card);if(header){CHARFORMAT2W style{};style.cbSize=sizeof(style);style.dwMask=CFM_SIZE;style.yHeight=270;SendMessageW(item,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&style));}
                row.cells.push_back({item,0,true,6,6}); } if (!row.cells.empty()) rows.push_back(std::move(row)); };
            const auto headers = array(config,L"headers"); if (headers.Size()) add(headers,true);
            for (const auto& value : array(config,L"rows")) if (value.ValueType() == JsonValueType::Array) add(value.GetArray());
            if(rows.size()>first)group_content(first,true,true,1,1,2,70);
        } else if (type == "rich_answer_option") { for (const auto& value : array(config,L"content")) {if(value.ValueType()==JsonValueType::Object)representation(value.GetObject());else add_text(value.ValueType()==JsonValueType::String?winrt::to_string(value.GetString()):stringify(value));} }
        else if(type=="geometry_2d_diagram"&&config.HasKey(L"points")){auto graph=JsonObject::Parse(config.Stringify());if(!graph.HasKey(L"mode"))graph.SetNamedValue(L"mode",string_value("polygon"));canvas(graph,"math_coordinate_graph");}
        else {canvas(config,type);
            if(type=="math_coordinate_graph"||type=="coordinate_graph"||type=="math_number_line"){
                const auto target=canvases.back()->window;const auto key="representation/"+std::to_string(canvases.size());
                if(!text(config,L"label").empty())regions.push_back({key+"/label",text(config,L"label"),target});
                if(!text(config,L"description").empty())regions.push_back({key+"/description",text(config,L"description"),target});
            }
        }
        if(canvases.size()>previous_canvases&&(type=="geometry_2d_diagram"||type=="geometry_3d_diagram"||type=="venn_diagram")){
            const auto target=canvases.back()->window;const auto key="representation/"+std::to_string(canvases.size());
            const auto spoken=[&](const std::string& value,std::string suffix){if(!value.empty())regions.push_back({key+"/"+suffix,value,target});};
            if(type=="venn_diagram")for(const auto name:{L"universeLabel",L"leftLabel",L"leftOnly",L"intersection",L"rightLabel",L"rightOnly",L"outside"})spoken(text(config,name),utf8(name));
            else{spoken(text(config,L"label"),"label");const auto labels=array(config,L"labels");for(unsigned index=0;index<std::min(3u,labels.Size());++index)if(labels.GetAt(index).ValueType()==JsonValueType::String)spoken(winrt::to_string(labels.GetStringAt(index)),"label/"+std::to_string(index));}
        }
    }
    void option_content(const JsonObject& option) {
        const auto content=array(option,L"content");
        if(content.Size())for(const auto& value:content){if(value.ValueType()==JsonValueType::Object)representation(value.GetObject());else add_text(value.ValueType()==JsonValueType::String?winrt::to_string(value.GetString()):stringify(value));}
        else add_text(visible_label(option).empty()?text(option,L"id"):visible_label(option));
        add_text(text(option,L"explanation"));
    }
    HWND rich_card(const JsonObject& item,const std::string& key,int padding_x,bool target=false){
        const auto first=rows.size();option_content(item);HWND unassigned=nullptr;
        if(target){unassigned=label(tr("Sin asignar","Unassigned","Non attribué"));regions.pop_back();reader_theme::set_text_color(unassigned,reader_theme::palette(host).muted);rows.push_back({{{unassigned,0,true}},0,0});}
        if(rows.size()==first)rows.push_back({{},48});const auto card=group_content(first,false,false,padding_x,6);
        unsigned readable=0;visit_cells(contents.at(card)->rows,[&](const Cell& cell){for(auto& region:regions)if(region.target==cell.window)region.key=key+(readable++?"/content/"+std::to_string(readable-1):"");});
        if(unassigned)matching_empty_labels[card]=unassigned;bind_card_children(card);return card;
    }
    void choose(const std::string& id,const std::string& selected,bool multiple,bool card_tap=false) {
        if(!cards_enabled)return;
        auto current = response(id,"choice"); auto values = array(current,L"selected"); std::vector<std::string> ids;
        for (const auto& value : values) ids.push_back(winrt::to_string(value.GetString()));
        if (!multiple) {if(card_tap&&ids.size()==1&&ids.front()==selected)ids.clear();else ids={selected};}
        else if (const auto found = std::ranges::find(ids,selected); found == ids.end()) ids.push_back(selected); else ids.erase(found);
        JsonArray chosen; for (const auto& value : ids) chosen.Append(string_value(value)); current.SetNamedValue(L"selected",chosen); answers.SetNamedValue(winrt::to_hstring(id),current);
        for (const auto item : choice_buttons[id])
            SendMessageW(item,BM_SETCHECK,std::ranges::find(ids,choice_options.at(item)) != ids.end() ? BST_CHECKED : BST_UNCHECKED,0);
        for(const auto& [label,toggle]:choice_labels)if(std::ranges::find(choice_buttons[id],toggle)!=choice_buttons[id].end())InvalidateRect(label,nullptr,FALSE);
        InvalidateRect(host,nullptr,FALSE);
        change();
    }
    void restore_choice_checks()const{
        for(const auto& [id,buttons]:choice_buttons){const auto selected=array(object(answers,winrt::to_hstring(id).c_str()),L"selected");
            for(const auto item:buttons){bool checked=false;for(const auto& value:selected)if(value.ValueType()==JsonValueType::String&&value.GetString()==winrt::to_hstring(choice_options.at(item))){checked=true;break;}
                SendMessageW(item,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);}}
    }
    void card_border(HWND item,bool selected,int radius=10,HDC target=nullptr)const{
        const auto dc=target?target:GetDC(item);if(!dc)return;RECT area{};GetClientRect(item,&area);const auto colors=reader_theme::palette(item);
        const auto pen=CreatePen(PS_SOLID,std::max(1,px(1)),selected?colors.accent:colors.outline);const auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        RoundRect(dc,area.left,area.top,area.right,area.bottom,px(radius*2),px(radius*2));SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);if(!target)ReleaseDC(item,dc);
    }
    void paint_choice_cards(HDC dc)const{
        const auto colors=reader_theme::palette(host);
        for(const auto& [toggle,content]:choice_cards){
            RECT bounds{};bool present=false;
            for(const auto item:content)if(GetParent(item)==host&&(GetWindowLongPtrW(item,GWL_STYLE)&WS_VISIBLE)){
                RECT item_bounds{};GetWindowRect(item,&item_bounds);MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&item_bounds),2);
                if(present)UnionRect(&bounds,&bounds,&item_bounds);else{bounds=item_bounds;present=true;}
            }
            if(!present)continue;
            // Android's MathTouchCard surrounds the radio and its complete
            // rich body. Keep the stroke outside their existing child bounds,
            // rather than making a read-only label look like an input field.
            InflateRect(&bounds,px(3),px(3));
            const auto pen=CreatePen(PS_SOLID,std::max(1,px(1)),SendMessageW(toggle,BM_GETCHECK,0,0)==BST_CHECKED?colors.accent:colors.outline);
            const auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
            RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,px(20),px(20));
            SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
        }
    }
    void paint(HDC dc)const{
        RECT area{};GetClientRect(host,&area);FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::card));paint_choice_cards(dc);
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
            AppendMenuW(menu.handle,MF_STRING,1,wide(tr("Seleccionar…","Select…","Sélectionner…")).c_str());
            for (std::uint32_t index = 0; index < choices.Size(); ++index) {
                auto label = wide(visible_label(choices.GetObjectAt(index)));
                for (std::size_t position = 0; (position = label.find(L'&',position)) != label.npos; position += 2) label.insert(position,1,L'&');
                AppendMenuW(menu.handle,MF_STRING,index + 2,label.c_str());
            }
            const auto style = prepare_native_menu(menu.handle,host);
            RECT rect{}; GetWindowRect(item,&rect);
            const auto selected = TrackPopupMenuEx(menu.handle,TPM_RETURNCMD | TPM_LEFTALIGN,rect.left,rect.bottom,host,nullptr);
            if (selected <= 0 || static_cast<std::uint32_t>(selected) > choices.Size()+1) return;
            const auto option=selected==1?JsonObject{}:choices.GetObjectAt(selected-2);auto value = object(answers,winrt::to_hstring(id).c_str());
            auto pairs = object(value,matching ? L"pairs" : L"values"); pairs.SetNamedValue(winrt::to_hstring(field),string_value(text(option,L"id")));
            value.SetNamedValue(matching ? L"pairs" : L"values",pairs); answers.SetNamedValue(winrt::to_hstring(id),value);
            SetWindowTextW(item,wide(selected==1?tr("Seleccionar…","Select…","Sélectionner…"):visible_label(option)).c_str()); change();
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
        std::vector<winrt::hstring> displaced;
        for(const auto& pair:pairs)if(pair.Value().ValueType()==JsonValueType::String&&pair.Value().GetString()==winrt::to_hstring(target)&&pair.Key()!=winrt::to_hstring(source))displaced.push_back(pair.Key());
        for(const auto& key:displaced)pairs.Remove(key);
        pairs.SetNamedValue(winrt::to_hstring(source),string_value(target)); current.SetNamedValue(L"pairs",pairs);
        answers.SetNamedValue(winrt::to_hstring(id),current); selected_sources.erase(id);refresh_matching(id);change();
    }
    void unmatch(const std::string& id,const std::string& target) {
        auto current=response(id,"matching"),pairs=object(current,L"pairs");std::optional<winrt::hstring> source;
        for(const auto& pair:pairs)if(pair.Value().ValueType()==JsonValueType::String&&pair.Value().GetString()==winrt::to_hstring(target)){source=pair.Key();break;}
        if(!source)return;pairs.Remove(*source);current.SetNamedValue(L"pairs",pairs);answers.SetNamedValue(winrt::to_hstring(id),current);selected_sources.erase(id);refresh_matching(id);change();
    }
    void refresh_matching(const std::string& id) {
        const auto found=matching_layouts.find(id);if(found==matching_layouts.end())return;auto& layout=found->second;
        const auto pairs=object(object(answers,winrt::to_hstring(id).c_str()),L"pairs");auto position=layout.first;
        rows[position++]=layout.available_heading;
        for(const auto& source:layout.sources)if(!pairs.HasKey(winrt::to_hstring(source)))rows[position++]=layout.source_rows.at(source);
        rows[position++]=layout.target_heading;
        for(const auto& target:layout.targets){const auto& target_row=layout.target_rows.at(target);rows[position++]=target_row;
            bool assigned=false;for(const auto& source:layout.sources)assigned=assigned||text(pairs,winrt::to_hstring(source).c_str())==target;
            if(!target_row.cells.empty())if(const auto empty=matching_empty_labels.find(target_row.cells.front().window);empty!=matching_empty_labels.end())ShowWindow(empty->second,assigned?SW_HIDE:SW_SHOW);
            for(const auto& source:layout.sources)if(text(pairs,winrt::to_hstring(source).c_str())==target)rows[position++]=layout.source_rows.at(source);}
        arrange(current_width);if(!initializing&&callbacks.layout_changed)callbacks.layout_changed();
    }
    void save_graph_draft(const std::string& id,Canvas* plot){
        JsonArray coordinates;for(const auto& [x,y]:graph_fields[plot]){JsonObject point;point.SetNamedValue(L"x",string_value(edit_text(x)));point.SetNamedValue(L"y",string_value(edit_text(y)));coordinates.Append(point);}
        auto current=response(id,"graph"),draft=object(current,L"interactionDraft");draft.SetNamedValue(L"coords",coordinates);current.SetNamedValue(L"interactionDraft",draft);answers.SetNamedValue(winrt::to_hstring(id),current);
    }
    void response_controls(const JsonObject& spec) {
        const auto id = text(spec,L"id"),type = text(spec,L"type");
        std::string heading=text(spec,L"label");
        if(!spec.HasKey(L"label")) {
            if(type=="math_multiple_choice_single")heading=tr("Selecciona una respuesta.","Select an answer.","Sélectionnez une réponse.");
            else if(type=="math_multiple_choice_multi")heading=tr("Selecciona todas las respuestas correctas.","Select all correct answers.","Sélectionnez toutes les bonnes réponses.");
            else if(type=="math_ordering_drag")heading=tr("Arrastra una tarjeta directamente o toca las tarjetas en el orden deseado.","Drag a card directly or tap the cards in the desired order.","Faites glisser une carte ou touchez les cartes dans l’ordre souhaité.");
            else if(type=="math_matching_drag")heading=tr("Arrastra una ficha a su destino, o toca primero la ficha y después el destino. Toca un destino sin ficha seleccionada para vaciarlo.","Drag a tile to its target, or tap the tile and then the target. Tap a target with no tile selected to clear it.","Faites glisser une fiche vers sa destination, ou touchez la fiche puis la destination. Touchez une destination sans fiche sélectionnée pour la vider.");
        }
        add_text(heading,id+"/label");
        const bool text_unit=(type=="math_numeric_input"||type=="math_expression_input")&&!array(spec,L"fields").Size();
        if(text_unit&&!text(spec,L"unit").empty())add_text(tr("Unidad: ","Unit: ","Unité : ")+text(spec,L"unit"),id+"/unit");
        if (type == "math_multiple_choice_single" || type == "math_multiple_choice_multi") {
            const bool multiple = type == "math_multiple_choice_multi"; auto current = response(id,"choice"); const auto selected = array(current,L"selected");
            for (const auto& value : array(spec,L"options")) {
                const auto option = value.GetObject(); const auto option_id = text(option,L"id");
                // The answer model already owns checked state. Native automatic
                // radios also select on focus, which would turn a card click
                // into two answer changes and make keyboard focus answer it.
                const auto toggle = button("",[this,id,option_id,multiple] { choose(id,option_id,multiple); },multiple ? BS_CHECKBOX : BS_RADIOBUTTON);
                choice_options[toggle] = option_id; choice_buttons[id].push_back(toggle);
                SetWindowSubclass(toggle,choice_button_proc,16,reinterpret_cast<DWORD_PTR>(this));
                if (accessibility) accessibility->SetHwndPropStr(toggle,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,PROPID_ACC_NAME,wide(visible_label(option)).c_str());
                for (const auto& chosen : selected) if (chosen.GetString() == winrt::to_hstring(option_id)) SendMessageW(toggle,BM_SETCHECK,BST_CHECKED,0);
                const auto first=rows.size();option_content(option);
                if(rows.size()==first)rows.push_back({{},48});
                rows[first].cells.insert(rows[first].cells.begin(),{toggle,48});rows[first].height=std::max(48,rows[first].height);
                // The radio and the original rich column share the first row;
                // further formulas/tables/graphs keep that same content inset.
                for(auto at=first+1;at<rows.size();++at)rows[at].indent=56;
                unsigned readable=0;
                const std::vector<Row> body(rows.begin()+static_cast<std::ptrdiff_t>(first),rows.end());visit_cells(body,[&](const Cell& cell){
                    choice_cards[toggle].push_back(cell.window);
                    if(cell.text){
                        // MathWidgetUi.text uses the platform's normal line
                        // spacing; lesson prose's 1.25 multiplier does not
                        // belong inside mathematical answer cards.
                        PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);paragraph.dwMask=PFM_LINESPACING;paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;
                        SendMessageW(cell.window,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
                        for(auto& region:regions)if(region.target==cell.window){region.key=id+"/option/"+option_id+(readable?"/content/"+std::to_string(readable):"");break;}++readable;
                        choice_labels[cell.window]=toggle;SetWindowSubclass(cell.window,choice_proc,15,reinterpret_cast<DWORD_PTR>(this));
                    }
                });
            }
        } else if (type == "math_numeric_input" || type == "math_expression_input") {
            if (array(spec,L"fields").Size()) fields(spec,id);
            else { const auto current = response(id,"text");
                if(type=="math_expression_input"&&text(spec,L"inputMode","simple")=="structured")rows.push_back({{{expression_input(id,"",current,spec,object(spec,L"validator"))}},44});
                else rows.push_back({{{input(id,"",current,text(spec,L"hint"),spec,object(spec,L"validator"),type=="math_expression_input"?"ALGEBRA":"NUMERIC"),0,false,12,8}},48}); }
        } else if (type == "math_multi_input" || type == "math_structured_answer" || type == "math_equation_fill_blanks") fields(spec,id);
        else if (type == "math_matching_drag") {
            (void)response(id,"matching"); const auto sources = array(spec,L"sources"),targets = array(spec,L"targets");
            auto restored=response(id,"matching");const auto old_pairs=object(restored,L"pairs");JsonObject valid_pairs;std::vector<std::string> used_targets;
            for(const auto& source:sources){const auto key=text(source.GetObject(),L"id"),target=text(old_pairs,winrt::to_hstring(key).c_str());
                const bool known=std::ranges::any_of(targets,[&](const auto& value){return text(value.GetObject(),L"id")==target;});
                if(known&&std::ranges::find(used_targets,target)==used_targets.end()){valid_pairs.SetNamedValue(winrt::to_hstring(key),string_value(target));used_targets.push_back(target);}}
            restored.SetNamedValue(L"pairs",valid_pairs);answers.SetNamedValue(winrt::to_hstring(id),restored);
            auto& layout=matching_layouts[id];layout.first=rows.size();
            for (const auto source : {true,false}) {
                add_text(source?tr("Fichas disponibles","Available tiles","Fiches disponibles"):tr("Destinos","Targets","Destinations"),id+(source?"/tiles":"/targets"),RichTextRole::prompt);
                (source?layout.available_heading:layout.target_heading)=rows.back();
                for(const auto& value:(source?sources:targets)) {
                    const auto item=value.GetObject();const auto key=text(item,L"id");
                    const auto card=rich_card(item,id+(source?"/source/":"/target/")+key,12,!source);SetWindowTextW(card,wide(visible_label(item)).c_str());
                    match_cards[card] = {id,key,source}; SetWindowSubclass(card,match_proc,13,reinterpret_cast<DWORD_PTR>(this));
                    rows.back().height=48;
                    (source?layout.sources:layout.targets).push_back(key);(source?layout.source_rows:layout.target_rows)[key]=rows.back();
                }
            }
            refresh_matching(id);
        } else if (type == "math_ordering_drag") {
            const auto current = response(id,"order"); auto values = array(current,L"ids"); if (!values.Size()) values = array(spec,L"initialOrder");
            const auto items = array(spec,L"items"); std::map<std::string,JsonObject> cards;
            for (const auto& value : items) cards[text(value.GetObject(),L"id")] = value.GetObject();
            auto& ids = orders[id]; for (const auto& value : values) if (cards.contains(winrt::to_string(value.GetString()))) ids.push_back(winrt::to_string(value.GetString()));
            for (const auto& value : items) { const auto key = text(value.GetObject(),L"id"); if (std::ranges::find(ids,key) == ids.end()) ids.push_back(key); }
            JsonArray initial_ids; for (const auto& key : ids) initial_ids.Append(string_value(key)); current.SetNamedValue(L"ids",initial_ids); answers.SetNamedValue(winrt::to_hstring(id),current);
            next_tap_positions[id]=static_cast<std::size_t>(std::clamp(number(object(current,L"interactionDraft"),L"nextTapPosition"),0.,static_cast<double>(ids.size())));
            for (const auto& key : ids) {
                const auto& item=cards.at(key);const auto card=rich_card(item,id+"/item/"+key,10);SetWindowTextW(card,wide(visible_label(item)).c_str());order_cards[card]={id,key};SetWindowSubclass(card,card_proc,12,reinterpret_cast<DWORD_PTR>(this));
                rows.back().height=48;order_rows[id].push_back(rows.size()-1);
            }
        } else if(type=="math_segment_transport"){
            const auto config=object(spec,L"validator");auto current=response(id,"graph");
            if(!current.HasKey(L"points"))current.SetNamedValue(L"points",JsonArray::Parse(array(object(config,L"initial"),L"points").Stringify()));
            add_text(tr("Coloca cada punto libremente y arrástralo para corregirlo. El botón Punto recorre los nombres disponibles; no entrega sus posiciones. La herramienta conserva la longitud que tomas: muévela o gírala para comparar ambos extremos. El botón Longitud recorre las fuentes y los tramos que ya has construido.","Place each point freely and drag it to revise it. Point cycles through available names without supplying positions. The tool preserves the length you take: move or rotate it to compare both endpoints. Length cycles through sources and segments you have already constructed.","Placez librement chaque point puis déplacez-le pour le corriger. Point parcourt les noms disponibles sans fournir de positions. L’outil conserve la longueur prise : déplacez-le ou tournez-le pour comparer les deux extrémités. Longueur parcourt les sources et les segments déjà construits."),id+"/help");
            add_text(tr("Las figuras comparten una misma escala, sin medidas ni coordenadas visibles. La herramienta es ámbar. En Vista puedes desplazar el dibujo y usar la rueda o los botones de acercar y alejar, también más allá del encuadre inicial. La comprobación admite sólo la tolerancia operativa declarada para colocar los puntos.","All figures share one scale without visible measurements or coordinates. The tool is amber. View lets you pan and use the wheel or zoom buttons, including beyond the initial framing. Checking allows only the declared operational tolerance for point placement.","Les figures partagent une même échelle sans mesures ni coordonnées visibles. L’outil est ambré. Vue permet de déplacer le dessin et de zoomer avec la molette ou les boutons, au-delà du cadrage initial. La vérification n’admet que la tolérance opérationnelle déclarée pour placer les points."),id+"/legend");
            canvas(config,"segment_transport",current,[this,id](JsonObject value){answers.SetNamedValue(wide(id),value);change();},id);
            auto* plot=canvases.back().get();
            plot->draft_changed=[this,id](JsonObject value){answers.SetNamedValue(wide(id),value);if(!initializing&&callbacks.draft_changed)callbacks.draft_changed();};
            plot->segment_description=tr("Construcción libre de puntos y transporte de longitudes, sin ejes ni coordenadas","Free point construction and length transport, without axes or coordinates","Construction libre de points et transport de longueurs, sans axes ni coordonnées");SetWindowTextW(plot->window,wide(plot->segment_description).c_str());
            const auto stop_drag=[plot]{plot->segment_dragging=false;plot->segment_point.reset();if(GetCapture()==plot->window)ReleaseCapture();};
            std::vector<HWND> controls;std::vector<std::pair<HWND,std::string>> mode_controls;
            const std::vector<std::pair<std::string,std::string>> modes{{"place",tr("Colocar puntos","Place points","Placer les points")},{"move_tool",tr("Mover herramienta","Move tool","Déplacer l’outil")},{"rotate_tool",tr("Girar herramienta","Rotate tool","Tourner l’outil")},{"view",tr("Vista","View","Vue")}};
            for(const auto& [mode,caption]:modes){const auto control=button(caption,[plot,stop_drag,mode]{if(plot->construction_locked&&mode=="place")return;stop_drag();auto draft=object(plot->state,L"interactionDraft");draft.SetNamedValue(L"mode",string_value(mode));plot->state.SetNamedValue(L"interactionDraft",draft);plot->segment_changed(false);},BS_PUSHBUTTON|BS_MULTILINE);controls.push_back(control);mode_controls.emplace_back(control,mode);}
            const auto point_button=button(tr("Punto","Point","Point"),[plot,stop_drag]{
                if(plot->construction_locked)return;stop_drag();auto labels=array(plot->drawing,L"pointLabels");if(!labels.Size())labels=array(plot->config,L"pointLabels");if(!labels.Size())return;
                auto draft=object(plot->state,L"interactionDraft");const auto available=std::min(labels.Size(),array(plot->state,L"points").Size()+1);const auto next=(static_cast<unsigned>(std::max(0.,number(draft,L"activePoint")))+1)%std::max(1u,available);draft.SetNamedValue(L"activePoint",JsonValue::CreateNumberValue(next));draft.SetNamedValue(L"mode",string_value("place"));plot->state.SetNamedValue(L"interactionDraft",draft);plot->segment_changed(false);
            },BS_PUSHBUTTON|BS_MULTILINE);controls.push_back(point_button);
            const auto measurement_button=button(tr("Longitud","Length","Longueur"),[plot,stop_drag]{
                stop_drag();const auto measurements=array(plot->drawing,L"measurements");if(!measurements.Size())return;const auto selected=text(object(object(plot->state,L"interactionDraft"),L"tool"),L"measurementId");unsigned next=0;
                for(unsigned index=0;index<measurements.Size();++index)if(text(measurements.GetObjectAt(index),L"id")==selected){next=(index+1)%measurements.Size();break;}
                JsonObject gesture;gesture.SetNamedValue(L"type",string_value("measure"));gesture.SetNamedValue(L"id",measurements.GetObjectAt(next).GetNamedValue(L"id"));plot->segment_gesture(gesture);
            },BS_PUSHBUTTON|BS_MULTILINE);controls.push_back(measurement_button);
            const auto reset_points=button(tr("Restablecer construcción","Restore construction","Rétablir la construction"),[plot,stop_drag]{
                if(plot->construction_locked)return;stop_drag();const auto previous_state=plot->state;const auto previous_width=plot->drawing_width,previous_height=plot->drawing_height;const auto previous_points=array(plot->state,L"points").Stringify();plot->state=JsonObject::Parse(plot->state.Stringify());plot->state.SetNamedValue(L"points",JsonArray::Parse(array(object(plot->config,L"initial"),L"points").Stringify()));auto draft=object(plot->state,L"interactionDraft");draft.SetNamedValue(L"activePoint",JsonValue::CreateNumberValue(0));plot->state.SetNamedValue(L"interactionDraft",draft);plot->drawing_width=plot->drawing_height=0;
                if(!plot->prepare_drawing()){plot->state=previous_state;plot->drawing_width=previous_width;plot->drawing_height=previous_height;if(plot->refresh_segment_controls)plot->refresh_segment_controls();return;}
                plot->segment_changed(previous_points!=array(plot->state,L"points").Stringify());
            },BS_PUSHBUTTON|BS_MULTILINE);controls.push_back(reset_points);
            const auto reset_tool=button(tr("Restablecer herramienta","Restore tool","Rétablir l’outil"),[plot,stop_drag]{stop_drag();JsonObject gesture;gesture.SetNamedValue(L"type",string_value("reset_tool"));plot->segment_gesture(gesture);},BS_PUSHBUTTON|BS_MULTILINE);controls.push_back(reset_tool);
            const auto reset_view=button(tr("Restablecer vista","Restore view","Rétablir la vue"),[plot,stop_drag]{stop_drag();JsonObject gesture;gesture.SetNamedValue(L"type",string_value("reset_view"));plot->segment_gesture(gesture);},BS_PUSHBUTTON|BS_MULTILINE);controls.push_back(reset_view);
            for(const auto& [delta,caption]:std::vector<std::pair<int,std::string>>{{1,tr("Acercar","Zoom in","Agrandir")},{-1,tr("Alejar","Zoom out","Réduire")}}){const auto control=button(caption,[plot,delta]{RECT area{};GetClientRect(plot->window,&area);plot->segment_zoom({area.right/2,area.bottom/2},delta);},BS_PUSHBUTTON|BS_MULTILINE);controls.push_back(control);}
            plot->auxiliary_controls=controls;
            plot->auxiliary_dispatch=[this](UINT message,WPARAM wp,LPARAM lp){return SendMessageW(host,message,wp,lp);};
            plot->segment_controls_layout_changed=[this]{if(callbacks.layout_changed)callbacks.layout_changed();};
            plot->refresh_segment_controls=[plot,mode_controls,point_button,measurement_button,reset_points]{
                const auto caption=[plot](HWND control,const std::string& value){std::wstring before(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');before.resize(GetWindowTextW(control,before.data(),static_cast<int>(before.size())));const auto after=wide(value);if(before!=after){SetWindowTextW(control,after.c_str());plot->segment_captions_dirty=true;}};
                const auto draft=object(plot->state,L"interactionDraft");for(const auto& [control,mode]:mode_controls){reader_theme::primary_button(control,plot->segment_mode()==mode);EnableWindow(control,mode!="place"||!plot->construction_locked);}
                auto labels=array(plot->drawing,L"pointLabels");if(!labels.Size())labels=array(plot->config,L"pointLabels");const auto selected=static_cast<unsigned>(std::max(0.,number(draft,L"activePoint")));std::string label;
                if(selected<labels.Size()&&labels.GetAt(selected).ValueType()==JsonValueType::String)label=winrt::to_string(labels.GetStringAt(selected));
                caption(point_button,plot->tr("Punto","Point","Point")+(label.empty()?"":": "+label));EnableWindow(point_button,!plot->construction_locked);EnableWindow(reset_points,!plot->construction_locked);
                const auto measurement=text(object(draft,L"tool"),L"measurementId");std::string measured;const auto measurements=array(plot->drawing,L"measurements");
                for(const auto& value:measurements){const auto item=value.GetObject();if(text(item,L"id")==measurement){measured=text(item,L"label");break;}}
                caption(measurement_button,plot->tr("Longitud","Length","Longueur")+(measured.empty()?"":": "+measured));EnableWindow(measurement_button,measurements.Size()!=0);
            };
            // Keep the very same controls with their canvas in the expanded
            // panel. No second response, view, or construction is created.
            for(std::size_t first=0;first<controls.size();first+=3){Row row;row.height=48;row.gap_after=0;for(std::size_t index=first;index<std::min(first+3,controls.size());++index)row.cells.push_back({controls[index]});rows.insert(rows.end()-1,std::move(row));}
            plot->refresh_segment_controls();
        } else if(type=="math_rigid_angle_copy"){
            const auto config=object(spec,L"validator");auto current=response(id,"rigid_angle");
            if(!current.HasKey(L"x")){current=JsonObject::Parse(object(config,L"initial").Stringify());current.SetNamedValue(L"type",string_value("rigid_angle"));answers.SetNamedValue(winrt::to_hstring(id),current);}
            add_text(tr("Mueve la copia completa y gírala alrededor de su vértice. El modo Vista y el doble clic desplazan o amplían la vista. La colocación admite una pequeña tolerancia operativa; la copia conserva su forma exacta.","Move the entire copy, then rotate it about its vertex. View mode and double-click move or enlarge the view. Placement allows a small operational tolerance; the copy keeps its exact shape.","Déplacez toute la copie puis tournez-la autour de son sommet. Le mode Vue et le double-clic déplacent ou agrandissent la vue. Le placement admet une petite tolérance opérationnelle ; la copie conserve exactement sa forme."),id+"/help");
            add_text(tr("Referencia fija: color del curso. Copia móvil: ámbar. La marca curva identifica cada región elegida.","Fixed reference: course colour. Movable copy: amber. The curved mark identifies each chosen region.","Référence fixe : couleur du cours. Copie mobile : ambre. La marque courbe identifie chaque région choisie."),id+"/legend");
            canvas(config,"rigid_angle",current,[this,id](JsonObject value){answers.SetNamedValue(winrt::to_hstring(id),value);change();},id);auto* plot=canvases.back().get();
            SetWindowTextW(plot->window,wide(tr("Referencia fija y copia rígida móvil de un ángulo, sin ejes ni coordenadas","Fixed reference and movable rigid angle copy, without axes or coordinates","Référence fixe et copie rigide mobile d’un angle, sans axes ni coordonnées")).c_str());
            // The original mode/reset controls are part of the expandable
            // panel, not detached clones with a second answer/history.
            Row modes; modes.height=48;modes.gap_after=0;
            const std::vector<std::pair<std::string,std::string>> definitions{{"move",tr("Mover copia","Move copy","Déplacer")},{"rotate",tr("Girar copia","Rotate copy","Tourner")},{"view",tr("Vista","View","Vue")}};
            for(const auto& [mode,caption]:definitions){const auto control=button(caption,[plot,mode]{plot->rigid_mode=mode;plot->rigid_dragging=false;if(GetCapture()==plot->window)ReleaseCapture();});reader_theme::primary_button(control);modes.cells.push_back({control});plot->auxiliary_controls.push_back(control);}
            const auto reset=button(tr("Restablecer copia inicial","Restore initial copy","Rétablir la copie initiale"),[this,plot,id,config]{auto value=JsonObject::Parse(object(config,L"initial").Stringify());value.SetNamedValue(L"type",string_value("rigid_angle"));answers.SetNamedValue(winrt::to_hstring(id),value);plot->state=value;plot->drawing_width=plot->drawing_height=0;plot->prepare_drawing();change();});plot->auxiliary_controls.push_back(reset);
            reader_theme::primary_button(reset);
            plot->auxiliary_dispatch=[this](UINT message,WPARAM wp,LPARAM lp){return SendMessageW(host,message,wp,lp);};
            // canvas() appended its expand toolbar and canvas. Insert modes
            // between those same rows so every action stays above its figure.
            rows.insert(rows.end()-1,std::move(modes));rows.insert(rows.end()-1,Row{{{reset}},48,0});
        } else if (type == "math_number_line") {
            auto current = response(id,"number_line"); if (!current.HasKey(L"value")&&!current.HasKey(L"interactionDraft")) {
                current.SetNamedValue(L"value",JsonValue::CreateNumberValue(snapped(number(spec,L"value"),number(spec,L"step",1),number(spec,L"min",-10),number(spec,L"max",10)))); current.SetNamedValue(L"closed",JsonValue::CreateBooleanValue(flag(spec,L"closed",true)));
                current.SetNamedValue(L"direction",string_value(text(spec,L"direction","right"))); answers.SetNamedValue(winrt::to_hstring(id),current);
            }
            auto config=JsonObject::Parse(spec.Stringify());if(!config.HasKey(L"min"))config.SetNamedValue(L"min",JsonValue::CreateNumberValue(-10));if(!config.HasKey(L"max"))config.SetNamedValue(L"max",JsonValue::CreateNumberValue(10));
            auto displayed=JsonObject::Parse(current.Stringify());if(!displayed.HasKey(L"value")&&object(current,L"interactionDraft").HasKey(L"lastValidValue"))displayed.SetNamedValue(L"value",object(current,L"interactionDraft").GetNamedValue(L"lastValidValue"));
            canvas(config,"number_line",displayed,[this,id](JsonObject value) { value.Remove(L"interactionDraft");answers.SetNamedValue(winrt::to_hstring(id),value); change(); },id);auto* plot=canvases.back().get();
            add_text(tr("Posición","Position","Position"),id+"/position");
            const auto position=make(MSFTEDIT_CLASS,"",WS_TABSTOP|ES_AUTOHSCROLL);reader_theme::apply(position,reader_theme::Surface::field);
            set_edit_text(position,text(object(current,L"interactionDraft"),L"position",decimal(number(current,L"value"))));input_ink(position,RGB(255,255,255));keyboard_context(position,spec,object(spec,L"validator"),false,"NUMERIC");
            SendMessageW(position,EM_SETEVENTMASK,0,ENM_CHANGE);SetWindowSubclass(position,field_proc,11,reinterpret_cast<DWORD_PTR>(this));rows.push_back({{{position}},48});
            const auto update_position=[this,id,position,config](int notification){if(notification!=EN_CHANGE||initializing)return;auto value=response(id,"number_line");const auto input=edit_text(position);JsonObject draft;draft.SetNamedValue(L"position",string_value(input));
                if(value.HasKey(L"value"))draft.SetNamedValue(L"lastValidValue",value.GetNamedValue(L"value"));else if(object(value,L"interactionDraft").HasKey(L"lastValidValue"))draft.SetNamedValue(L"lastValidValue",object(value,L"interactionDraft").GetNamedValue(L"lastValidValue"));value.SetNamedValue(L"interactionDraft",draft);
                if(const auto parsed=coordinate(input))value.SetNamedValue(L"value",JsonValue::CreateNumberValue(snapped(*parsed,number(config,L"step",1),number(config,L"min",-10),number(config,L"max",10))));else value.Remove(L"value");
                answers.SetNamedValue(winrt::to_hstring(id),value);change();};commands[GetDlgCtrlID(position)]=update_position;
            const auto closed=button(tr("Punto cerrado (incluye el extremo)","Closed point (includes the endpoint)","Point fermé (extrémité incluse)"),[this,id]{auto value=response(id,"number_line");value.SetNamedValue(L"closed",JsonValue::CreateBooleanValue(!flag(value,L"closed")));answers.SetNamedValue(winrt::to_hstring(id),value);change();},BS_AUTOCHECKBOX);
            SendMessageW(closed,BM_SETCHECK,flag(current,L"closed")?BST_CHECKED:BST_UNCHECKED,0);rows.push_back({{{closed}},48});
            add_text(tr("Dirección del intervalo","Interval direction","Direction de l’intervalle"),id+"/interval-direction");
            const auto direction=button(tr(text(current,L"direction")=="left"?"Izquierda":"Derecha",text(current,L"direction")=="left"?"Left":"Right",text(current,L"direction")=="left"?"Gauche":"Droite"),[]{});
            commands[GetDlgCtrlID(direction)]=[this,id,direction](int notification){if(notification!=BN_CLICKED)return;const auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,wide(tr("Izquierda","Left","Gauche")).c_str());AppendMenuW(menu,MF_STRING,2,wide(tr("Derecha","Right","Droite")).c_str());const auto style=prepare_native_menu(menu,host);RECT area{};GetWindowRect(direction,&area);const auto selected=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_LEFTALIGN,area.left,area.bottom,host,nullptr);DestroyMenu(menu);if(selected!=1&&selected!=2)return;auto value=response(id,"number_line");value.SetNamedValue(L"direction",string_value(selected==1?"left":"right"));answers.SetNamedValue(winrt::to_hstring(id),value);SetWindowTextW(direction,wide(selected==1?tr("Izquierda","Left","Gauche"):tr("Derecha","Right","Droite")).c_str());change();};rows.push_back({{{direction}},48});
            plot->sync_fields=[this,id,position,closed,direction]{const auto value=object(answers,winrt::to_hstring(id).c_str());const bool previous=initializing;initializing=true;set_edit_text(position,decimal(number(value,L"value")));initializing=previous;SendMessageW(closed,BM_SETCHECK,flag(value,L"closed")?BST_CHECKED:BST_UNCHECKED,0);SetWindowTextW(direction,wide(text(value,L"direction")=="left"?tr("Izquierda","Left","Gauche"):tr("Derecha","Right","Droite")).c_str());};
        } else if (type == "math_slider_interaction") {
            auto current = response(id,"sliders"),values = object(current,L"values");
            for (const auto& value : array(spec,L"parameters")) {
                const auto parameter = value.GetObject(); const auto key = text(parameter,L"id");
                const auto low=number(parameter,L"min"),high=number(parameter,L"max",10),step=number(parameter,L"step",1);
                const auto starting_value=number(values,winrt::to_hstring(key).c_str(),number(parameter,L"value",low));
                values.SetNamedValue(winrt::to_hstring(key),JsonValue::CreateNumberValue(snapped(starting_value,step,low,high)));
            }
            current.SetNamedValue(L"values",values); answers.SetNamedValue(winrt::to_hstring(id),current);
            auto preview=JsonObject::Parse(object(spec,L"visual").Stringify());preview.SetNamedValue(L"parameters",array(spec,L"parameters"));
            canvas(preview,"sliders_visual",current,{},id);
            for (const auto& value : array(spec,L"parameters")) {
                const auto parameter = value.GetObject(); const auto key = text(parameter,L"id");
                const auto name=visible_label(parameter);const auto value_label=label(name+": "+decimal(number(values,winrt::to_hstring(key).c_str())),id+"/parameter/"+key);
                rows.push_back({{{value_label,0,true}},26});const auto slider = make(TRACKBAR_CLASSW,"",WS_TABSTOP | TBS_AUTOTICKS);
                const auto low = number(parameter,L"min"), high = number(parameter,L"max",10), step = number(parameter,L"step",1);
                const auto steps = static_cast<int>(std::clamp(std::ceil((high - low) / step),1.0,10000.0));
                SendMessageW(slider,TBM_SETRANGEMAX,FALSE,steps); SendMessageW(slider,TBM_SETPOS,TRUE,static_cast<LPARAM>(std::lround((number(values,winrt::to_hstring(key).c_str(),low) - low) / step)));
                commands[GetDlgCtrlID(slider)] = [this,id,key,name,slider,value_label,low,high,step](int) {
                    const auto number_value = snapped(low + static_cast<double>(SendMessageW(slider,TBM_GETPOS,0,0)) * step,step,low,high);
                    auto current_answer = response(id,"sliders"),current_values = object(current_answer,L"values"); current_values.SetNamedValue(winrt::to_hstring(key),JsonValue::CreateNumberValue(number_value));
                    current_answer.SetNamedValue(L"values",current_values); answers.SetNamedValue(winrt::to_hstring(id),current_answer);
                    const auto displayed = name+": "+decimal(number_value); set_rich_text(value_label,displayed,{},false,RichTextRole::body,locale); for (auto& region : regions) if (region.target == value_label) region.source_text = displayed; change();
                }; rows.push_back({{{slider}},48});
            }
        } else if (type == "math_coordinate_graph_builder" || type == "math_geometry_builder") {
            auto current = response(id,"graph"); if (!current.HasKey(L"points")) current.SetNamedValue(L"points",array(spec,L"points"));
            answers.SetNamedValue(winrt::to_hstring(id),current);
            auto config=JsonObject::Parse(spec.Stringify());if(type=="math_geometry_builder"&&!config.HasKey(L"mode"))config.SetNamedValue(L"mode",string_value("polygon"));
            canvas(config,"graph",current,[this,id](JsonObject value) {
                const auto previous=object(answers,winrt::to_hstring(id).c_str());if(previous.HasKey(L"interactionDraft"))value.SetNamedValue(L"interactionDraft",previous.GetNamedValue(L"interactionDraft"));
                answers.SetNamedValue(winrt::to_hstring(id),value);
            });
            auto* plot = canvases.back().get();
            JsonArray points;for(const auto& point:plot->points){JsonObject value;value.SetNamedValue(L"x",JsonValue::CreateNumberValue(point.x));value.SetNamedValue(L"y",JsonValue::CreateNumberValue(point.y));points.Append(value);}current.SetNamedValue(L"points",points);answers.SetNamedValue(winrt::to_hstring(id),current);
            plot->sync_fields = [this,id,plot] {
                const auto previous = initializing; initializing = true;
                for (std::size_t index = 0; index < graph_fields[plot].size(); ++index) {
                    const auto [x,y]=graph_fields[plot][index];
                    // A graph move must not erase another incomplete coordinate.
                    if(coordinate(edit_text(x)))set_edit_text(x,decimal(plot->points[index].x));
                    if(coordinate(edit_text(y)))set_edit_text(y,decimal(plot->points[index].y));
                }
                initializing = previous;save_graph_draft(id,plot);change();
            };
            add_text(tr("Arrastra los puntos o edita sus coordenadas.","Drag the points or edit their coordinates.","Faites glisser les points ou modifiez leurs coordonnées."),id + "/coordinates-instruction");
            for (std::size_t index = 0; index < plot->points.size(); ++index) {
                const auto editable = plot->points[index].draggable;
                const auto restored_coordinates=array(object(current,L"interactionDraft"),L"coords");const auto draft=index<restored_coordinates.Size()?restored_coordinates.GetObjectAt(static_cast<unsigned>(index)):JsonObject{};
                const auto x = make(MSFTEDIT_CLASS,text(draft,L"x",decimal(plot->points[index].x)),WS_TABSTOP | ES_AUTOHSCROLL);
                const auto y = make(MSFTEDIT_CLASS,text(draft,L"y",decimal(plot->points[index].y)),WS_TABSTOP | ES_AUTOHSCROLL);
                graph_fields[plot].push_back({x,y});
                for (const auto item : {x,y}) { reader_theme::apply(item,reader_theme::Surface::field);input_ink(item,RGB(255,255,255));keyboard_context(item,{}, {},false,"NUMERIC");SendMessageW(item,EM_SETEVENTMASK,0,ENM_CHANGE); SetWindowSubclass(item,field_proc,11,reinterpret_cast<DWORD_PTR>(this));EnableWindow(item,editable); }
                const auto sync = [this,id,plot,index,x,y](int notification) {
                    if (notification != EN_CHANGE || initializing || !cards_enabled || !plot->points[index].draggable) return;
                    save_graph_draft(id,plot);const auto xx=coordinate(edit_text(x)),yy=coordinate(edit_text(y));
                    if(xx&&yy){
                        auto& point = plot->points[index]; point.x = graph_coordinate(*xx,number(plot->config,L"snap",1),plot->xmin,plot->xmax);
                        point.y = graph_coordinate(*yy,number(plot->config,L"snap",1),plot->ymin,plot->ymax);
                        auto answer_value = response(id,"graph"); JsonArray points;
                        for (const auto& item : plot->points) { JsonObject coordinates; coordinates.SetNamedValue(L"x",JsonValue::CreateNumberValue(item.x)); coordinates.SetNamedValue(L"y",JsonValue::CreateNumberValue(item.y)); points.Append(coordinates); }
                        answer_value.SetNamedValue(L"points",points); answers.SetNamedValue(winrt::to_hstring(id),answer_value); InvalidateRect(plot->window,nullptr,FALSE);
                    }
                    change();
                };
                commands[GetDlgCtrlID(x)] = sync; commands[GetDlgCtrlID(y)] = sync;
                const auto prefix=plot->points.size()>1?(index<26?std::string(1,static_cast<char>('A'+index)):"P"+std::to_string(index+1))+": ":std::string{};
                rows.push_back({{{label(prefix+"x =",id + "/x/" + std::to_string(index)),70,true},{x,0},
                    {label(" y =",id + "/y/" + std::to_string(index)),40,true},{y,0}},48});
            }
        } else throw std::invalid_argument("Native math control is not yet implemented: " + type);
        add_text(text(spec,L"instruction"),id + "/instruction-authored");
    }
    void fields(const JsonObject& spec,const std::string& id) {
        auto current = response(id,"fields"),values = object(current,L"values"); current.SetNamedValue(L"values",values); answers.SetNamedValue(winrt::to_hstring(id),current);
        std::map<std::string,JsonObject> definitions; for (const auto& value : array(spec,L"fields")) definitions[text(value.GetObject(),L"id")] = value.GetObject();
        const bool inline_fields=spec.HasKey(L"segments");std::set<std::string> used;
        const auto field = [&](const std::string& key) -> Cell {
            if (!definitions.contains(key)) throw std::invalid_argument("A displayed math field has no definition");
            if(!used.insert(key).second)throw std::invalid_argument("A mathematical template repeats the same field");
            const auto config = definitions.at(key); const auto kind = text(config,L"type","numeric");
            const auto first=rows.size();const auto caption=text(config,L"label");
            if(!inline_fields)add_text(caption,id+"/field/"+key);
            auto keyboard_config=JsonObject::Parse(config.Stringify());for(const auto property:{L"keyboard",L"keyboardSymbols"})if(!keyboard_config.HasKey(property)&&spec.HasKey(property))keyboard_config.SetNamedValue(property,spec.GetNamedValue(property));
            const auto validator=object(object(object(spec,L"validator"),L"fields"),winrt::to_hstring(key).c_str());
            const bool structured=kind=="expression"&&text(config,L"inputMode","simple")=="structured";
            const auto control=kind=="fixed"?label(text(config,L"text"),id+"/fixed/"+key):kind=="dropdown"?dropdown(id,key,array(config,L"options"),current)
                :structured?expression_input(id,key,current,keyboard_config,validator)
                :input(id,key,current,text(config,L"hint",inline_fields?caption:std::string{}),keyboard_config,validator,kind=="expression"?"ALGEBRA":"NUMERIC");
            if(accessibility&&!caption.empty())accessibility->SetHwndPropStr(control,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,PROPID_ACC_NAME,wide(caption).c_str());
            Row control_row;control_row.height=kind=="fixed"?0:48;control_row.gap_after=0;
            control_row.cells.push_back({control,inline_fields?(kind=="dropdown"?150:100):0,kind=="fixed",kind=="fixed"||structured||kind=="dropdown"?0:12,kind=="fixed"||structured||kind=="dropdown"?0:8});
            const auto unit=text(config,L"unit");int inline_width=kind=="dropdown"?150:100;
            if(inline_fields&&!unit.empty()){const auto unit_label=label(unit,id+"/field/"+key+"/unit");const auto unit_width=std::max(1,MulDiv(intrinsic_text_width(unit_label),96,static_cast<int>(GetDpiForWindow(host))));control_row.cells.push_back({unit_label,unit_width,true});inline_width+=unit_width;}
            rows.push_back(std::move(control_row));if(!inline_fields)add_text(unit,id+"/field/"+key+"/unit");
            const auto column=group_content(first,false,false,0,0,inline_fields?0:8);
            rows.pop_back();
            // Android preserves the semantic hole on its field column without
            // narrating the editable value, its hint or a selected dropdown.
            if(inline_fields&&kind!="fixed"&&!caption.empty())regions.push_back({id+"/field/"+key,caption,column});
            return {column,inline_fields?inline_width:0};
        };
        const auto segments = array(spec,L"segments"); const auto table = array(spec,L"rows");
        if (inline_fields) {
            const auto first=rows.size();
            Row row; for (const auto& value : segments) { const auto segment = value.GetObject();
                if (segment.HasKey(L"field")) row.cells.push_back(field(text(segment,L"field")));
                else row.cells.push_back({label(text(segment,L"text")),0,true}); }
            if (!row.cells.empty()){rows.push_back(std::move(row));group_content(first,true,true,0,0,8);}
        } else if (text(spec,L"layout")=="table"&&table.Size()) {
            const auto first=rows.size();
            const auto headers = array(spec,L"headers"); if (headers.Size()) { Row row; for (const auto& value : headers){const auto heading=label(winrt::to_string(value.GetString()),{},RichTextRole::prompt);CHARFORMAT2W style{};style.cbSize=sizeof(style);style.dwMask=CFM_SIZE;style.yHeight=270;SendMessageW(heading,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&style));row.cells.push_back({heading,150,true});} rows.push_back(std::move(row)); }
            for (const auto& row_value : table) { Row row; for (const auto& value : row_value.GetArray())
                if (value.ValueType() == JsonValueType::Object){auto cell=field(text(value.GetObject(),L"field"));cell.width=150;row.cells.push_back(cell);}
                else row.cells.push_back({label(value.ValueType() == JsonValueType::String ? winrt::to_string(value.GetString()) : stringify(value)),150,true});
                if (!row.cells.empty()) rows.push_back(std::move(row)); }
            group_content(first,true,false,0,0,0);
        } else for (const auto& value : array(spec,L"fields")) rows.push_back({{field(text(value.GetObject(),L"id"))},0});
        // A partial inline/table template does not hide the remaining authored
        // fields. Preserve source order and their own labels and units.
        for(const auto& value:array(spec,L"fields")){const auto config=value.GetObject();const auto key=text(config,L"id");if(used.contains(key))continue;
            rows.push_back({{field(key)},0});}
    }
    void build() {
        // Match MathPuzzleRenderer's card actions. The lesson owns reset and
        // fullscreen, so feedback, progress and the existing reader stay joined.
        const auto spacer=make(L"STATIC","");
        expand_button=button(tr("Pantalla completa","Full screen","Plein écran"),[this]{if(callbacks.fullscreen_requested)callbacks.fullscreen_requested();});
        reset_button=button(tr("Reiniciar","Restart","Recommencer"),[this]{if(callbacks.reset_requested)callbacks.reset_requested();});
        reader_theme::icon_button(expand_button);reader_theme::icon_button(reset_button);
        rows.push_back({{{spacer},{expand_button,48},{reset_button,48}},48});
        // Weight is selected BEFORE native math is built and aligned: TOM
        // may encode bold numerals with supplementary mathematical digits.
        add_text(text(block,L"prompt",text(block,L"question")),"prompt",RichTextRole::prompt);
        for (const auto& value : array(block,L"representations")) representation(value.GetObject());
        for (const auto& value : array(block,L"tools")) representation(value.GetObject());
        for (const auto& value : array(block,L"responses")) response_controls(value.GetObject());
        initial = JsonObject::Parse(answers.Stringify()); initializing = false;
    }
    int arrange_rows(const std::vector<Row>& content_rows,HWND container,int width,bool move,int padding_x=8,int padding_y=8,int spacing=8,int offset_x=0) {
        width=std::max(1,width);const auto gap=px(spacing);int y=px(padding_y);
        const auto format_coordinate=[](HWND item,int pixel){int numerator=0,denominator=0;SendMessageW(item,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            return numerator>0&&denominator>0?MulDiv(pixel,denominator,numerator):pixel;};
        for (const auto& row : content_rows) {
            if(!row.cells.empty()&&std::ranges::all_of(row.cells,[](const auto& cell){return !(GetWindowLongPtrW(cell.window,GWL_STYLE)&WS_VISIBLE);}))continue;
            int fixed = 0,flex = 0; for (const auto& cell : row.cells) if (cell.width) fixed += px(cell.width); else ++flex;
            const auto flexible = std::max(px(30),(width-px(padding_x*2)-px(row.indent)-fixed-gap*(static_cast<int>(row.cells.size())-1))/std::max(1,flex));
            int height=px(row.height),x=px(padding_x+row.indent)+offset_x;
            for(const auto& cell:row.cells)if(const auto found=contents.find(cell.window);found!=contents.end())height=std::max(height,content_layout(*found->second,cell.width?px(cell.width):flexible,move));
            for(const auto& cell:row.cells)if(std::ranges::any_of(expression_editors,[&](const auto& value){return value->window==cell.window;}))height=std::max(height,math_expression_editor_height(cell.window));
            for(const auto& cell:row.cells)if(const auto canvas=std::ranges::find_if(canvases,[&](const auto& value){return value->window==cell.window;});canvas!=canvases.end())
                height=std::max(height,std::max(px(48),static_cast<int>(std::lround((cell.width?px(cell.width):flexible)/(*canvas)->aspect_ratio()))-((*canvas)->type=="rigid_angle"?px(96):0)));
            for (const auto& cell : row.cells) if (cell.text || (GetWindowLongPtrW(cell.window,GWL_STYLE) & BS_MULTILINE) || editors.contains(cell.window)) {
                const auto cell_width = cell.width ? px(cell.width) : flexible;
                if (cell.text) {
                    // With a 1px measuring viewport RichEdit can retain the
                    // previous width's wrapped height until the real placement.
                    // This viewport is provisional; the final row still uses
                    // its native requested height plus the existing insets.
                    const auto measuring_height=std::max(px(48),height);
                    SetWindowPos(cell.window,nullptr,0,0,cell_width,measuring_height,SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOCOPYBITS);
                    if(choice_labels.contains(cell.window)||cell.inset_x||cell.inset_y){RECT content{px(cell.inset_x),0,cell_width-px(cell.inset_x),measuring_height};SendMessageW(cell.window,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));}
                    SendMessageW(cell.window,EM_REQUESTRESIZE,0,0);
                    if (const auto found = requested_heights.find(cell.window); found != requested_heights.end()) { height = std::max(height,found->second + px(cell.inset_y?cell.inset_y*2:4)); continue; }
                }
                const auto dc = GetDC(cell.window); const auto old = SelectObject(dc,font);
                const auto count = GetWindowTextLengthW(cell.window); std::wstring value(static_cast<std::size_t>(count) + 1,L'\0'); value.resize(GetWindowTextW(cell.window,value.data(),static_cast<int>(value.size())));
                RECT text_rect{0,0,cell_width,0};
                DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&text_rect,DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                height = std::max(height,static_cast<int>(text_rect.bottom) + px(8)); SelectObject(dc,old); ReleaseDC(cell.window,dc);
            }
            if (move) for (const auto& cell : row.cells) { const auto cell_width = cell.width ? px(cell.width) : flexible;
                for(const auto& plot:canvases)if(plot->fullscreen&&plot->inline_parent==container){
                    if(cell.window==plot->window)plot->inline_rectangle={x,y,x+cell_width,y+height};
                    else if(const auto at=std::ranges::find(plot->auxiliary_controls,cell.window);at!=plot->auxiliary_controls.end()){const auto index=static_cast<std::size_t>(at-plot->auxiliary_controls.begin());if(index<plot->auxiliary_rectangles.size())plot->auxiliary_rectangles[index]={x,y,x+cell_width,y+height};}}
                if(GetParent(cell.window)==container){
                    // Reparenting a rich/field column changes native child
                    // order. Keep dialog navigation in the same visual/source
                    // order as Android MathInputFocusOrder, without moving focus.
                    SetWindowPos(cell.window,HWND_BOTTOM,x,y,cell_width,height,SWP_NOACTIVATE | SWP_NOCOPYBITS);
                    if(choice_labels.contains(cell.window)){
                        // The native radio centers its glyph in the row, while
                        // RichEdit otherwise anchors text at the top. Center
                        // the measured rich body, like Android's option row.
                        const auto measured=requested_heights.find(cell.window);
                        const auto inset=measured==requested_heights.end()?0:std::max(0,(height-measured->second)/2);
                        // RichEdit scales the text origin, while the right and
                        // bottom bounds still clip in client pixels. Convert
                        // the inset alone so zoom cannot cut off the glyphs.
                        RECT content{px(cell.inset_x),format_coordinate(cell.window,inset),cell_width-px(cell.inset_x),height};SendMessageW(cell.window,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));
                    }else if(cell.inset_x||cell.inset_y){RECT content{px(cell.inset_x),format_coordinate(cell.window,px(cell.inset_y)),cell_width-px(cell.inset_x),height};if(calculator_buttons.contains(cell.window))content.right-=px(32);SendMessageW(cell.window,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&content));}
                }x += cell_width + gap; }
            y += height + px(row.gap_after);
        }
        if(move){for(const auto& plot:canvases)if(GetParent(plot->window)==container)plot->prepare_drawing();RedrawWindow(container,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);}
        return y+(container==host?0:px(padding_y));
    }
    int arrange(int width,bool move=true){
        current_width=std::max(px(100),width);std::vector<Row> adaptive;std::set<const Canvas*> arranged_controls;
        for(const auto& row:rows){
            const Canvas* segment=nullptr;
            if(!row.cells.empty())for(const auto& plot:canvases)if(plot->type=="segment_transport"&&std::ranges::all_of(row.cells,[&](const auto& cell){return std::ranges::find(plot->auxiliary_controls,cell.window)!=plot->auxiliary_controls.end();})){segment=plot.get();break;}
            if(!segment){adaptive.push_back(row);continue;}
            if(!arranged_controls.insert(segment).second)continue;
            const auto available=std::max(1,current_width-px(12)),gap=px(8),columns=segment->segment_columns(available,gap);
            for(std::size_t first=0;first<segment->auxiliary_controls.size();first+=columns){const auto count=static_cast<int>(std::min<std::size_t>(columns,segment->auxiliary_controls.size()-first));const auto cell_width=std::max(1,(available-gap*(count-1))/count);int measured=px(48);Row controls;controls.gap_after=0;
                for(int index=0;index<count;++index){const auto control=segment->auxiliary_controls[first+index];measured=std::max(measured,segment->segment_control_height(control,cell_width));controls.cells.push_back({control});}
                controls.height=MulDiv(measured,96,static_cast<int>(GetDpiForWindow(host)))+1;adaptive.push_back(std::move(controls));
            }
        }
        return arrange_rows(adaptive,host,current_width,move,6,8);
    }
    static LRESULT CALLBACK field_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
        auto* self = reinterpret_cast<Impl*>(value);
        if(message==WM_ENABLE){
            if(wparam){if(const auto previous=self->input_disabled_readonly.find(item);previous!=self->input_disabled_readonly.end()){SendMessageW(item,EM_SETREADONLY,previous->second,0);self->input_disabled_readonly.erase(previous);}}
            else if(!reader_theme::palette(item).high_contrast){
                // EnableWindow has already set the real WS_DISABLED bit.
                // RichEdit's default FALSE handler forces COLOR_3DFACE over
                // its explicit background. Retain its native read-only text
                // services instead, without custom text painting or fake input.
                self->input_disabled_readonly.try_emplace(item,(GetWindowLongPtrW(item,GWL_STYLE)&ES_READONLY)!=0);
                SendMessageW(item,EM_SETREADONLY,TRUE,0);SendMessageW(item,EM_SETBKGNDCOLOR,0,reader_theme::background(item));InvalidateRect(item,nullptr,TRUE);return 0;
            }
            const auto result=DefSubclassProc(item,message,wparam,lparam);SendMessageW(item,EM_SETBKGNDCOLOR,0,reader_theme::background(item));return result;
        }
        if(message==WM_SETFOCUS){const auto result=DefSubclassProc(item,message,wparam,lparam);SendMessageW(item,EM_SETSEL,0,-1);if(!self->initializing&&self->callbacks.expression_focus)self->callbacks.expression_focus(item,true);return result;}
        if(message==WM_SIZE){const auto result=DefSubclassProc(item,message,wparam,lparam);self->layout_calculator(item);return result;}
        if(message==WM_PAINT||message==WM_PRINTCLIENT){
            const auto result=DefSubclassProc(item,message,wparam,lparam);
            // MathInputControls uses the same permanent accent/10dp outline
            // for simple fields. The native RichEdit still paints every glyph,
            // caret and selection; graph/number-line fields retain their own UI.
            if(self->editors.contains(item))self->card_border(item,true,10,message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wparam):nullptr);
            return result;
        }
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
        if (message == WM_NCDESTROY) {self->calculator_buttons.erase(item);self->input_disabled_readonly.erase(item);RemoveWindowSubclass(item,field_proc,11);}
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK choice_button_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value){
        auto* self=reinterpret_cast<Impl*>(value);
        if(message==WM_SETFOCUS){
            // Win32 can emit BN_CLICKED even for a manual radio while giving
            // it focus. Reuse the restoration guard: focus is not an answer.
            const bool previous=self->initializing;self->initializing=true;const auto result=DefSubclassProc(item,message,wparam,lparam);self->initializing=previous;self->restore_choice_checks();return result;
        }
        if(message==WM_NCDESTROY)RemoveWindowSubclass(item,choice_button_proc,16);
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK choice_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
        auto* self=reinterpret_cast<Impl*>(value);
        if(message==WM_MOUSEWHEEL){if(self->drag.window==item){self->drag={};if(GetCapture()==item)ReleaseCapture();}return SendMessageW(GetParent(self->host),message,wparam,lparam);}
        if(message==WM_LBUTTONDOWN){if(self->cards_enabled){self->drag={item,{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)},GetTickCount64(),false};SetCapture(item);}return 0;}
        if(message==WM_MOUSEMOVE&&self->drag.window==item&&(wparam&MK_LBUTTON)){
            if(std::abs(GET_X_LPARAM(lparam)-self->drag.start.x)>self->px(8)||std::abs(GET_Y_LPARAM(lparam)-self->drag.start.y)>self->px(8))self->drag.moved=true;return 0;}
        if(message==WM_LBUTTONUP&&self->drag.window==item){const bool activate=!self->drag.moved&&self->cards_enabled;self->drag={};if(GetCapture()==item)ReleaseCapture();
            if(activate){const auto toggle=self->choice_labels.at(item);if(IsWindowEnabled(toggle)){SetFocus(toggle);
                for(const auto& [id,buttons]:self->choice_buttons)if(std::ranges::find(buttons,toggle)!=buttons.end()){
                    self->choose(id,self->choice_options.at(toggle),(GetWindowLongPtrW(toggle,GWL_STYLE)&BS_TYPEMASK)==BS_CHECKBOX,true);break;}}}return 0;}
        if(message==WM_LBUTTONDBLCLK)return 0;
        if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE){if(self->drag.window==item)self->drag={};return 0;}
        if(message==WM_NCDESTROY){self->choice_labels.erase(item);RemoveWindowSubclass(item,choice_proc,15);}
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK card_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
        auto* self = reinterpret_cast<Impl*>(value);
        if(message==WM_PAINT||message==WM_PRINTCLIENT){const auto result=DefSubclassProc(item,message,wparam,lparam);self->card_border(item,false,10,message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wparam):nullptr);return result;}
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
        if (message == WM_LBUTTONUP && self->drag.window == item) {
            const bool tap=!self->drag.moved;self->drag = {}; if (GetCapture() == item) ReleaseCapture();
            if(tap&&self->cards_enabled){const auto [id,key]=self->order_cards.at(item);auto& next=self->next_tap_positions[id];const auto& ids=self->orders.at(id);
                if(next<ids.size()){const auto from=static_cast<std::size_t>(std::ranges::find(ids,key)-ids.begin());const auto to=next++;self->reorder(id,from,to);
                    auto current=self->response(id,"order");JsonObject draft;draft.SetNamedValue(L"nextTapPosition",JsonValue::CreateNumberValue(static_cast<double>(next)));current.SetNamedValue(L"interactionDraft",draft);self->answers.SetNamedValue(winrt::to_hstring(id),current);self->change();}}
            return 0;
        }
        if(message==WM_LBUTTONDBLCLK)return 0;
        if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE){if(self->drag.window==item)self->drag={};return 0;}
        if(message==WM_MOUSEWHEEL){if(self->drag.window==item){self->drag={};if(GetCapture()==item)ReleaseCapture();}return SendMessageW(GetParent(self->host),message,wparam,lparam);}
        if (message == WM_NCDESTROY) { if (self->drag.window == item) self->drag = {}; RemoveWindowSubclass(item,card_proc,12); }
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK match_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
        auto* self = reinterpret_cast<Impl*>(value);
        if(message==WM_PAINT||message==WM_PRINTCLIENT){const auto result=DefSubclassProc(item,message,wparam,lparam);const auto& card=self->match_cards.at(item);bool selected=false;
            if(card.source){const auto at=self->selected_sources.find(card.response);selected=at!=self->selected_sources.end()&&at->second==card.id;}
            else for(const auto& pair:object(object(self->answers,winrt::to_hstring(card.response).c_str()),L"pairs"))if(pair.Value().ValueType()==JsonValueType::String&&pair.Value().GetString()==winrt::to_hstring(card.id)){selected=true;break;}
            self->card_border(item,selected,10,message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wparam):nullptr);return result;}
        if (message == WM_LBUTTONDOWN && self->cards_enabled && IsWindowEnabled(item)) {
            const auto& card = self->match_cards.at(item);
            if(card.source){const auto selected=self->selected_sources.find(card.response);if(selected!=self->selected_sources.end()&&selected->second==card.id)self->selected_sources.erase(selected);else self->selected_sources[card.response]=card.id;
                for(const auto& [window,candidate]:self->match_cards)if(candidate.response==card.response)InvalidateRect(window,nullptr,FALSE);}
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
                if(PtInRect(&rectangle,at)){
                    if(self->selected_sources.contains(card.response))self->match(card.response,self->selected_sources.at(card.response),card.id);
                    else if(!origin.source)self->unmatch(card.response,card.id);break;}
            }
            return 0;
        }
        if(message==WM_MOUSEWHEEL){if(self->drag.window==item){self->drag={};if(GetCapture()==item)ReleaseCapture();}return SendMessageW(GetParent(self->host),message,wparam,lparam);}
        if(message==WM_LBUTTONDBLCLK)return 0;
        if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE){if(self->drag.window==item)self->drag={};return 0;}
        if (message == WM_NCDESTROY) { if (self->drag.window == item) self->drag = {}; RemoveWindowSubclass(item,match_proc,13); }
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK scroll_child_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value){
        auto* self=reinterpret_cast<Impl*>(value);const auto parent=GetParent(item);const auto found=self->contents.find(parent);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,scroll_child_proc,19);return DefSubclassProc(item,message,wparam,lparam);}
        if(found!=self->contents.end()){
            if(message==WM_MOUSEHWHEEL||(message==WM_MOUSEWHEEL&&(GET_KEYSTATE_WPARAM(wparam)&MK_SHIFT)))return SendMessageW(parent,message,wparam,lparam);
            RECT area{};GetClientRect(parent,&area);if(message==WM_LBUTTONDOWN&&found->second->horizontal&&found->second->extent>area.right){POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};MapWindowPoints(item,parent,&point,1);return SendMessageW(parent,message,wparam,MAKELPARAM(point.x,point.y));}
        }
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK card_child_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value){
        auto* self=reinterpret_cast<Impl*>(value);const auto found=self->card_children.find(item);
        if(message==WM_NCDESTROY){self->card_children.erase(item);RemoveWindowSubclass(item,card_child_proc,18);return DefSubclassProc(item,message,wparam,lparam);}
        if(found!=self->card_children.end()&&(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK||message==WM_MOUSEMOVE)){
            POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};MapWindowPoints(item,found->second,&point,1);return SendMessageW(found->second,message,wparam,MAKELPARAM(point.x,point.y));}
        if(found!=self->card_children.end()&&message==WM_MOUSEWHEEL)return SendMessageW(found->second,message,wparam,lparam);
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK content_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wparam,lparam);
        LRESULT themed{};if(reader_theme::control_color(message,wparam,lparam,themed))return themed;
        if(message==WM_PRINTCLIENT){RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(reader_theme::Surface::card));return 0;}
        if(message==WM_COMMAND||message==WM_NOTIFY||message==WM_DRAWITEM||message==WM_MEASUREITEM||message==WM_MENUCHAR||(message==WM_HSCROLL&&lparam))return SendMessageW(self->host,message,wparam,lparam);
        if(message==WM_ERASEBKGND){RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(reader_theme::Surface::card));return TRUE;}
        const auto found=self->contents.find(window);if(found!=self->contents.end()){
            auto& content=*found->second;RECT area{};GetClientRect(window,&area);
            const auto scroll=[&](int position){content.scroll=std::clamp(position,0,std::max(0,content.extent-static_cast<int>(area.right)));self->content_layout(content,area.right,true);};
            if(message==WM_HSCROLL){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(window,SB_HORZ,&info);auto position=content.scroll;switch(LOWORD(wparam)){case SB_LINELEFT:position-=self->px(40);break;case SB_LINERIGHT:position+=self->px(40);break;case SB_PAGELEFT:position-=area.right;break;case SB_PAGERIGHT:position+=area.right;break;case SB_THUMBTRACK:position=info.nTrackPos;break;}scroll(position);return 0;}
            if(message==WM_MOUSEHWHEEL||(message==WM_MOUSEWHEEL&&(GET_KEYSTATE_WPARAM(wparam)&MK_SHIFT))){scroll(content.scroll+MulDiv(GET_WHEEL_DELTA_WPARAM(wparam),self->px(40),WHEEL_DELTA)*(message==WM_MOUSEHWHEEL?1:-1));return 0;}
            if(message==WM_LBUTTONDOWN&&content.horizontal&&content.extent>area.right){content.drag_origin={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};content.drag_scroll=content.scroll;content.dragging=true;content.moved=false;SetCapture(window);return 0;}
            if(message==WM_MOUSEMOVE&&content.dragging&&(wparam&MK_LBUTTON)){const auto delta=content.drag_origin.x-GET_X_LPARAM(lparam);content.moved=content.moved||std::abs(delta)>self->px(6);if(content.moved)scroll(content.drag_scroll+delta);return 0;}
            if(message==WM_LBUTTONUP&&content.dragging){content.dragging=false;if(GetCapture()==window)ReleaseCapture();return 0;}
            if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE)content.dragging=false;
        }
        if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(window),message,wparam,lparam);
        return DefWindowProcW(window,message,wparam,lparam);
    }
    void initialize() {
        WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = view_class; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register native math view");
        cls.lpfnWndProc = Canvas::procedure; cls.lpszClassName = canvas_class;cls.style=CS_DBLCLKS;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot register native math canvas");
        cls.lpfnWndProc=content_proc;cls.lpszClassName=content_class;if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register mathematical content containers");
        host = CreateWindowExW(WS_EX_CONTROLPARENT,view_class,L"",WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
        if (!host) throw std::runtime_error("Cannot create native math view");
        if(!reader_theme::reference_mathematics(parent).has_value())reader_theme::set_reference_mathematics(host,true);
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
        if (message == WM_PAINT) { PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);self->paint(dc);EndPaint(window,&paint);return 0; }
        if (message == WM_PRINTCLIENT) { self->paint(reinterpret_cast<HDC>(wparam));return 0; }
        if (message == WM_ERASEBKGND) { RECT rect{}; GetClientRect(window,&rect); FillRect(reinterpret_cast<HDC>(wparam),&rect,reader_theme::brush(reader_theme::Surface::card)); return TRUE; }
        try {
            if (message == WM_MEASUREITEM && measure_native_menu(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
            if (message == WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                if(item.hwndItem==self->expand_button||item.hwndItem==self->reset_button){(void)reader_theme::draw_button(item);auto icon=item.rcItem;InflateRect(&icon,-self->px(12),-self->px(12));const auto colors=reader_theme::palette(window);draw_icon(item.hDC,icon,item.hwndItem==self->expand_button?Icon::expand:Icon::reset,IsWindowEnabled(item.hwndItem)?(item.hwndItem==self->expand_button?colors.text:colors.accent):colors.muted);return TRUE;}
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
                if (header->code == EN_REQUESTRESIZE) { const auto* request = reinterpret_cast<REQRESIZE*>(lparam); self->requested_heights[header->hwndFrom] = static_cast<int>(request->rc.bottom - request->rc.top);self->requested_widths[header->hwndFrom]=static_cast<int>(request->rc.right-request->rc.left); } return 0; }
            if (message == WM_MOUSEWHEEL) return SendMessageW(GetParent(window),message,wparam,lparam);
        } catch (const std::exception& error) { OutputDebugStringW(wide(error.what()).c_str()); }
        return DefWindowProcW(window,message,wparam,lparam);
    }
    void clear() {
        initializing = true;
        for(const auto& canvas:canvases)canvas->close_expanded(false);
        for (auto item = GetWindow(host,GW_CHILD); item;) { const auto next = GetWindow(item,GW_HWNDNEXT); DestroyWindow(item); item = next; }
        canvases.clear();rows.clear();contents.clear();card_children.clear();matching_empty_labels.clear();requested_widths.clear();intrinsic_widths.clear();label_formats.clear();typeset_fragments.clear(); regions.clear(); commands.clear(); editors.clear();expression_editors.clear();keyboard_contexts.clear();calculator_buttons.clear();input_disabled_readonly.clear(); orders.clear(); order_rows.clear(); order_cards.clear(); match_cards.clear(); selected_sources.clear(); choice_buttons.clear(); choice_options.clear();choice_labels.clear();choice_cards.clear();next_tap_positions.clear();matching_layouts.clear();expand_button=reset_button=nullptr;live_visuals.clear(); graph_fields.clear(); requested_heights.clear(); next_id = 100;
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
    // Focus follows Android's select-all-on-entry behavior; the calculator's
    // explicit return caret is applied afterwards, at the end of its result.
    SetFocus(field);const auto end=GetWindowTextLengthW(field);SendMessageW(field,EM_SETSEL,end,end);return true;
}
MathPuzzleView::~MathPuzzleView() { if (impl_) impl_->clear(); }
HWND MathPuzzleView::window() const { return impl_->host; }
HWND MathPuzzleView::fullscreen_button() const { return impl_->expand_button; }
int MathPuzzleView::height_for_width(int width) { return impl_->arrange(width,false); }
void MathPuzzleView::layout(int x,int y,int width) { const auto height = impl_->arrange(width); MoveWindow(impl_->host,x,y,width,height,TRUE); }
std::string MathPuzzleView::answers_json() const { impl_->capture_expressions();return stringify(impl_->answers); }
std::string MathPuzzleView::answer_identity_json() const {
    impl_->capture_expressions();auto values=JsonObject::Parse(impl_->answers.Stringify());
    for(const auto& response:values)if(response.Value().ValueType()==JsonValueType::Object){auto value=response.Value().GetObject();value.Remove(L"editorDraft");value.Remove(L"editorDrafts");value.Remove(L"interactionDraft");}
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
    impl_->visit_cells(impl_->rows,[&](const auto& cell){const auto item=cell.window;
        const bool view_control=item==impl_->expand_button||item==impl_->reset_button||std::ranges::any_of(impl_->canvases,[item](const auto& canvas){return canvas->expand_button==item;});
        const bool expression=std::ranges::any_of(impl_->expression_editors,[item](const auto& value){return value->window==item;});
        const bool interactive = impl_->editors.contains(item) || expression || (impl_->commands.contains(GetDlgCtrlID(item))&&!view_control)
            || std::ranges::any_of(impl_->canvases,[item](const auto& canvas) { return canvas->window == item && canvas->changed; });
        if (interactive) EnableWindow(item,enabled);
    });
    for(const auto& canvas:impl_->canvases)if(canvas->changed){
        if(canvas->type=="segment_transport"){
            canvas->construction_locked=!enabled;
            // A checked construction stays fixed; comparing it with the
            // rigid tool and navigating its view remain presentation actions.
            EnableWindow(canvas->window,TRUE);for(const auto control:canvas->auxiliary_controls)EnableWindow(control,TRUE);
            if(canvas->refresh_segment_controls)canvas->refresh_segment_controls();
            if(!enabled){canvas->segment_dragging=false;canvas->segment_point.reset();if(GetCapture()==canvas->window)ReleaseCapture();}
        }else EnableWindow(canvas->window,enabled);
    }
    for(const auto& [canvas,fields]:impl_->graph_fields)for(std::size_t index=0;index<fields.size();++index){const auto [x,y]=fields[index];EnableWindow(x,enabled&&canvas->points[index].draggable);EnableWindow(y,enabled&&canvas->points[index].draggable);}
}
void MathPuzzleView::set_text_scale(int percentage) {
    percentage=std::clamp(percentage,75,150);if(impl_->text_scale==percentage)return;impl_->text_scale=percentage;
    const auto next=CreateFontW(-MulDiv(18*percentage,static_cast<int>(GetDpiForWindow(impl_->host)),96*100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
    if(!next)throw std::runtime_error("Cannot scale mathematical text");
    impl_->visit_cells(impl_->rows,[&](const auto& cell){const auto item=cell.window;
        wchar_t name[32]{};GetClassNameW(item,name,32);
        if(_wcsnicmp(name,L"RichEdit",8)==0)SendMessageW(item,EM_SETZOOM,percentage,100);
        else SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
    });
    if(impl_->font)DeleteObject(impl_->font);impl_->font=next;
    for(const auto& canvas:impl_->canvases){canvas->scale_text(percentage);if(canvas->type=="segment_transport"&&canvas->fullscreen)canvas->expanded_layout();}
    impl_->requested_heights.clear();impl_->requested_widths.clear();impl_->intrinsic_widths.clear();if(impl_->current_width>0)impl_->arrange(impl_->current_width);
}
void MathPuzzleView::set_expression_request(std::function<std::string(std::string_view)> request){
    impl_->callbacks.expression_request=std::move(request);impl_->typeset_fragments.clear();
    for(const auto& canvas:impl_->canvases)canvas->layout_request=impl_->callbacks.expression_request;
    if(!impl_->callbacks.expression_request)return;
    for(const auto& region:impl_->regions){const auto format=impl_->label_formats.find(region.target);if(format==impl_->label_formats.end()||!IsWindow(region.target))continue;
        CHARRANGE selection{};POINT scroll{};SendMessageW(region.target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(region.target,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
        CHARFORMAT2W original{};original.cbSize=sizeof(original);SendMessageW(region.target,EM_SETSEL,0,1);SendMessageW(region.target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&original));
        (void)set_original_math_text(region.target,region.source_text,format->second.first,impl_->callbacks.expression_request,{},format->second.second,impl_->locale);
        CHARFORMAT2W size{};size.cbSize=sizeof(size);size.dwMask=CFM_SIZE;size.yHeight=original.yHeight;SendMessageW(region.target,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&size));
        PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);paragraph.dwMask=PFM_LINESPACING;paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;SendMessageW(region.target,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
        SendMessageW(region.target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));SendMessageW(region.target,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
    }
    impl_->requested_heights.clear();impl_->requested_widths.clear();impl_->intrinsic_widths.clear();if(impl_->current_width>0)impl_->arrange(impl_->current_width);
}
void MathPuzzleView::reset() { const auto width = impl_->current_width; impl_->clear(); impl_->answers = JsonObject{}; impl_->build(); impl_->arrange(width); if (impl_->callbacks.layout_changed) impl_->callbacks.layout_changed(); impl_->change(); }
} // namespace lgaed::presentation
