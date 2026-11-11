#include "presentation/windows/order_code_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
constexpr wchar_t class_name[]=L"LearnGameAndEngineDev.OrderCode";
void prepare_graphics(){static const auto token=[](){Gdiplus::GdiplusStartupInput input;ULONG_PTR value=0;
    if(Gdiplus::GdiplusStartup(&value,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Cannot initialize code-card drawing");return value;}();(void)token;}
}
struct OrderCodeView::Impl {
    HWND host=nullptr,pressed=nullptr;HMODULE rich=nullptr;HFONT font=nullptr,number_font=nullptr;
    std::vector<std::string> fragments;
    std::size_t required_count=0; // Original graded indices only.
    std::vector<std::size_t> ordered,original; // Also holds visual separator ids.
    std::vector<HWND> rows;
    std::vector<int> requested_heights;
    std::vector<RECT> cards;
    std::function<void()> changed;
    POINT down{};bool dragging=false,drag_changed=false,enabled=true,wrap=false,translucent=false;LONG_PTR pressed_style=0;std::size_t next_tap=0;
    int percentage=100,width=1,height=1,content_width=1,horizontal=0,gutter=18,number_pixels=0;
    Impl(HWND parent,std::vector<std::string> source,std::vector<std::size_t> order,
            std::function<void()> callback,std::size_t optional_count)
        :fragments(std::move(source)),required_count(fragments.size()),changed(std::move(callback)){
        if(fragments.empty()||optional_count>4)
            throw std::invalid_argument("Optional order cards require real fragments and at most four separators");
        std::vector<std::size_t> identities(required_count);
        std::iota(identities.begin(),identities.end(),0);
        auto sorted=order;std::ranges::sort(sorted);if(sorted!=identities)order=identities;
        ordered=std::move(order);
        fragments.resize(required_count+optional_count);
        for(std::size_t i=required_count;i<fragments.size();++i)ordered.push_back(i);
        original.resize(fragments.size());std::iota(original.begin(),original.end(),0);
        prepare_graphics();
        // Creating/showing a native child can synchronously paint its parent.
        // Allocate every geometry slot before exposing the HWND, and publish
        // the ordered model only once every referenced text HWND exists.
        requested_heights.resize(fragments.size());cards.resize(fragments.size());rows.reserve(fragments.size());
        WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=procedure;
        cls.lpszClassName=class_name;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        WNDCLASSEXW found{sizeof(found)};if(!GetClassInfoExW(cls.hInstance,class_name,&found)&&!RegisterClassExW(&cls))throw std::runtime_error("Cannot register code ordering");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,class_name,L"C++",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_HSCROLL,
            0,0,1,1,parent,nullptr,cls.hInstance,this);
        if(!host)throw std::runtime_error("Cannot create code ordering");
        reader_theme::apply(host,reader_theme::Surface::card);
        rich=LoadLibraryW(L"Msftedit.dll");if(!rich)throw std::runtime_error("Cannot load code ordering text");
        font=CreateFontW(-px(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,reader_theme::code_font_face());
        for(std::size_t i=0;i<fragments.size();++i){
            const auto row=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_READONLY|ES_MULTILINE|ES_AUTOHSCROLL,
                0,0,1,1,host,reinterpret_cast<HMENU>(100+i),cls.hInstance,nullptr);
            if(!row)throw std::runtime_error("Cannot create code fragment");
            reader_theme::apply(row,reader_theme::Surface::low);
            reader_theme::set_text_color(row,RGB(225,230,235));
            // WM_SETFONT replaces RichEdit's character defaults. It belongs
            // before the original syntax spans, never after their creation.
            SendMessageW(row,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
            // Code cards use the same full-width monospace spaces as the
            // editor. Never narrow indentation or remove original blank lines.
            set_rich_text(row,fragments[i],{},true);
            SendMessageW(row,EM_SETTARGETDEVICE,0,1);
            SendMessageW(row,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE);
            SetWindowSubclass(row,child,1,reinterpret_cast<DWORD_PTR>(this));rows.push_back(row);
        }
    }
    ~Impl(){release(false);if(host&&IsWindow(host))DestroyWindow(host);if(number_font)DeleteObject(number_font);if(font)DeleteObject(font);if(rich)FreeLibrary(rich);}
    int px(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(host)),96);}
    std::size_t position(HWND row)const{return static_cast<std::size_t>(std::ranges::find(ordered,static_cast<std::size_t>(GetDlgCtrlID(row)-100))-ordered.begin());}
    int prepare_number_font(){
        const auto pixels=std::max(1,MulDiv(px(18),percentage,100));
        if(pixels!=number_pixels){const auto replacement=CreateFontW(-pixels,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,reader_theme::code_font_face());
            if(!replacement)throw std::runtime_error("Cannot create code-order numbering font");if(number_font)DeleteObject(number_font);number_font=replacement;number_pixels=pixels;}
        const auto dc=GetDC(host);const auto old=SelectObject(dc,number_font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
        const auto label=std::to_wstring(std::max<std::size_t>(1,ordered.size()));SIZE extent{};GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&extent);
        gutter=std::max(px(18),static_cast<int>(extent.cx)+px(3)+1);SelectObject(dc,old);ReleaseDC(host,dc);return static_cast<int>(std::max(1L,metrics.tmHeight+metrics.tmExternalLeading));
    }
    void format_bounds(HWND row){
        RECT rect{};GetClientRect(row,&rect);rect.left=MulDiv(px(16),100,percentage);rect.top=MulDiv(px(8),100,percentage);
        // RichEdit scales both edge insets. Convert the right/bottom margins
        // too, instead of reserving a larger, clipping inset at non-100% zoom.
        rect.right=std::max(rect.left+1,rect.right-MulDiv(px(16),100,percentage));
        rect.bottom=std::max(rect.top+1,rect.bottom-MulDiv(px(8),100,percentage));
        SendMessageW(row,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&rect));
    }
    SIZE text_size(HWND row,int fallback_line){
        // Measure the actual TextServices line boxes, not an auxiliary GDI
        // font's estimate. A single-line card has no next baseline from which
        // to infer its height, especially with zoom or font fallback.
        POINTL first{};SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
        const auto count=SendMessageW(row,EM_GETLINECOUNT,0,0);
        Microsoft::WRL::ComPtr<IRichEditOle> ole;
        SendMessageW(row,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument> document;if(ole)(void)ole.As(&document);
        LONG right=first.x,bottom=first.y+fallback_line;
        constexpr LONG flags=tomStart|tomClientCoord|tomAllowOffClient;
        const auto include=[&](LONG character){
            Microsoft::WRL::ComPtr<ITextRange> range;
            LONG x=0,y=0;
            if(document&&document->Range(character,character,&range)==S_OK&&range){
                if(range->GetPoint(flags|TA_RIGHT|TA_BOTTOM,&x,&y)==S_OK){
                    right=std::max(right,x);bottom=std::max(bottom,y);
                }
            }
        };
        for(LRESULT line=0;line<count;++line){
            const auto start=SendMessageW(row,EM_LINEINDEX,static_cast<WPARAM>(line),0);
            if(start<0)continue;
            const auto length=SendMessageW(row,EM_LINELENGTH,static_cast<WPARAM>(start),0);
            POINTL end{},position{};
            SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),start+length);
            SendMessageW(row,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&position),start);
            if(end.y==position.y)right=std::max(right,end.x);
            bottom=std::max(bottom,position.y+fallback_line);
            include(static_cast<LONG>(start));
            if(length>0)include(static_cast<LONG>(start+length-1));
        }
        // Two device pixels cover final glyph overhang/rounding; all original
        // text, full-width spaces, syntax colors and selection remain untouched.
        return {std::max(1L,right-first.x)+px(32)+2,std::max(1L,bottom-first.y)+px(16)+2};
    }
    void layout(int w){
        width=std::max(1,w);const auto line=prepare_number_font();const auto viewport=std::max(1,width-gutter);
        std::vector<SIZE> sizes(rows.size());content_width=viewport;
        for(std::size_t i=0;i<rows.size();++i){const auto row=rows[i];SendMessageW(row,WM_SETREDRAW,FALSE,0);
            if(wrap)SendMessageW(row,EM_SETTARGETDEVICE,0,1);
            SetWindowPos(row,nullptr,0,0,viewport,std::max(px(48),line+px(16)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);format_bounds(row);
            POINT origin{};SendMessageW(row,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&origin));
            SendMessageW(row,EM_REQUESTRESIZE,0,0);sizes[i]=text_size(row,line);
            if(wrap&&sizes[i].cx>viewport){SendMessageW(row,EM_SETTARGETDEVICE,0,0);
                SetWindowPos(row,nullptr,0,0,viewport,sizes[i].cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);format_bounds(row);
                SendMessageW(row,EM_REQUESTRESIZE,0,0);sizes[i]=text_size(row,line);sizes[i].cx=viewport;
            }else if(wrap)SendMessageW(row,EM_SETTARGETDEVICE,0,0);
            content_width=std::max(content_width,static_cast<int>(sizes[i].cx));}
        horizontal=std::clamp(horizontal,0,std::max(0,content_width-viewport));int y=px(2);
        for(const auto i:ordered){const auto row=rows[i];cards[i]={gutter-horizontal,y,gutter-horizontal+sizes[i].cx,y+sizes[i].cy};
            SetWindowPos(row,nullptr,cards[i].left,cards[i].top,sizes[i].cx,sizes[i].cy,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);format_bounds(row);
            const auto inset=std::max(1,px(1));const auto region=CreateRoundRectRgn(inset,inset,sizes[i].cx-inset,sizes[i].cy-inset,px(18),px(18));
            if(region&&!SetWindowRgn(row,region,FALSE))DeleteObject(region);SendMessageW(row,WM_SETREDRAW,TRUE,0);InvalidateRect(row,nullptr,FALSE);y+=sizes[i].cy+px(4);}
        const bool overflow=content_width>viewport;height=std::max(px(4),y-px(2))+(overflow?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_width-1),static_cast<UINT>(viewport),horizontal,0};SetScrollInfo(host,SB_HORZ,&info,TRUE);
        ShowScrollBar(host,SB_HORZ,overflow);reader_theme::refresh_scrollbars(host);InvalidateRect(host,nullptr,FALSE);
    }
    void draw_card(Gdiplus::Graphics& graphics,const RECT& rect,Gdiplus::Pen& outline,Gdiplus::Brush* fill)const{
        const auto half=static_cast<float>(std::max(1,px(1)))/2;
        const Gdiplus::RectF box(static_cast<float>(rect.left)+half,static_cast<float>(rect.top)+half,
            static_cast<float>(rect.right-rect.left)-half*2,static_cast<float>(rect.bottom-rect.top)-half*2);
        const auto diameter=std::min({static_cast<float>(px(20)),box.Width,box.Height});if(diameter<=0)return;
        Gdiplus::GraphicsPath path;path.AddArc(box.X,box.Y,diameter,diameter,180,90);path.AddArc(box.GetRight()-diameter,box.Y,diameter,diameter,270,90);
        path.AddArc(box.GetRight()-diameter,box.GetBottom()-diameter,diameter,diameter,0,90);path.AddArc(box.X,box.GetBottom()-diameter,diameter,diameter,90,90);path.CloseFigure();
        if(fill)graphics.FillPath(fill,&path);graphics.DrawPath(&outline,&path);
    }
    void paint_row_outline(HWND row,HDC dc)const{
        if(!dc)return;RECT rect{};GetClientRect(row,&rect);const auto ink=reader_theme::palette(host).outline;
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::Pen outline(Gdiplus::Color(255,GetRValue(ink),GetGValue(ink),GetBValue(ink)),static_cast<float>(std::max(1,px(1))));
        draw_card(graphics,rect,outline,nullptr);
    }
    void paint(HDC dc){
        RECT area{};GetClientRect(host,&area);FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::card));
        auto column=area;column.right=std::min<LONG>(column.right,gutter);FillRect(dc,&column,reader_theme::brush(reader_theme::Surface::inset));
        if(ordered.empty())return;
        const auto colors=reader_theme::palette(host);const auto surface=reader_theme::background(rows.empty()?host:rows.front());
        const auto color=[](COLORREF value){return Gdiplus::Color(255,GetRValue(value),GetGValue(value),GetBValue(value));};
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::SolidBrush fill(color(surface));Gdiplus::Pen outline(color(colors.outline),static_cast<float>(std::max(1,px(1))));
        graphics.SetClip(Gdiplus::Rect(gutter,0,std::max(0L,area.right-gutter),area.bottom),Gdiplus::CombineModeIntersect);
        for(const auto i:ordered)draw_card(graphics,cards[i],outline,&fill);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,colors.high_contrast?colors.text:RGB(111,119,130));const auto old=SelectObject(dc,number_font?number_font:font);
        for(std::size_t n=0;n<ordered.size();++n){auto rect=cards[ordered[n]];if(rect.right<=rect.left||rect.bottom<=rect.top)continue;
            rect.left=0;rect.right=gutter-px(3);rect.top-=px(2);rect.bottom+=px(2);const auto label=std::to_wstring(n+1);
            DrawTextW(dc,label.c_str(),-1,&rect,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
        SelectObject(dc,old);
    }
    void move(std::size_t from,std::size_t to,bool tap=false){
        if(!enabled||from>=ordered.size()||to>=ordered.size())return;
        if(from==to&&!tap)return;
        if(from!=to){const auto value=ordered[from];ordered.erase(ordered.begin()+from);ordered.insert(ordered.begin()+to,value);layout(width);}
        // Publishing clears reviewed feedback and can clamp the lesson's
        // bottom scroll. Keep those screen destinations stable while the
        // pointer is captured; the final arrangement is published on release.
        // A deliberate tap still starts a sequence even at its current index.
        if(dragging)drag_changed=true;else if(changed)changed();
    }
    void release(bool publish=true){const auto item=pressed;const bool notify=publish&&drag_changed;
        pressed=nullptr;dragging=false;drag_changed=false;
        if(item&&translucent&&IsWindow(item)){SetLayeredWindowAttributes(item,0,255,LWA_ALPHA);SetWindowLongPtrW(item,GWL_EXSTYLE,pressed_style);RedrawWindow(item,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME);}
        translucent=false;if(item&&GetCapture()==item)ReleaseCapture();
        // ReleaseCapture synchronously sends WM_CAPTURECHANGED. All gesture
        // state must already be clear, and owner callbacks may destroy us.
        if(notify&&changed){const auto callback=changed;callback();}}
    static LRESULT CALLBACK child(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,child,1);return DefSubclassProc(item,message,wp,lp);}
        if(message==WM_PAINT||message==WM_PRINTCLIENT){
            const auto result=DefSubclassProc(item,message,wp,lp);
            // RichEdit owns an opaque client surface. Its native paint must
            // finish before the same original card contour is drawn over it.
            const auto dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wp):GetDC(item);
            self.paint_row_outline(item,dc);if(message==WM_PAINT&&dc)ReleaseDC(item,dc);return result;
        }
        if(message==WM_GETDLGCODE)return DLGC_WANTARROWS|DLGC_WANTCHARS;
        if(message==WM_LBUTTONDBLCLK)return 0;
        if(message==WM_SYSKEYDOWN&&(wp==VK_UP||wp==VK_DOWN)){
            const auto at=self.position(item);if(wp==VK_UP&&at)self.move(at,at-1);else if(wp==VK_DOWN&&at+1<self.ordered.size())self.move(at,at+1);return 0;}
        if(message==WM_LBUTTONDOWN){if(!self.enabled)return 0;self.pressed=item;self.dragging=false;
            self.down={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(item,&self.down);SetFocus(item);SetCapture(item);return 0;}
        if(message==WM_MOUSEMOVE&&self.pressed==item&&(wp&MK_LBUTTON)){
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(item,&point);
            // A clear displacement is a drag even when DOWN/MOVE/UP arrive
            // within one clock tick. A hold cutoff turned fast drags into
            // nextTapPosition clicks and moved the source to an unrelated row.
            if(!self.dragging&&(std::abs(point.x-self.down.x)>self.px(4)||std::abs(point.y-self.down.y)>self.px(4))){
                self.dragging=true;self.pressed_style=GetWindowLongPtrW(item,GWL_EXSTYLE);SetWindowLongPtrW(item,GWL_EXSTYLE,self.pressed_style|WS_EX_LAYERED);
                self.translucent=SetLayeredWindowAttributes(item,0,140,LWA_ALPHA)!=FALSE;if(!self.translucent)SetWindowLongPtrW(item,GWL_EXSTYLE,self.pressed_style);}
            if(self.dragging)for(std::size_t to=0;to<self.ordered.size();++to){RECT bounds{};GetWindowRect(self.rows[self.ordered[to]],&bounds);
                if(PtInRect(&bounds,point)){self.move(self.position(item),to);break;}}
            return 0;}
        if(message==WM_LBUTTONUP&&self.pressed==item){const bool drag=self.dragging;self.release();
            if(!drag&&self.next_tap<self.ordered.size())self.move(self.position(item),self.next_tap++,true);return 0;}
        if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE){self.release();return 0;}
        if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL){const auto owner=self.host;self.release();
            return IsWindow(owner)?SendMessageW(owner,message,wp,lp):0;}
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK procedure(HWND item,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->host=item;SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(item,message,wp,lp);
        if(message==WM_DESTROY)self->release(false);
        if(message==WM_NOTIFY){const auto note=reinterpret_cast<NMHDR*>(lp);if(note&&note->code==EN_REQUESTRESIZE&&note->idFrom>=100&&note->idFrom-100<self->requested_heights.size()){
            const auto size=reinterpret_cast<REQRESIZE*>(lp);self->requested_heights[note->idFrom-100]=size->rc.bottom-size->rc.top;return 0;}}
        if(message==WM_HSCROLL||message==WM_MOUSEHWHEEL){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(item,SB_HORZ,&info);
            if(message==WM_MOUSEHWHEEL)self->horizontal+=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*self->px(40);
            else switch(LOWORD(wp)){case SB_LINELEFT:self->horizontal-=self->px(24);break;case SB_LINERIGHT:self->horizontal+=self->px(24);break;case SB_PAGELEFT:self->horizontal-=self->width-self->gutter;break;case SB_PAGERIGHT:self->horizontal+=self->width-self->gutter;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:self->horizontal=info.nTrackPos;break;}
            self->layout(self->width);return 0;}
        if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(item),message,wp,lp);
        LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        if(message==WM_ERASEBKGND){self->paint(reinterpret_cast<HDC>(wp));return 1;}
        if(message==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);self->paint(dc);EndPaint(item,&paint);return 0;}
        return DefWindowProcW(item,message,wp,lp);
    }
};
OrderCodeView::OrderCodeView(HWND parent,std::vector<std::string> source,std::vector<std::size_t> order,std::function<void()> changed)
    :impl_(std::make_unique<Impl>(parent,std::move(source),std::move(order),std::move(changed))){}
