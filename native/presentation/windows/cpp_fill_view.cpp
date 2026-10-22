#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr wchar_t view_class[]=L"LearnGameAndEngineDev.CppFillView";
constexpr std::size_t empty_slot=std::numeric_limits<std::size_t>::max();
}
struct CppFillView::Impl {
    struct Piece { HWND item=nullptr; bool hole=false,newline=false; std::string source; };
    HWND host=nullptr; HMODULE rich=nullptr; HFONT font=nullptr;
    const curriculum::Block& block;
    std::vector<Piece> pieces;
    std::vector<HWND> holes,cards;
    std::vector<std::unique_ptr<windows::CodeEditor>> editors;
    std::vector<std::size_t> values;
    std::function<void()> changed,submit;
    bool updating=true,enabled=true;
    std::size_t selected=empty_slot,dragged=empty_slot;
    POINT down{}; ULONGLONG pressed=0;
    int height=0,horizontal=0,content_width=0,text_scale=100;
    Impl(HWND parent,const curriculum::Block& model,std::string_view saved,
         std::function<void()> on_change,std::function<void()> on_submit)
        :block(model),changed(std::move(on_change)),submit(std::move(on_submit)) {
        windows::ensure_winrt_apartment();
        rich=LoadLibraryW(L"Msftedit.dll");
        if(!rich)throw std::runtime_error("RichEdit is unavailable");
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=view_class;
        cls.lpfnWndProc=proc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register code holes");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,view_class,L"C++",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_HSCROLL,
            0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
        if(!host)throw std::runtime_error("Cannot create code holes");
        reader_theme::apply(host,reader_theme::Surface::card);
        font=CreateFontW(-MulDiv(18,static_cast<int>(GetDpiForWindow(host)),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
        std::vector<std::string> answers; std::vector<std::size_t> restored;
        if(!saved.empty()){
            const auto json=JsonObject::Parse(winrt::to_hstring(saved));
            if(json.HasKey(L"answers"))for(const auto& item:json.GetNamedArray(L"answers"))answers.push_back(winrt::to_string(item.GetString()));
            if(json.HasKey(L"slots"))for(const auto& item:json.GetNamedArray(L"slots")){
                const auto n=item.GetNumber(); restored.push_back(n<0?empty_slot:static_cast<std::size_t>(n));
            }
        }
        std::size_t cursor=0,index=0;
        while(cursor<block.code_template.size()){
            if(block.code_template.compare(cursor,2,"[]")==0){
                HWND item=create(MSFTEDIT_CLASS,L"",WS_TABSTOP|ES_AUTOHSCROLL|(drag()?ES_READONLY:0),100+static_cast<int>(index));
                reader_theme::apply(item,reader_theme::Surface::field);
                SendMessageW(item,EM_SETEVENTMASK,0,ENM_CHANGE);SendMessageW(item,EM_SETTARGETDEVICE,0,1);
                if(drag()){
                    values.push_back(index<restored.size()?restored[index]:empty_slot);
                    if(values.back()!=empty_slot&&values.back()>=block.choices.size())throw std::runtime_error("Saved code card is outside its exercise");
                    set_rich_text(item,values.back()==empty_slot?"…":block.choices[values.back()],{},true);
                }else{
                    editors.push_back(std::make_unique<windows::CodeEditor>(item,
                        windows::EditorDocument{index<answers.size()?answers[index]:""},false,[this]{if(submit)submit();}));
                    style_cpp(item);
                }
                SetWindowSubclass(item,child_proc,1,reinterpret_cast<DWORD_PTR>(this));
                holes.push_back(item);pieces.push_back({item,true,false,{}});cursor+=2;++index;
            }else if(block.code_template[cursor]=='\n'){
                pieces.push_back({nullptr,false,true,{}});++cursor;
            }else{
                const auto end=block.code_template.find_first_of("\n",cursor);
                const auto next=block.code_template.find("[]",cursor);
                const auto stop=std::min(end==std::string::npos?block.code_template.size():end,
                    next==std::string::npos?block.code_template.size():next);
                auto text=block.code_template.substr(cursor,stop-cursor);
                auto item=create(MSFTEDIT_CLASS,L"",ES_READONLY|ES_MULTILINE,1000+static_cast<int>(pieces.size()));
                set_rich_text(item,text,{},true);pieces.push_back({item,false,false,std::move(text)});cursor=stop;
            }
        }
        if(drag())for(std::size_t i=0;i<block.choices.size();++i){
            auto item=create(L"BUTTON",wide(block.choices[i]).c_str(),WS_TABSTOP|BS_PUSHBUTTON,2000+static_cast<int>(i));
            SetWindowSubclass(item,child_proc,1,reinterpret_cast<DWORD_PTR>(this)); cards.push_back(item);
        }
        updating=false;
    }
    ~Impl(){editors.clear();if(host&&IsWindow(host))DestroyWindow(host);if(font)DeleteObject(font);if(rich)FreeLibrary(rich);}
    bool drag()const{return block.type==curriculum::BlockType::drag_fill_code;}
    int px(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(host)),96);}
    HWND create(const wchar_t* type,const wchar_t* text,DWORD style,int id){
        auto item=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!item)throw std::runtime_error("Cannot create a code hole control");
        reader_theme::apply(item,reader_theme::Surface::card);SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        if(_wcsicmp(type,MSFTEDIT_CLASS)==0)SendMessageW(item,EM_SETZOOM,text_scale,100);
        return item;
    }
    int measure(std::string_view value)const{
        const auto dc=GetDC(host);const auto old=SelectObject(dc,font);SIZE size{};const auto text=wide(value);
        GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);SelectObject(dc,old);ReleaseDC(host,dc);return size.cx;
    }
    void layout(int width){
        int x=px(10),y=px(8);const auto line=MulDiv(px(40),text_scale,100);const auto right=std::max(px(90),width-px(10));
        content_width=width;
        // A source line stays a source line. Horizontal overflow belongs to
        // this code surface instead of clipping tokens or changing pages.
        for(const auto& p:pieces){
            if(p.newline){content_width=std::max(content_width,x+px(10));x=px(10);continue;}
            auto value=p.source;if(p.hole){const auto i=static_cast<std::size_t>(std::ranges::find(holes,p.item)-holes.begin());
                value=drag()?(values[i]==empty_slot?"      ":block.choices[values[i]]):editors[i]->document().text;}
            x+=std::max(p.hole?px(78):0,measure(value)+px(p.hole?22:4));
        }
        content_width=std::max(content_width,x+px(10));horizontal=std::clamp(horizontal,0,std::max(0,content_width-width));x=px(10);
        for(const auto& p:pieces){
            if(p.newline){x=px(10);y+=line;continue;}
            auto value=p.source;if(p.hole){const auto i=static_cast<std::size_t>(std::ranges::find(holes,p.item)-holes.begin());
                value=drag()?(values[i]==empty_slot?"      ":block.choices[values[i]]):editors[i]->document().text;}
            int cell=measure(value)+px(p.hole?22:4);if(p.hole)cell=std::max(px(78),cell);
            MoveWindow(p.item,x-horizontal,y,std::max(px(12),cell),line,TRUE);x+=cell;
        }
        y+=line;
        if(!cards.empty()){
            y+=px(8);x=px(10);
            for(auto card:cards){const int id=GetDlgCtrlID(card)-2000;int cell=std::max(px(48),measure(block.choices[static_cast<std::size_t>(id)])+px(24));
                if(x>px(10)&&x+cell>right){x=px(10);y+=line+px(6);}cell=std::min(cell,right-px(10));
                MoveWindow(card,x,y,cell,line,TRUE);x+=cell+px(6);}
            y+=line;
        }
        height=y+px(8)+(content_width>width?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_width-1),static_cast<UINT>(std::max(1,width)),horizontal,0};
        SetScrollInfo(host,SB_HORZ,&info,TRUE);
        reader_theme::refresh_scrollbars(host);
    }
    void assign(std::size_t slot,std::size_t choice){
        if(!enabled||slot>=holes.size()||(choice!=empty_slot&&choice>=block.choices.size()))return;
        updating=true;
        // One physical card occupies one hole. Duplicate text cards remain
        // interchangeable during evaluation, exactly as in Android.
        if(choice!=empty_slot)for(std::size_t i=0;i<values.size();++i)if(values[i]==choice&&i!=slot){values[i]=empty_slot;set_rich_text(holes[i],"…",{},true);}
        values[slot]=choice;set_rich_text(holes[slot],choice==empty_slot?"…":block.choices[choice],{},true);
        updating=false;if(changed)changed();RECT area{};GetClientRect(host,&area);layout(area.right);
    }
    static LRESULT CALLBACK child_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);const int id=GetDlgCtrlID(item);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,child_proc,1);return DefSubclassProc(item,message,wparam,lparam);}
        if(self.drag()&&self.enabled){
            if(message==WM_LBUTTONDOWN&&id>=2000){self.dragged=static_cast<std::size_t>(id-2000);self.pressed=GetTickCount64();self.down={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};SetCapture(item);}
            if(message==WM_LBUTTONUP&&id>=2000&&self.dragged!=empty_slot){
                const auto card=self.dragged;self.dragged=empty_slot;POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ClientToScreen(item,&point);
                if(GetCapture()==item)ReleaseCapture();
                if(GetTickCount64()-self.pressed>=20)for(std::size_t i=0;i<self.holes.size();++i){RECT r{};GetWindowRect(self.holes[i],&r);if(PtInRect(&r,point)){self.assign(i,card);return 0;}}
                self.selected=card;
            }
            if(message==WM_LBUTTONUP&&id>=100&&id<1000){const auto slot=static_cast<std::size_t>(id-100);
                self.assign(slot,self.selected);self.selected=empty_slot;return 0;}
            if(message==WM_KEYDOWN&&id>=100&&id<1000&&(wparam==VK_DELETE||wparam==VK_BACK)){self.assign(static_cast<std::size_t>(id-100),empty_slot);return 0;}
        }
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);self->host=item;SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(item,message,wparam,lparam);
        LRESULT result{};if(reader_theme::control_color(message,wparam,lparam,result))return result;
        if(message==WM_DRAWITEM&&reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))return TRUE;
        if(message==WM_SIZE){self->layout(LOWORD(lparam));return 0;}
        if(message==WM_HSCROLL||message==WM_MOUSEHWHEEL){
            RECT r{};GetClientRect(item,&r);SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(item,SB_HORZ,&info);
            if(message==WM_MOUSEHWHEEL)self->horizontal+=GET_WHEEL_DELTA_WPARAM(wparam)*self->px(48)/WHEEL_DELTA;
            else switch(LOWORD(wparam)){case SB_LINELEFT:self->horizontal-=self->px(24);break;case SB_LINERIGHT:self->horizontal+=self->px(24);break;
                case SB_PAGELEFT:self->horizontal-=r.right;break;case SB_PAGERIGHT:self->horizontal+=r.right;break;
                case SB_THUMBTRACK:case SB_THUMBPOSITION:self->horizontal=info.nTrackPos;break;default:break;}
            self->layout(r.right);return 0;
        }
        if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(item),message,wparam,lparam);
        if(message==WM_ERASEBKGND){RECT r{};GetClientRect(item,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,reader_theme::brush(reader_theme::Surface::card));return TRUE;}
        if(message==WM_COMMAND&&!self->updating){
            const int id=LOWORD(wparam);if(HIWORD(wparam)==EN_CHANGE&&id>=100&&id<1000&&!self->drag()){
                const auto i=static_cast<std::size_t>(id-100);if(i<self->editors.size()){self->updating=true;self->editors[i]->capture_change();style_cpp(self->holes[i]);self->updating=false;if(self->changed)self->changed();}
                return 0;
            }
            if(HIWORD(wparam)==BN_CLICKED&&id>=2000){self->selected=static_cast<std::size_t>(id-2000);return 0;}
        }
        return DefWindowProcW(item,message,wparam,lparam);
    }
};
CppFillView::CppFillView(HWND p,const curriculum::Block& b,std::string_view saved,std::function<void()> c,std::function<void()> s)
    :impl_(std::make_unique<Impl>(p,b,saved,std::move(c),std::move(s))){}
