#include "presentation/windows/card_list.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lgaed::presentation {
namespace {
constexpr wchar_t class_name[] = L"LearnGameAndEngineDev.CardList";
constexpr UINT header_message=WM_APP+0x381,offset_message=WM_APP+0x382;
struct CardList;
LRESULT CALLBACK card_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
struct CardList {
    struct Card { HWND window=nullptr; std::wstring caption; int height=48; };
    HWND window=nullptr,parent=nullptr;
    int identifier=0,selection=-1,scroll=0,wheel_remainder=0;
    UINT activation=0,scroll_message=0;
    int header_height=0;
    HFONT font=nullptr;
    std::vector<Card> cards;
    bool owned=false,down=false,dragging=false,laying_out=false;
    POINT pressed{};
    int pressed_scroll=0,pressed_card=-1;
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96); }
    int extent() const { std::int64_t total=header_height;for(const auto& card:cards)total+=card.height;return static_cast<int>(total); }
    int top(int index) const { int result=header_height;for(int i=0;i<index;++i)result+=cards[static_cast<std::size_t>(i)].height;return result; }
    int index(HWND child) const { for(std::size_t i=0;i<cards.size();++i)if(cards[i].window==child)return static_cast<int>(i);return -1; }
    int index_at(int y) const {
        if(cards.empty())return -1;
        if(y<header_height)return header_height? -1:0;
        int bottom=header_height;for(std::size_t i=0;i<cards.size();++i){bottom+=cards[i].height;if(y<bottom)return static_cast<int>(i);}
        return static_cast<int>(cards.size()-1);
    }
    bool inside(POINT point) const {
        RECT bounds{};GetClientRect(window,&bounds);
        return PtInRect(&bounds,point)&&point.y+scroll>=header_height&&point.y+scroll<extent();
    }
    void configure_gesture(HWND target) {
        GESTURECONFIG pan{GID_PAN,GC_PAN|GC_PAN_WITH_SINGLE_FINGER_VERTICALLY,GC_PAN_WITH_GUTTER|GC_PAN_WITH_INERTIA};
        SetGestureConfig(target,0,1,&pan,sizeof(pan));
    }
    void layout() {
        if(laying_out)return;
        laying_out=true;
        RECT bounds{};GetClientRect(window,&bounds);
        scroll=std::clamp(scroll,0,std::max(0,extent()-static_cast<int>(bounds.bottom)));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,extent()-1),
            static_cast<UINT>(std::max(0L,bounds.bottom)),scroll,0};
        SetScrollInfo(window,SB_VERT,&info,FALSE);ShowScrollBar(window,SB_VERT,FALSE);
        // Android's catalog does not expose a scrollbar. Native range/position
        // remain available for wheel, drag, keyboard and accessibility.
        GetClientRect(window,&bounds);
        int y=header_height-scroll;
        for(const auto& card:cards){
            SetWindowPos(card.window,nullptr,0,y,bounds.right,card.height,
                SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);
            y+=card.height;
        }
        laying_out=false;
        if(scroll_message)SendMessageW(parent,scroll_message,static_cast<WPARAM>(scroll),reinterpret_cast<LPARAM>(window));
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
    }
    void set_scroll(int value) { scroll=value;layout(); }
    void ensure_visible(int item) {
        if(item<0||static_cast<std::size_t>(item)>=cards.size())return;
        RECT area{};GetClientRect(window,&area);const int start=top(item),end=start+cards[static_cast<std::size_t>(item)].height;
        if(start<scroll)scroll=start;
        else if(end>scroll+area.bottom)scroll=cards[static_cast<std::size_t>(item)].height>area.bottom?start:end-area.bottom;
        layout();
    }
    void select(int item,bool notify,bool reveal=false) {
        if(item<-1||(item>=0&&static_cast<std::size_t>(item)>=cards.size()))return;
        const int previous=selection;selection=item;
        for(std::size_t i=0;i<cards.size();++i){
            const auto style=GetWindowLongPtrW(cards[i].window,GWL_STYLE);
            SetWindowLongPtrW(cards[i].window,GWL_STYLE,static_cast<int>(i)==selection?style|WS_TABSTOP:style&~static_cast<LONG_PTR>(WS_TABSTOP));
        }
        if(previous>=0)InvalidateRect(cards[static_cast<std::size_t>(previous)].window,nullptr,FALSE);
        if(selection>=0)InvalidateRect(cards[static_cast<std::size_t>(selection)].window,nullptr,FALSE);
        if(reveal)ensure_visible(item);
        if(notify)SendMessageW(parent,WM_COMMAND,MAKEWPARAM(identifier,LBN_SELCHANGE),reinterpret_cast<LPARAM>(window));
    }
    void activate(int item) {
        if(item<0||static_cast<std::size_t>(item)>=cards.size())return;
        select(item,true);PostMessageW(parent,activation,0,0);
    }
    void cancel_drag() { down=false;dragging=false;pressed_card=-1;if(GetCapture()==window)ReleaseCapture(); }
    void begin(POINT point) {
        RECT area{};GetClientRect(window,&area);if(!PtInRect(&area,point))return;
        down=true;dragging=false;pressed=point;pressed_scroll=scroll;pressed_card=index_at(point.y+scroll);
        if(pressed_card>=0&&inside(point)){select(pressed_card,true);SetFocus(cards[static_cast<std::size_t>(pressed_card)].window);}
        else pressed_card=-1;
        SetCapture(window);
    }
    void move(POINT point) {
        if(!down)return;
        if(std::abs(point.x-pressed.x)>=4||std::abs(point.y-pressed.y)>=4)dragging=true;
        if(dragging)set_scroll(pressed_scroll+pressed.y-point.y);
    }
    void end(POINT point) {
        if(!down)return;
        const bool click=pressed_card>=0&&!dragging&&inside(point)&&index_at(point.y+scroll)==pressed_card;
        const int card=pressed_card;cancel_drag();if(click)activate(card);
    }
    void keyboard(WPARAM key) {
        if(cards.empty())return;
        if(key==VK_RETURN||key==VK_SPACE){activate(selection);return;}
        RECT area{};GetClientRect(window,&area);
        if(key==VK_PRIOR||key==VK_NEXT){
            set_scroll(scroll+(key==VK_NEXT?1:-1)*std::max(1L,area.bottom));
            const int destination=std::max(0,index_at(scroll));select(destination,true);
            SetFocus(cards[static_cast<std::size_t>(destination)].window);return;
        }
        int destination=selection<0?0:selection;
        switch(key){
        case VK_UP:--destination;break;
        case VK_DOWN:++destination;break;
        case VK_HOME:destination=0;break;
        case VK_END:destination=static_cast<int>(cards.size()-1);break;
        default:return;
        }
        destination=std::clamp(destination,0,static_cast<int>(cards.size()-1));
        select(destination,true,true);SetFocus(cards[static_cast<std::size_t>(destination)].window);
    }
    void wheel(WPARAM value) {
        cancel_drag();wheel_remainder+=GET_WHEEL_DELTA_WPARAM(value);
        const int steps=wheel_remainder/WHEEL_DELTA;wheel_remainder%=WHEEL_DELTA;
        if(!steps)return;
        UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
        RECT bounds{};GetClientRect(window,&bounds);
        const auto amount=lines==WHEEL_PAGESCROLL?bounds.bottom:static_cast<LONG>(std::min(lines,100u))*px(24);
        const auto requested=static_cast<std::int64_t>(scroll)-static_cast<std::int64_t>(steps)*amount;
        set_scroll(static_cast<int>(std::clamp(requested,std::int64_t{0},static_cast<std::int64_t>(std::numeric_limits<int>::max()/2))));
    }
    void context(HWND target,LPARAM position) {
        POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
        if(point.x!=-1||point.y!=-1){auto local=point;ScreenToClient(window,&local);if(inside(local))select(index_at(local.y+scroll),true);}
        else if(const auto item=index(target);item>=0)select(item,true);
        SendMessageW(parent,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(window),position);
    }
    bool gesture(LPARAM value) {
        GESTUREINFO info{sizeof(info)};
        if(!GetGestureInfo(reinterpret_cast<HGESTUREINFO>(value),&info)||(info.dwID!=GID_PAN&&info.dwID!=GID_END))return false;
        POINT point{info.ptsLocation.x,info.ptsLocation.y};ScreenToClient(window,&point);
        if(info.dwFlags&GF_BEGIN){begin(point);dragging=true;}
        else if((info.dwFlags&GF_END)||info.dwID==GID_END)cancel_drag();
        else move(point);
        CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(value));return true;
    }
};
bool navigation_key(WPARAM key) { return key==VK_RETURN||key==VK_SPACE||key==VK_UP||key==VK_DOWN||key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT; }
LRESULT CALLBACK card_proc(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& self=*reinterpret_cast<CardList*>(data);
    if(message==WM_NCDESTROY){RemoveWindowSubclass(child,card_proc,1);return DefSubclassProc(child,message,wp,lp);}
    if(message==BM_CLICK){self.activate(self.index(child));return 0;}
    if(message==WM_GETDLGCODE){const auto result=DefSubclassProc(child,message,wp,lp)|DLGC_WANTARROWS;const auto key=reinterpret_cast<MSG*>(lp);return key&&navigation_key(key->wParam)?result|DLGC_WANTMESSAGE:result;}
    if(message==WM_KEYDOWN&&navigation_key(wp)){self.keyboard(wp);return 0;}
    if((message==WM_KEYUP&&navigation_key(wp))||(message==WM_CHAR&&(wp==L'\r'||wp==L' ')))return 0;
    if(message==WM_CONTEXTMENU){self.context(child,lp);return 0;}
    if(message==WM_MOUSEWHEEL){self.wheel(wp);return 0;}
    if(message==WM_GESTURE&&self.gesture(lp))return 0;
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_MOUSEMOVE){
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};MapWindowPoints(child,self.window,&point,1);
        if(message==WM_LBUTTONDOWN)self.begin(point);
        else if(message==WM_LBUTTONUP)self.end(point);
        else self.move(point);
        return 0;
    }
    return DefSubclassProc(child,message,wp,lp);
}
LRESULT CALLBACK list_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* self=reinterpret_cast<CardList*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<CardList*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);
    try {
        switch(message){
        case WM_CREATE:self->configure_gesture(window);return 0;
        case header_message:{
            const int next=static_cast<int>(wp);
            if(next<0||next>std::numeric_limits<int>::max()/2-self->extent()+self->header_height)return LB_ERR;
            if(self->scroll>=self->header_height&&self->scroll>0)self->scroll+=next-self->header_height;
            self->header_height=next;self->scroll_message=static_cast<UINT>(lp);self->layout();return 0;
        }
        case offset_message:if(wp)self->set_scroll(static_cast<int>(lp));return self->scroll;
        case LB_ADDSTRING:{
            if(!lp||self->cards.size()>=10000)return LB_ERR;
            MEASUREITEMSTRUCT measure{ODT_LISTBOX,static_cast<UINT>(self->identifier),static_cast<UINT>(self->cards.size()),0,
                static_cast<UINT>(self->px(48)),0};
            SendMessageW(self->parent,WM_MEASUREITEM,static_cast<WPARAM>(self->identifier),reinterpret_cast<LPARAM>(&measure));
            if(!measure.itemHeight||measure.itemHeight>static_cast<UINT>(std::numeric_limits<int>::max()/2))return LB_ERR;
            const int height=static_cast<int>(measure.itemHeight);if(self->extent()>std::numeric_limits<int>::max()/2-height)return LB_ERR;
            std::wstring text(reinterpret_cast<const wchar_t*>(lp));
            const auto card=CreateWindowExW(0,L"BUTTON",text.c_str(),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|BS_NOTIFY,
                0,0,1,1,window,reinterpret_cast<HMENU>(1),GetModuleHandleW(nullptr),nullptr);
            if(!card)return LB_ERR;
            if(!SetWindowSubclass(card,card_proc,1,reinterpret_cast<DWORD_PTR>(self))){DestroyWindow(card);return LB_ERR;}
            reader_theme::apply(card,reader_theme::Surface::screen);
            self->cards.push_back({card,std::move(text),height});self->configure_gesture(card);
            SendMessageW(card,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),FALSE);self->layout();return static_cast<LRESULT>(self->cards.size()-1);
        }
        case LB_RESETCONTENT:
            self->cancel_drag();for(const auto& card:self->cards)DestroyWindow(card.window);self->cards.clear();self->selection=-1;self->scroll=0;self->layout();return 0;
        case LB_GETCOUNT:return static_cast<LRESULT>(self->cards.size());
        case LB_GETCURSEL:return self->selection<0?LB_ERR:self->selection;
        case LB_SETCURSEL:{const auto index=static_cast<int>(wp);if(index<-1||(index>=0&&static_cast<std::size_t>(index)>=self->cards.size()))return LB_ERR;self->select(index,false);return index<0?LB_ERR:index;}
        case LB_GETTOPINDEX:return self->cards.empty()?LB_ERR:std::max(0,self->index_at(self->scroll));
        case LB_SETTOPINDEX:if(wp>=self->cards.size())return LB_ERR;self->set_scroll(wp?self->top(static_cast<int>(wp)):0);return 0;
        case LB_SETITEMHEIGHT:{
            if(wp>=self->cards.size()||lp<1||lp>std::numeric_limits<int>::max()/2)return LB_ERR;
            auto& card=self->cards[wp];if(static_cast<std::int64_t>(self->extent())-card.height+lp>std::numeric_limits<int>::max()/2)return LB_ERR;
            const int anchor=std::max(0,self->index_at(self->scroll)),within=self->scroll-self->top(anchor);
            card.height=static_cast<int>(lp);
            // Match list-box restoration: remeasuring earlier cards keeps the
            // same first visible card, including a partial pixel displacement.
            self->scroll=self->top(anchor)+std::min(within,self->cards[static_cast<std::size_t>(anchor)].height-1);
            self->layout();return 0;
        }
        case LB_GETITEMHEIGHT:return wp<self->cards.size()?self->cards[wp].height:LB_ERR;
        case LB_GETITEMRECT:{
            if(wp>=self->cards.size()||!lp)return LB_ERR;RECT area{};GetClientRect(window,&area);const auto top=self->top(static_cast<int>(wp))-self->scroll;
            *reinterpret_cast<RECT*>(lp)={0,top,area.right,top+self->cards[wp].height};return 0;
        }
        case LB_ITEMFROMPOINT:{const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};return MAKELRESULT(std::max(0,self->index_at(point.y+self->scroll)),!self->inside(point));}
        case LB_GETTEXTLEN:return wp<self->cards.size()?static_cast<LRESULT>(self->cards[wp].caption.size()):LB_ERR;
        case LB_GETTEXT:if(wp>=self->cards.size()||!lp)return LB_ERR;std::copy(self->cards[wp].caption.c_str(),self->cards[wp].caption.c_str()+self->cards[wp].caption.size()+1,reinterpret_cast<wchar_t*>(lp));return static_cast<LRESULT>(self->cards[wp].caption.size());
        case WM_SETFONT:self->font=reinterpret_cast<HFONT>(wp);for(const auto& card:self->cards)SendMessageW(card.window,message,wp,lp);return 0;
        case WM_GETFONT:return reinterpret_cast<LRESULT>(self->font);
        case WM_SIZE:self->layout();return 0;
        case WM_SETFOCUS:if(self->selection>=0)SetFocus(self->cards[static_cast<std::size_t>(self->selection)].window);return 0;
        case WM_GETDLGCODE:return DLGC_WANTARROWS|DLGC_WANTCHARS;
        case WM_KEYDOWN:if(navigation_key(wp)){self->keyboard(wp);return 0;}break;
        case WM_MOUSEWHEEL:self->wheel(wp);return 0;
        case WM_LBUTTONDOWN:self->begin({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_MOUSEMOVE:self->move({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_LBUTTONUP:self->end({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_CAPTURECHANGED:case WM_CANCELMODE:self->cancel_drag();return 0;
        case WM_GESTURE:if(self->gesture(lp))return 0;break;
        case WM_CONTEXTMENU:self->context(window,lp);return 0;
        case WM_VSCROLL:{
            RECT area{};GetClientRect(window,&area);SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(window,SB_VERT,&info);int value=self->scroll;
            switch(LOWORD(wp)){case SB_LINEUP:value-=self->px(24);break;case SB_LINEDOWN:value+=self->px(24);break;
            case SB_PAGEUP:value-=area.bottom;break;case SB_PAGEDOWN:value+=area.bottom;break;
            case SB_THUMBTRACK:case SB_THUMBPOSITION:value=info.nTrackPos;break;case SB_TOP:value=0;break;case SB_BOTTOM:value=self->extent();break;default:break;}
            self->set_scroll(value);return 0;
        }
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED){const auto item=self->index(reinterpret_cast<HWND>(lp));if(item>=0)self->activate(item);return 0;}break;
        case WM_DRAWITEM:{
            auto draw=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);const auto index=self->index(draw.hwndItem);if(index<0)return FALSE;
            draw.CtlType=ODT_LISTBOX;draw.CtlID=static_cast<UINT>(self->identifier);draw.itemID=static_cast<UINT>(index);draw.hwndItem=window;
            draw.itemState&=~ODS_SELECTED;if(index==self->selection)draw.itemState|=ODS_SELECTED;
            if(GetFocus()==window&&index==self->selection)draw.itemState|=ODS_FOCUS;
            return SendMessageW(self->parent,WM_DRAWITEM,static_cast<WPARAM>(self->identifier),reinterpret_cast<LPARAM>(&draw));
        }
        case WM_ERASEBKGND:{RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wp),&area,reader_theme::brush(reader_theme::Surface::screen));return TRUE;}
        case WM_PAINT:{
            // A card list can end above the viewport. Paint its uncovered area
            // explicitly rather than leaving the system's white background.
            PAINTSTRUCT paint{};
            if(const auto dc=BeginPaint(window,&paint))
                FillRect(dc,&paint.rcPaint,reader_theme::brush(reader_theme::Surface::screen));
            EndPaint(window,&paint);
            return 0;
        }
        case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(self->owned)delete self;return DefWindowProcW(window,message,wp,lp);
        default:break;
        }
    }catch(const std::exception&){if(message==WM_CREATE)return -1;return LB_ERR;}
    return DefWindowProcW(window,message,wp,lp);
}
} // namespace
HWND create_card_list(HWND parent,int id,UINT activate_message) {
    static const bool registered=[] {
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=class_name;cls.lpfnWndProc=list_proc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("No se pudo registrar el catálogo de tarjetas.");return true;
    }();(void)registered;
    auto self=std::make_unique<CardList>();self->parent=parent;self->identifier=id;self->activation=activate_message;
    const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,class_name,L"Catálogo",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),self.get());
    if(!window)throw std::runtime_error("No se pudo crear el catálogo de tarjetas.");self->owned=true;self.release();return window;
}
void set_card_list_header(HWND list,int height,UINT scroll_message){SendMessageW(list,header_message,static_cast<WPARAM>(height),scroll_message);}
int card_list_scroll_offset(HWND list){return static_cast<int>(SendMessageW(list,offset_message,0,0));}
void set_card_list_scroll_offset(HWND list,int offset){SendMessageW(list,offset_message,1,offset);}
} // namespace lgaed::presentation