OrderCodeView::~OrderCodeView()=default;
HWND OrderCodeView::window()const{return impl_->host;}
int OrderCodeView::height_for_width(int width){impl_->layout(width);return impl_->height;}
std::vector<std::size_t> OrderCodeView::order()const{return impl_->ordered;}
std::vector<std::pair<HWND,std::string>> OrderCodeView::reading_regions()const{std::vector<std::pair<HWND,std::string>> result;for(const auto i:impl_->ordered)result.emplace_back(impl_->rows[i],impl_->fragments[i]);return result;}
void OrderCodeView::set_completed(bool value){
    for(const auto row:impl_->rows)reader_theme::completed_control(row,value);
    InvalidateRect(impl_->host,nullptr,FALSE);
}
void OrderCodeView::set_enabled(bool value){impl_->enabled=value;if(!value)impl_->release();}
void OrderCodeView::set_text_scale(int value){impl_->percentage=std::clamp(value,25,400);for(const auto row:impl_->rows)SendMessageW(row,EM_SETZOOM,impl_->percentage,100);impl_->layout(impl_->width);}
void OrderCodeView::set_wrap(bool value){impl_->wrap=value;for(const auto row:impl_->rows)SendMessageW(row,EM_SETTARGETDEVICE,0,value?0:1);impl_->layout(impl_->width);}
void OrderCodeView::restart_tap_sequence(){impl_->next_tap=0;}
void OrderCodeView::reset(){impl_->release(false);set_completed(false);impl_->ordered=impl_->original;impl_->next_tap=0;impl_->layout(impl_->width);if(impl_->changed)impl_->changed();}
}