CppFillView::~CppFillView()=default;
HWND CppFillView::window()const{return impl_->host;}
int CppFillView::height_for_width(int width){impl_->layout(width);return impl_->height;}
std::vector<std::string> CppFillView::answers()const{std::vector<std::string> r;for(const auto& e:impl_->editors)r.push_back(e->document().text);return r;}
std::vector<std::size_t> CppFillView::slots()const{return impl_->values;}
std::string CppFillView::response_json()const{
    JsonObject result;JsonArray array;
    if(impl_->drag()){for(auto n:slots())array.Append(JsonValue::CreateNumberValue(n==empty_slot?-1:static_cast<double>(n)));result.Insert(L"slots",array);}
    else{for(const auto& s:answers())array.Append(JsonValue::CreateStringValue(winrt::to_hstring(s)));result.Insert(L"answers",array);}
    return winrt::to_string(result.Stringify());
}
windows::CodeEditor* CppFillView::editor(HWND focused){for(std::size_t i=0;i<impl_->editors.size();++i)if(impl_->holes[i]==focused)return impl_->editors[i].get();return nullptr;}
void CppFillView::set_enabled(bool value){impl_->enabled=value;for(auto h:impl_->holes)EnableWindow(h,value);for(auto h:impl_->cards)EnableWindow(h,value);}
void CppFillView::set_text_scale(int percentage){
    percentage=std::clamp(percentage,75,150);if(impl_->text_scale==percentage)return;impl_->text_scale=percentage;
    const auto next=CreateFontW(-MulDiv(18*percentage,static_cast<int>(GetDpiForWindow(impl_->host)),72*100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
    if(!next)throw std::runtime_error("Cannot scale code-hole text");
    for(auto item=GetWindow(impl_->host,GW_CHILD);item;item=GetWindow(item,GW_HWNDNEXT)){
        wchar_t name[32]{};GetClassNameW(item,name,32);
        if(_wcsnicmp(name,L"RichEdit",8)==0)SendMessageW(item,EM_SETZOOM,percentage,100);
        else SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);
    }
    if(impl_->font)DeleteObject(impl_->font);impl_->font=next;RECT bounds{};GetClientRect(impl_->host,&bounds);impl_->layout(bounds.right);
}
void CppFillView::reset(){impl_->updating=true;for(const auto& e:impl_->editors)e->reset(windows::EditorDocument{});for(std::size_t i=0;i<impl_->values.size();++i){impl_->values[i]=empty_slot;set_rich_text(impl_->holes[i],"…",{},true);}impl_->updating=false;}
}
