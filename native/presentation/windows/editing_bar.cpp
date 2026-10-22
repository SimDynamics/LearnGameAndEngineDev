#include "presentation/windows/editing_bar.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/icons.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "core/lesson_state.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
constexpr wchar_t bar_class[]=L"LearnGameAndEngineDev.EditingBar";
constexpr UINT_PTR focus_timer=1,strip_subclass=2,button_subclass=3;
constexpr int first_command=9000,shortcut_command=first_command+3;
const std::vector<std::string>& defaults(){
    // KeyboardShortcutSettingsRepository.DEFAULT_SHORTCUTS, in original order.
    // Personal arrays are never extended, trimmed or deduplicated.
    static const std::vector<std::string> values{
        "(",")","{","}","[","]",";","\"","'","<",">","=","+","-","*","/","\\","%","&","|","!","?",",",".",":",
        "::","<<",">>","->","++","--","&&","||","==","!=","<=",">=","^","~","#","##","...","->*",".*"};
    return values;
}
}
std::vector<std::string> parse_state_shortcuts(const state::StateData& data){
    const auto stored=state::string_value(data,"keyboard_shortcut_settings","cpp_shortcuts");
    if(stored.empty())return defaults();
    windows::ensure_winrt_apartment();
    try{
        winrt::Windows::Data::Json::JsonArray array;
        if(!winrt::Windows::Data::Json::JsonArray::TryParse(winrt::to_hstring(stored),array))return defaults();
        std::vector<std::string> result;result.reserve(array.Size());
        for(const auto& value:array){
            using winrt::Windows::Data::Json::JsonValueType;
            result.push_back(value.ValueType()==JsonValueType::Null?"":value.ValueType()==JsonValueType::String?
                winrt::to_string(value.GetString()):winrt::to_string(value.Stringify()));
        }
        return result;
    }catch(const winrt::hresult_error&){return defaults();}
}
struct EditingBar::Impl{
    HWND parent=nullptr,bar=nullptr,strip=nullptr,target=nullptr;
    std::array<HWND,3> fixed{};
    std::vector<HWND> buttons;
    std::vector<std::string> shortcuts;
    HFONT font=nullptr;
    int horizontal=0,extent=0;
    bool shown=false,closing=false,laying_out=false;
    std::function<windows::CodeEditor*(HWND)> resolve;
    std::function<void()> visibility_changed;
    Impl(HWND owner,std::function<windows::CodeEditor*(HWND)> resolver,std::function<void()> changed)
        :parent(owner),resolve(std::move(resolver)),visibility_changed(std::move(changed)){}
    ~Impl(){closing=true;if(bar&&IsWindow(bar)){KillTimer(bar,focus_timer);DestroyWindow(bar);}if(font)DeleteObject(font);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(parent)),96);}
    bool owns_focus(HWND focus)const{return focus&&bar&&(focus==bar||IsChild(bar,focus));}
    windows::CodeEditor* editor(HWND handle)const{
        if(!handle||!IsWindow(handle)||!IsChild(parent,handle)||!IsWindowEnabled(handle)||(GetWindowLongPtrW(handle,GWL_STYLE)&ES_READONLY))return nullptr;
        // Keep HWND identity only; page changes may replace its adapter.
        return resolve(handle);
    }
    void theme(){reader_theme::apply(bar,reader_theme::Surface::header);reader_theme::apply(strip,reader_theme::Surface::header);
        for(const auto item:fixed)if(item)reader_theme::apply(item,reader_theme::Surface::card);
        for(const auto item:buttons)reader_theme::apply(item,reader_theme::Surface::card);}
    void update_font(){
        const auto next=CreateFontW(-MulDiv(10,static_cast<int>(GetDpiForWindow(parent)),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!next)return;for(const auto item:fixed)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        for(const auto item:buttons)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;
    }
    int natural_width(std::size_t index)const{
        const auto dc=GetDC(strip);const auto old=SelectObject(dc,font);const auto text=wide(shortcuts[index]);SIZE measured{};
        GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&measured);SelectObject(dc,old);ReleaseDC(strip,dc);
        return std::max(px(36),static_cast<int>(measured.cx)+px(20));
    }
    void layout(){
        if(!bar||!strip||laying_out)return;laying_out=true;RECT area{};GetClientRect(bar,&area);
        const int padding=px(5),gap=px(3),tab_width=px(48),history_width=px(36);
        const int height=std::max(1,static_cast<int>(area.bottom)-padding*2),right=static_cast<int>(area.right)-padding;
        MoveWindow(fixed[0],padding,padding,tab_width,height,TRUE);
        MoveWindow(fixed[1],std::max(padding,right-history_width*2-gap),padding,history_width,height,TRUE);
        MoveWindow(fixed[2],std::max(padding,right-history_width),padding,history_width,height,TRUE);
        const int left=padding+tab_width+gap,width=std::max(1,right-history_width*2-gap*2-left);
        MoveWindow(strip,left,padding,width,height,TRUE);
        extent=0;for(std::size_t i=0;i<buttons.size();++i)extent+=natural_width(i)+(i?gap:0);
        horizontal=std::clamp(horizontal,0,std::max(0,extent-width));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,extent-1),static_cast<UINT>(width),horizontal,0};
        SetScrollInfo(strip,SB_HORZ,&info,TRUE);reader_theme::refresh_scrollbars(strip);RECT visible{};GetClientRect(strip,&visible);
        int x=-horizontal;for(std::size_t i=0;i<buttons.size();++i){const auto cell=natural_width(i);
            MoveWindow(buttons[i],x,0,cell,std::max(px(24),static_cast<int>(visible.bottom)),TRUE);x+=cell+gap;}
        laying_out=false;RedrawWindow(bar,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void scroll(int position){horizontal=position;layout();}
    void reveal(HWND button){const auto found=std::ranges::find(buttons,button);if(found==buttons.end())return;
        const auto index=static_cast<std::size_t>(found-buttons.begin());int left=0;for(std::size_t i=0;i<index;++i)left+=natural_width(i)+px(3);
        RECT area{};GetClientRect(strip,&area);const auto right=left+natural_width(index);
        if(left<horizontal)scroll(left);else if(right>horizontal+area.right)scroll(right-area.right);
    }
    void set_shortcuts(std::vector<std::string> values){
        if(values==shortcuts&&buttons.size()==values.size())return;
        if(values.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()-shortcut_command))throw std::length_error("Too many code shortcuts");
        const bool had_focus=owns_focus(GetFocus());for(const auto item:buttons)DestroyWindow(item);buttons.clear();shortcuts=std::move(values);horizontal=0;
        for(std::size_t i=0;i<shortcuts.size();++i){const auto caption=wide(shortcuts[i]);
            const auto button=CreateWindowExW(0,L"BUTTON",caption.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                0,0,1,1,strip,reinterpret_cast<HMENU>(static_cast<INT_PTR>(shortcut_command+i)),GetModuleHandleW(nullptr),nullptr);
            if(!button)throw std::runtime_error("Cannot create a configured code shortcut");
            SetWindowSubclass(button,shortcut_proc,button_subclass,reinterpret_cast<DWORD_PTR>(this));buttons.push_back(button);
            reader_theme::apply(button,reader_theme::Surface::card);if(font)SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        }
        layout();if(had_focus)SetFocus(fixed[0]);
    }
    void initialize(){
        if(!IsWindow(parent)||!resolve)throw std::invalid_argument("EditingBar requires a parent and a live-editor resolver");
        static const bool registered=[]{WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);
            type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=bar_class;return RegisterClassW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;}();
        if(!registered)throw std::runtime_error("Cannot register contextual editing controls");
        bar=CreateWindowExW(WS_EX_CONTROLPARENT,bar_class,L"Símbolos y edición de código",WS_CHILD|WS_CLIPCHILDREN,
            0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);if(!bar)throw std::runtime_error("Cannot create contextual editing controls");
        strip=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Atajos de código",WS_CHILD|WS_VISIBLE|WS_HSCROLL|WS_CLIPCHILDREN,
            0,0,1,1,bar,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!strip||!SetWindowSubclass(strip,strip_proc,strip_subclass,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot create scrolling code shortcuts");
        static constexpr const wchar_t* labels[]{L"TAB",L"Deshacer",L"Rehacer"};
        for(std::size_t i=0;i<fixed.size();++i){fixed[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,1,1,bar,reinterpret_cast<HMENU>(static_cast<INT_PTR>(first_command+i)),GetModuleHandleW(nullptr),nullptr);
            if(!fixed[i])throw std::runtime_error("Cannot create fixed editing controls");}
        theme();update_font();set_shortcuts(defaults());
        if(!SetTimer(bar,focus_timer,100,nullptr))throw std::runtime_error("Cannot observe the active code editor");
    }
    void refresh(){
        if(closing||!bar||!IsWindow(bar))return;const auto focus=GetFocus();if(!owns_focus(focus))target=editor(focus)?focus:nullptr;
        if(!editor(target))target=nullptr;const bool visible=target!=nullptr;
        if(visible!=shown){shown=visible;ShowWindow(bar,shown?SW_SHOWNOACTIVATE:SW_HIDE);if(visibility_changed)visibility_changed();}
    }
    void execute(int id){
        if(id<first_command||id>=shortcut_command+static_cast<int>(shortcuts.size()))return;refresh();const auto destination=target;
        if(!editor(destination))return;SetFocus(destination);if(GetFocus()!=destination){refresh();return;}
        const auto adapter=editor(destination);if(!adapter){refresh();return;}
        if(id==first_command)adapter->insert_tab();else if(id==first_command+1)adapter->undo();else if(id==first_command+2)adapter->redo();
        else (void)adapter->insert_shortcut(shortcuts[static_cast<std::size_t>(id-shortcut_command)]);
        refresh();
    }
    bool draw(const DRAWITEMSTRUCT& item)const{
        if(item.CtlType!=ODT_BUTTON)return false;const auto colors=reader_theme::palette(item.hwndItem);const auto saved=SaveDC(item.hDC);
        FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::header));const auto fill=CreateSolidBrush((item.itemState&ODS_SELECTED)?colors.header:colors.field);
        const auto border=CreatePen(PS_SOLID,1,colors.outline);const auto old_brush=SelectObject(item.hDC,fill),old_pen=SelectObject(item.hDC,border);
        RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,px(7),px(7));
        SelectObject(item.hDC,old_brush);SelectObject(item.hDC,old_pen);DeleteObject(fill);DeleteObject(border);
        auto bounds=item.rcItem;InflateRect(&bounds,-px(6),-px(4));
        if(item.CtlID==first_command+1||item.CtlID==first_command+2){InflateRect(&bounds,-px(3),-px(3));draw_icon(item.hDC,bounds,item.CtlID==first_command+1?Icon::undo:Icon::redo,colors.text);}
        else{SelectObject(item.hDC,font);SetTextColor(item.hDC,colors.text);SetBkMode(item.hDC,TRANSPARENT);
            std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(item.hwndItem))+1,L'\0');text.resize(static_cast<std::size_t>(GetWindowTextW(item.hwndItem,text.data(),static_cast<int>(text.size()))));
            DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
        if(item.itemState&ODS_FOCUS){auto focus=item.rcItem;InflateRect(&focus,-2,-2);DrawFocusRect(item.hDC,&focus);}RestoreDC(item.hDC,saved);return true;
    }
    static LRESULT CALLBACK shortcut_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL)return SendMessageW(self.strip,message,wp,lp);
        if(message==WM_SETFOCUS)self.reveal(window);
        if(message==WM_NCDESTROY)RemoveWindowSubclass(window,shortcut_proc,button_subclass);
        return DefSubclassProc(window,message,wp,lp);
    }
    static LRESULT CALLBACK strip_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_COMMAND||message==WM_DRAWITEM)return SendMessageW(self.bar,message,wp,lp);
        LRESULT color=0;if(reader_theme::control_color(message,wp,lp,color))return color;
        if(message==WM_HSCROLL){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(window,SB_HORZ,&info);RECT area{};GetClientRect(window,&area);int next=self.horizontal;
            switch(LOWORD(wp)){case SB_LINELEFT:next-=self.px(40);break;case SB_LINERIGHT:next+=self.px(40);break;case SB_PAGELEFT:next-=area.right;break;case SB_PAGERIGHT:next+=area.right;break;
            case SB_THUMBPOSITION:case SB_THUMBTRACK:next=info.nTrackPos;break;case SB_LEFT:next=0;break;case SB_RIGHT:next=self.extent;break;default:break;}self.scroll(next);return 0;}
        if(message==WM_MOUSEHWHEEL||message==WM_MOUSEWHEEL){self.scroll(self.horizontal+(message==WM_MOUSEWHEEL?-1:1)*GET_WHEEL_DELTA_WPARAM(wp)*self.px(80)/WHEEL_DELTA);return 0;}
        if(message==WM_ERASEBKGND){RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wp),&area,reader_theme::brush(reader_theme::Surface::header));return 1;}
        if(message==WM_SIZE)self.layout();
        if(message==WM_NCDESTROY)RemoveWindowSubclass(window,strip_proc,strip_subclass);
        return DefSubclassProc(window,message,wp,lp);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->bar=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wp,lp);
        try{LRESULT color=0;if(reader_theme::control_color(message,wp,lp,color))return color;
            switch(message){case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)self->execute(LOWORD(wp));return 0;
            case WM_DRAWITEM:if(self->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;break;
            case WM_ERASEBKGND:{RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wp),&area,reader_theme::brush(reader_theme::Surface::header));return 1;}
            case WM_SIZE:self->layout();return 0;case WM_TIMER:if(wp==focus_timer){self->refresh();return 0;}break;
            case WM_DPICHANGED_AFTERPARENT:self->update_font();self->layout();return 0;case WM_THEMECHANGED:case WM_SETTINGCHANGE:self->theme();return 0;
            case WM_DESTROY:KillTimer(window,focus_timer);return 0;case WM_NCDESTROY:self->bar=nullptr;self->target=nullptr;self->shown=false;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;default:break;}
        }catch(...){MessageBeep(MB_ICONWARNING);}return DefWindowProcW(window,message,wp,lp);
    }
};
EditingBar::EditingBar(HWND parent,std::function<windows::CodeEditor*(HWND)> resolve,std::function<void()> changed)
    :impl_(std::make_unique<Impl>(parent,std::move(resolve),std::move(changed))){impl_->initialize();}
EditingBar::~EditingBar()=default;
void EditingBar::refresh(){impl_->refresh();}
void EditingBar::set_shortcuts(std::vector<std::string> values){impl_->set_shortcuts(std::move(values));}
void EditingBar::place(int x,int y,int width,int height){if(impl_->bar)MoveWindow(impl_->bar,x,y,std::max(1,width),std::max(1,height),TRUE);}
bool EditingBar::visible()const{return impl_->shown;}
HWND EditingBar::window()const{return impl_->bar;}
int EditingBar::logical_height()const{return 66;}
} // namespace lgaed::presentation
