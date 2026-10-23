#include "presentation/windows/order_code_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace lgaed::presentation {
namespace {constexpr wchar_t class_name[]=L"LearnGameAndEngineDev.OrderCode";}
struct OrderCodeView::Impl {
    HWND host=nullptr,pressed=nullptr;HMODULE rich=nullptr;HFONT font=nullptr;
    std::vector<std::string> fragments;
    std::vector<std::size_t> ordered,original;
    std::vector<HWND> rows;
    std::vector<int> requested_heights;
    std::function<void()> changed;
    POINT down{};ULONGLONG began=0;bool dragging=false,enabled=true,wrap=false;std::size_t next_tap=0;
    int percentage=100,width=1,height=1,content_width=1,horizontal=0;
    Impl(HWND parent,std::vector<std::string> source,std::vector<std::size_t> order,std::function<void()> callback)
        :fragments(std::move(source)),ordered(std::move(order)),changed(std::move(callback)){
        original.resize(fragments.size());std::iota(original.begin(),original.end(),0);
        auto sorted=ordered;std::ranges::sort(sorted);if(sorted!=original)ordered=original;
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
        requested_heights.resize(fragments.size());
        for(std::size_t i=0;i<fragments.size();++i){
            const auto row=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_READONLY|ES_MULTILINE|ES_AUTOHSCROLL,
                0,0,1,1,host,reinterpret_cast<HMENU>(100+i),cls.hInstance,nullptr);
            if(!row)throw std::runtime_error("Cannot create code fragment");
            reader_theme::apply(row,reader_theme::Surface::card);set_rich_text(row,fragments[i],{},true);
            SendMessageW(row,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);SendMessageW(row,EM_SETTARGETDEVICE,0,1);
            SendMessageW(row,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE);
            SetWindowSubclass(row,child,1,reinterpret_cast<DWORD_PTR>(this));rows.push_back(row);
        }
    }
    ~Impl(){if(host&&IsWindow(host))DestroyWindow(host);if(font)DeleteObject(font);if(rich)FreeLibrary(rich);}
    int px(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(host)),96);}
    std::size_t position(HWND row)const{return static_cast<std::size_t>(std::ranges::find(ordered,static_cast<std::size_t>(GetDlgCtrlID(row)-100))-ordered.begin());}
    void layout(int w){
        width=std::max(1,w);int y=px(8);content_width=width;
        const auto dc=GetDC(host);const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
        const int line=std::max(px(20),MulDiv(metrics.tmHeight,percentage,100));
        std::vector<int> heights(fragments.size(),line+px(12));
        for(std::size_t i=0;i<fragments.size();++i){
            std::size_t start=0;int lines=0;
            do{const auto end=fragments[i].find('\n',start);auto text=wide(fragments[i].substr(start,end==std::string::npos?end:end-start));
                SIZE extent{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&extent);
                content_width=std::max(content_width,MulDiv(extent.cx,percentage,100)+px(44));++lines;
                if(end==std::string::npos)break;start=end+1;
            }while(start<fragments[i].size());
            heights[i]=lines*line+px(12);
        }
        SelectObject(dc,old);ReleaseDC(host,dc);
        if(wrap)content_width=width;
        horizontal=std::clamp(horizontal,0,std::max(0,content_width-width));
        for(const auto i:ordered){const int cell=std::max(1,content_width-px(36));MoveWindow(rows[i],px(28)-horizontal,y,cell,heights[i],TRUE);
            if(wrap){SendMessageW(rows[i],EM_REQUESTRESIZE,0,0);if(requested_heights[i]>0){heights[i]=requested_heights[i]+px(12);MoveWindow(rows[i],px(28),y,cell,heights[i],TRUE);}}
            y+=heights[i]+px(4);}
        height=y+px(8)+(content_width>width?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_width-1),static_cast<UINT>(width),horizontal,0};SetScrollInfo(host,SB_HORZ,&info,TRUE);
        reader_theme::refresh_scrollbars(host);InvalidateRect(host,nullptr,FALSE);
    }
    void move(std::size_t from,std::size_t to){
        if(!enabled||from>=ordered.size()||to>=ordered.size())return;
        if(from!=to){const auto value=ordered[from];ordered.erase(ordered.begin()+from);ordered.insert(ordered.begin()+to,value);layout(width);}
        if(changed)changed();
    }
    void release(){const auto item=pressed;pressed=nullptr;dragging=false;if(item&&GetCapture()==item)ReleaseCapture();}
    static LRESULT CALLBACK child(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,child,1);return DefSubclassProc(item,message,wp,lp);}
        if(message==WM_GETDLGCODE)return DLGC_WANTARROWS|DLGC_WANTCHARS;
        if(message==WM_LBUTTONDBLCLK)return 0;
        if(message==WM_SYSKEYDOWN&&(wp==VK_UP||wp==VK_DOWN)){
            const auto at=self.position(item);if(wp==VK_UP&&at)self.move(at,at-1);else if(wp==VK_DOWN&&at+1<self.ordered.size())self.move(at,at+1);return 0;}
        if(message==WM_LBUTTONDOWN){if(!self.enabled)return 0;self.pressed=item;self.dragging=false;
            self.down={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(item,&self.down);self.began=GetTickCount64();SetFocus(item);SetCapture(item);return 0;}
        if(message==WM_MOUSEMOVE&&self.pressed==item&&(wp&MK_LBUTTON)){
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(item,&point);
            if(GetTickCount64()-self.began>=20&&(std::abs(point.x-self.down.x)>self.px(4)||std::abs(point.y-self.down.y)>self.px(4)))self.dragging=true;
            if(self.dragging)for(std::size_t to=0;to<self.ordered.size();++to){RECT bounds{};GetWindowRect(self.rows[self.ordered[to]],&bounds);
                if(point.y>=bounds.top&&point.y<bounds.bottom){self.move(self.position(item),to);break;}}
            return 0;}
        if(message==WM_LBUTTONUP&&self.pressed==item){const bool drag=self.dragging;self.release();
            if(!drag&&self.next_tap<self.ordered.size())self.move(self.position(item),self.next_tap++);return 0;}
        if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE){self.release();return 0;}
        if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL){self.release();return SendMessageW(self.host,message,wp,lp);}
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK procedure(HWND item,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->host=item;SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(item,message,wp,lp);
        if(message==WM_NOTIFY){const auto note=reinterpret_cast<NMHDR*>(lp);if(note&&note->code==EN_REQUESTRESIZE&&note->idFrom>=100&&note->idFrom-100<self->requested_heights.size()){
            const auto size=reinterpret_cast<REQRESIZE*>(lp);self->requested_heights[note->idFrom-100]=size->rc.bottom-size->rc.top;return 0;}}
        if(message==WM_HSCROLL||message==WM_MOUSEHWHEEL){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(item,SB_HORZ,&info);
            if(message==WM_MOUSEHWHEEL)self->horizontal+=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*self->px(40);
            else switch(LOWORD(wp)){case SB_LINELEFT:self->horizontal-=self->px(24);break;case SB_LINERIGHT:self->horizontal+=self->px(24);break;case SB_PAGELEFT:self->horizontal-=self->width;break;case SB_PAGERIGHT:self->horizontal+=self->width;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:self->horizontal=info.nTrackPos;break;}
            self->layout(self->width);return 0;}
        if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(item),message,wp,lp);
        LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        if(message==WM_ERASEBKGND){RECT bounds{};GetClientRect(item,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::card));return 1;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reader_theme::palette(item).muted);
            const auto old=SelectObject(dc,self->font);
            for(std::size_t n=0;n<self->ordered.size();++n){RECT bounds{};GetWindowRect(self->rows[self->ordered[n]],&bounds);MapWindowPoints(HWND_DESKTOP,item,reinterpret_cast<POINT*>(&bounds),2);
                bounds.left=self->px(2);bounds.right=self->px(23);const auto label=std::to_wstring(n+1);DrawTextW(dc,label.c_str(),-1,&bounds,DT_RIGHT|DT_TOP|DT_SINGLELINE);}
            SelectObject(dc,old);EndPaint(item,&paint);return 0;}
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
void OrderCodeView::set_enabled(bool value){impl_->enabled=value;if(!value)impl_->release();}
void OrderCodeView::set_text_scale(int value){impl_->percentage=std::clamp(value,25,400);for(const auto row:impl_->rows)SendMessageW(row,EM_SETZOOM,impl_->percentage,100);impl_->layout(impl_->width);}
void OrderCodeView::set_wrap(bool value){impl_->wrap=value;for(const auto row:impl_->rows)SendMessageW(row,EM_SETTARGETDEVICE,0,value?0:1);impl_->layout(impl_->width);}
void OrderCodeView::restart_tap_sequence(){impl_->next_tap=0;}
void OrderCodeView::reset(){impl_->ordered=impl_->original;impl_->next_tap=0;impl_->layout(impl_->width);if(impl_->changed)impl_->changed();}
}
