#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <limits>
#include <map>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr wchar_t view_class[]=L"LearnGameAndEngineDev.CppFillView";
constexpr std::size_t empty_slot=std::numeric_limits<std::size_t>::max();
void prepare_graphics(){static const auto token=[](){Gdiplus::GdiplusStartupInput input;ULONG_PTR value=0;
    if(Gdiplus::GdiplusStartup(&value,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Cannot initialize code-token drawing");return value;}();(void)token;}

}
struct CppFillView::Impl {
    struct Piece { HWND item=nullptr; bool hole=false,newline=false; std::string source; };
    HWND host=nullptr,palette=nullptr,pressed_item=nullptr; HMODULE rich=nullptr; HFONT font=nullptr;
    const curriculum::Block& block;
    std::vector<Piece> pieces;
    std::vector<HWND> holes,cards,card_labels;
    std::vector<std::unique_ptr<windows::CodeEditor>> editors;
    std::vector<std::size_t> values,bank_order;
    std::function<void()> changed,submit;
    std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)> lesson_gesture;
    bool updating=true,enabled=true,wrap=false,laying_out=false,floating=false,dragging=false;
    std::size_t dragged=empty_slot;
    POINT down{}; ULONGLONG pressed=0;
    int height=0,horizontal=0,content_width=0,text_scale=100;
    int gutter_width=0,palette_horizontal=0,palette_extent=0,palette_height=0;
    std::vector<RECT> numbered_lines;
    std::vector<LONG> inline_baselines;
    bool completed=false;
    std::map<HWND,int> requested_heights;
    Impl(HWND parent,const curriculum::Block& model,std::string_view saved,
         std::function<void()> on_change,std::function<void()> on_submit,
         std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)> on_gesture)
        :block(model),changed(std::move(on_change)),submit(std::move(on_submit)),lesson_gesture(std::move(on_gesture)) {
        windows::ensure_winrt_apartment();prepare_graphics();
        rich=LoadLibraryW(L"Msftedit.dll");
        if(!rich)throw std::runtime_error("RichEdit is unavailable");
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=view_class;
        cls.lpfnWndProc=proc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register code holes");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,view_class,L"C++",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_HSCROLL,
            0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
        if(!host)throw std::runtime_error("Cannot create code holes");
        SetWindowSubclass(host,background_proc,2,reinterpret_cast<DWORD_PTR>(this));
        reader_theme::apply(host,reader_theme::Surface::card);
        font=CreateFontW(-MulDiv(18,static_cast<int>(GetDpiForWindow(host)),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,reader_theme::code_font_face());
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
                HWND item=create(MSFTEDIT_CLASS,L"",WS_TABSTOP|ES_AUTOHSCROLL|(drag()?ES_READONLY:(ES_MULTILINE|ES_CENTER)),100+static_cast<int>(index));
                reader_theme::apply(item,drag()?reader_theme::Surface::card:reader_theme::Surface::field);
                SendMessageW(item,EM_SETEVENTMASK,0,ENM_CHANGE);SendMessageW(item,EM_SETTARGETDEVICE,0,1);
                if(drag()){
                    values.push_back(index<restored.size()?restored[index]:empty_slot);
                    if(values.back()!=empty_slot&&values.back()>=block.choices.size())throw std::runtime_error("Saved code card is outside its exercise");
                    set_token_text(item,values.back()==empty_slot?"":block.choices[values.back()]);
                }else{
                    editors.push_back(std::make_unique<windows::CodeEditor>(item,
                        windows::EditorDocument{index<answers.size()?answers[index]:""},false,[this]{if(submit)submit();}));
                    style_cpp(item);
                    PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);
                    paragraph.dwMask=PFM_ALIGNMENT|PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;
                    paragraph.wAlignment=PFA_CENTER;paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;
                    SendMessageW(item,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
                    set_edit_hint(item,"...",RGB(125,135,145),true);
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
                set_token_text(item,text,RGB(225,230,235));SendMessageW(item,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE);
                SendMessageW(item,EM_SETTARGETDEVICE,0,1);
                pieces.push_back({item,false,false,std::move(text)});cursor=stop;
            }
        }
        if(drag()){
            palette=create(L"STATIC",L"C++",WS_CLIPCHILDREN|WS_HSCROLL,3000);
            reader_theme::apply(palette,reader_theme::Surface::inset);
            SetWindowSubclass(palette,palette_proc,3,reinterpret_cast<DWORD_PTR>(this));
            for(std::size_t i=0;i<block.choices.size();++i){
                auto item=create(L"BUTTON",wide(block.choices[i]).c_str(),WS_TABSTOP|BS_PUSHBUTTON|WS_CLIPCHILDREN,2000+static_cast<int>(i),palette);
                reader_theme::apply(item,reader_theme::Surface::low);
                auto label=create(MSFTEDIT_CLASS,L"",ES_READONLY|ES_MULTILINE,4000+static_cast<int>(i),item);
                reader_theme::apply(label,reader_theme::Surface::low);set_token_text(label,block.choices[i]);
                SetWindowSubclass(label,label_proc,4,reinterpret_cast<DWORD_PTR>(this));
                SetWindowSubclass(item,child_proc,1,reinterpret_cast<DWORD_PTR>(this));cards.push_back(item);card_labels.push_back(label);
            }
            bank_order.resize(cards.size());std::iota(bank_order.begin(),bank_order.end(),0);
            std::erase_if(bank_order,[&](auto choice){return std::ranges::find(values,choice)!=values.end();});refresh_slots();
        }
        updating=false;
    }
    ~Impl(){release();editors.clear();if(palette&&GetParent(palette)!=host&&IsWindow(palette))DestroyWindow(palette);if(host&&IsWindow(host))DestroyWindow(host);if(font)DeleteObject(font);if(rich)FreeLibrary(rich);}
    bool drag()const{return block.type==curriculum::BlockType::drag_fill_code;}
    int px(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(host)),96);}
    HWND create(const wchar_t* type,const wchar_t* text,DWORD style,int id,HWND parent=nullptr){
        auto item=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent?parent:host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!item)throw std::runtime_error("Cannot create a code hole control");
        reader_theme::apply(item,reader_theme::Surface::card);SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        if(_wcsicmp(type,MSFTEDIT_CLASS)==0)SendMessageW(item,EM_SETZOOM,text_scale,100);
        return item;
    }
    void set_token_text(HWND item,std::string_view text,COLORREF ink=RGB(216,216,216)){
        reader_theme::set_text_color(item,ink);
        set_rich_text(item,text,{},true);compact_code_spaces(item);
        SendMessageW(item,EM_SETTARGETDEVICE,0,1);
        // Display-only code has no edit inset. Source characters and native
        // syntax spans remain intact, including deliberately repeated spaces.
        RECT area{};GetClientRect(item,&area);SendMessageW(item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&area));
    }
    int text_height()const{
        const auto dc=GetDC(host);const auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
        SelectObject(dc,old);ReleaseDC(host,dc);return std::max(1L,metrics.tmHeight+metrics.tmExternalLeading);
    }
    SIZE token_size(HWND item,std::string_view source){
        const auto line=text_height();
        SetWindowPos(item,nullptr,0,0,std::max(px(18),measure(source)+px(4)),line,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
        RECT area{};GetClientRect(item,&area);SendMessageW(item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&area));
        POINTL begin{},end{};SendMessageW(item,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&begin),0);
        SendMessageW(item,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),static_cast<LPARAM>(wide(source).size()));
        return {std::max(1L,end.x-begin.x),line};
    }
    void refresh_slots(){
        for(std::size_t i=0;i<holes.size();++i){const auto choice=values[i];
            reader_theme::apply(holes[i],choice==empty_slot?reader_theme::Surface::card:reader_theme::Surface::low);
            set_token_text(holes[i],choice==empty_slot?std::string_view{}:block.choices[choice]);}
        for(std::size_t i=0;i<cards.size();++i)ShowWindow(cards[i],std::ranges::find(values,i)==values.end()?SW_SHOWNOACTIVATE:SW_HIDE);
    }
    void frame(HWND item,HDC dc,int radius,COLORREF outline,COLORREF fill,bool clear=false){
        RECT area{};GetClientRect(item,&area);if(area.right<=0||area.bottom<=0||!dc)return;
        if(clear){const auto brush=CreateSolidBrush(reader_theme::background(GetParent(item)));FillRect(dc,&area,brush);DeleteObject(brush);}
        const auto color=[](COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));};
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        const auto stroke=static_cast<float>(std::max(1,px(1))),half=stroke/2;
        const Gdiplus::RectF box(half,half,static_cast<float>(area.right)-stroke,static_cast<float>(area.bottom)-stroke);
        const auto diameter=std::min({static_cast<float>(px(radius*2)),box.Width,box.Height});if(diameter<=0)return;
        Gdiplus::GraphicsPath path;path.AddArc(box.X,box.Y,diameter,diameter,180,90);path.AddArc(box.GetRight()-diameter,box.Y,diameter,diameter,270,90);
        path.AddArc(box.GetRight()-diameter,box.GetBottom()-diameter,diameter,diameter,0,90);path.AddArc(box.X,box.GetBottom()-diameter,diameter,diameter,90,90);path.CloseFigure();
        if(clear){Gdiplus::SolidBrush background(color(fill));graphics.FillPath(&background,&path);}Gdiplus::Pen border(color(outline),stroke);graphics.DrawPath(&border,&path);
    }
    void paint_slot(HWND item,HDC dc){const auto slot=static_cast<std::size_t>(GetDlgCtrlID(item)-100);const auto colors=reader_theme::palette(item);
        const auto filled=slot<values.size()&&values[slot]!=empty_slot;
        frame(item,dc,filled?4:3,completed&&!colors.high_contrast?RGB(105,220,145):filled?colors.accent:(colors.high_contrast?colors.outline:RGB(106,106,106)),reader_theme::background(item));}
    bool paint_card(const DRAWITEMSTRUCT& draw){
        if(std::ranges::find(cards,draw.hwndItem)==cards.end())return false;
        const auto colors=reader_theme::palette(draw.hwndItem);frame(draw.hwndItem,draw.hDC,4,colors.accent,reader_theme::background(draw.hwndItem),true);
        reader_theme::paint_keyboard_focus(draw.hwndItem,draw.hDC,draw.rcItem,colors.accent);return true;
    }
    int layout_palette(int width){
        if(!palette)return 0;const int pad=px(12);int x=pad,y=pad;int max_height=0;palette_extent=width;
        struct Place{HWND card,label;int x,y,w,h,tw,th;};std::vector<Place> positions;
        for(const auto i:bank_order){
            const auto size=token_size(card_labels[i],block.choices[i]);const int w=size.cx+px(24),h=size.cy+px(20);
            positions.push_back({cards[i],card_labels[i],x,y,w,h,size.cx,size.cy});
            if(floating){x+=w;max_height=std::max(max_height,h);}else y+=h;
        }
        if(floating){palette_extent=std::max(width,x+pad);palette_height=pad*2+max_height;
            palette_horizontal=std::clamp(palette_horizontal,0,std::max(0,palette_extent-width));}
        else{palette_horizontal=0;palette_height=y+pad;}
        for(const auto& p:positions){MoveWindow(p.card,p.x-palette_horizontal,p.y,p.w,p.h,TRUE);MoveWindow(p.label,px(12),px(10),p.tw,p.th,TRUE);
            RECT bounds{};GetClientRect(p.label,&bounds);SendMessageW(p.label,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&bounds));}
        const bool overflow=floating&&palette_extent>width;
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,palette_extent-1),static_cast<UINT>(std::max(1,width)),palette_horizontal,0};
        SetScrollInfo(palette,SB_HORZ,&info,TRUE);ShowScrollBar(palette,SB_HORZ,overflow);reader_theme::refresh_scrollbars(palette);
        if(overflow)palette_height+=GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host));
        InvalidateRect(palette,nullptr,FALSE);return palette_height;
    }
    int measure(std::string_view value)const{
        const auto dc=GetDC(host);const auto old=SelectObject(dc,font);SIZE size{};const auto text=wide(value);
        GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);SelectObject(dc,old);ReleaseDC(host,dc);return size.cx;
    }
    struct InlineMetrics { int top=0,baseline=0,bottom=0; };
    InlineMetrics inline_metrics(HWND item)const{
        const auto dc=GetDC(host);const auto old=SelectObject(dc,font);TEXTMETRICW fallback{};
        GetTextMetricsW(dc,&fallback);SelectObject(dc,old);ReleaseDC(host,dc);
        InlineMetrics result{0,static_cast<int>(fallback.tmAscent),text_height()};
        Microsoft::WRL::ComPtr<IRichEditOle> ole;
        SendMessageW(item,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
        Microsoft::WRL::ComPtr<ITextDocument> document;Microsoft::WRL::ComPtr<ITextRange> range;
        if(!ole||FAILED(ole.As(&document))||document->Range(0,0,&range)!=S_OK||!range)return result;
        LONG x=0,top=0,baseline=0,bottom=0;constexpr LONG flags=tomStart|tomClientCoord|tomAllowOffClient;
        if(range->GetPoint(flags|TA_TOP|TA_LEFT,&x,&top)==S_OK&&
            range->GetPoint(flags|TA_BASELINE|TA_LEFT,&x,&baseline)==S_OK&&
            range->GetPoint(flags|TA_BOTTOM|TA_LEFT,&x,&bottom)==S_OK&&bottom>top)
            result={static_cast<int>(top),static_cast<int>(baseline),static_cast<int>(bottom)};
        return result;
    }
    void inline_rect(HWND item,int width,int height,bool hole){
        // EM_SETRECT uses unzoomed insets. Left/right and top/bottom must be
        // converted separately; scaling the entire right edge clips text.
        const auto natural=inline_metrics(item);const int glyph=std::max(1,natural.bottom-natural.top);
        const int horizontal_pad=hole?px(6):0,vertical_pad=std::max(0,(height-glyph)/2);
        RECT rect{MulDiv(horizontal_pad,100,text_scale),MulDiv(vertical_pad,100,text_scale),
            width-MulDiv(horizontal_pad,100,text_scale),height-MulDiv(vertical_pad,100,text_scale)};
        rect.right=std::max(rect.left+1,rect.right);rect.bottom=std::max(rect.top+1,rect.bottom);
        SendMessageW(item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&rect));
    }
    void layout_inline(int width){
        if(laying_out)return;laying_out=true;
        struct Guard{bool& flag;~Guard(){flag=false;}} guard{laying_out};
        const auto count=1+static_cast<std::size_t>(std::ranges::count(block.code_template,'\n'));
        gutter_width=std::max(px(14),measure(std::string(std::to_string(count).size(),'0'))+px(3));
        const int start=gutter_width+px(6),right=std::max(start+px(12),width-px(6));
        const int available=right-start,line=std::max(MulDiv(px(22),text_scale,100),text_height()+px(2));
        struct Placement{HWND item;int x,y,w,h,baseline;bool hole;};std::vector<Placement> positions;
        int x=start,y=px(8),ascent=0,descent=0,logical_top=y,logical_baseline=-1;
        std::size_t first=0;content_width=width;numbered_lines.clear();inline_baselines.clear();
        const auto finish_row=[&]{
            const int h=std::max(line,ascent+descent),extra=(h-ascent-descent)/2;
            for(auto n=first;n<positions.size();++n)positions[n].y=y+extra+ascent-positions[n].baseline;
            if(logical_baseline<0){
                if(first<positions.size())logical_baseline=y+extra+ascent;
                else {const auto dc=GetDC(host);const auto old=SelectObject(dc,font);TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);
                    SelectObject(dc,old);ReleaseDC(host,dc);logical_baseline=y+(h-tm.tmHeight)/2+tm.tmAscent;}
            }
            content_width=std::max(content_width,x+px(6));y+=h;x=start;ascent=descent=0;first=positions.size();
        };
        const auto finish_line=[&]{finish_row();numbered_lines.push_back({0,logical_top,gutter_width,y});
            inline_baselines.push_back(logical_baseline);logical_top=y;logical_baseline=-1;};
        for(const auto& piece:pieces){
            if(piece.newline){finish_line();continue;}
            std::string value=piece.source;
            if(piece.hole){const auto slot=static_cast<std::size_t>(std::ranges::find(holes,piece.item)-holes.begin());value=editors[slot]->document().text;}
            int w=piece.hole?std::max(MulDiv(px(42),text_scale,100),measure(value)+px(12)):static_cast<int>(token_size(piece.item,value).cx);
            if(wrap&&x>start&&x+w>right)finish_row();
            if(wrap)w=std::min(w,available);
            int h=piece.hole?line:text_height();
            SendMessageW(piece.item,EM_SETTARGETDEVICE,0,!piece.hole&&wrap?0:1);
            SetWindowPos(piece.item,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
            inline_rect(piece.item,w,h,piece.hole);
            if(!piece.hole&&wrap){SendMessageW(piece.item,EM_REQUESTRESIZE,0,0);
                if(const auto found=requested_heights.find(piece.item);found!=requested_heights.end())h=std::max(h,found->second);
                SetWindowPos(piece.item,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
                // Wrapped source keeps its first baseline, not a centered paragraph.
                RECT rect{0,0,w,h};SendMessageW(piece.item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&rect));}
            const auto metrics=inline_metrics(piece.item);h=std::max(h,metrics.bottom);
            positions.push_back({piece.item,x,y,w,h,metrics.baseline,piece.hole});
            ascent=std::max(ascent,metrics.baseline);descent=std::max(descent,h-metrics.baseline);x+=w;
        }
        finish_line();
        if(wrap){content_width=width;horizontal=0;}else horizontal=std::clamp(horizontal,0,std::max(0,content_width-width));
        for(const auto& p:positions){const int left=p.x-horizontal;
            SetWindowPos(p.item,nullptr,left,p.y,p.w,p.h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
            const auto clip=CreateRectRgn(std::clamp(gutter_width-left,0,p.w),0,p.w,p.h);
            if(clip&&p.hole){const auto rounded=CreateRoundRectRgn(0,0,p.w+1,p.h+1,px(6),px(6));
                if(rounded){CombineRgn(clip,clip,rounded,RGN_AND);DeleteObject(rounded);}}
            if(clip&&!SetWindowRgn(p.item,clip,FALSE))DeleteObject(clip);
            InvalidateRect(p.item,nullptr,FALSE);
        }
        const bool overflow=content_width>width;
        height=y+px(8)+(overflow?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_width-1),static_cast<UINT>(std::max(1,width)),horizontal,0};
        SetScrollInfo(host,SB_HORZ,&info,FALSE);ShowScrollBar(host,SB_HORZ,overflow);reader_theme::refresh_scrollbars(host);
        InvalidateRect(host,nullptr,FALSE);
    }
    void layout(int width){
        if(!drag()){layout_inline(width);return;}
        if(laying_out)return;laying_out=true;
        struct Guard{bool& value;~Guard(){value=false;}} guard{laying_out};
        const auto count=1+static_cast<std::size_t>(std::ranges::count(block.code_template,'\n'));
        gutter_width=std::max(px(18),measure(std::string(std::to_string(count).size(),'0'))+px(3)+1);
        const int start=gutter_width+px(10),right=std::max(start+px(12),width-px(10));
        const int available=std::max(px(12),right-start),line=drag()?std::max(px(22),text_height()):MulDiv(px(40),text_scale,100);
        struct Placement{HWND item;int x,y,width,height;};std::vector<Placement> placements;
        int x=start,y=px(8),row_height=line,logical_top=y;content_width=width;numbered_lines.clear();
        std::size_t row_begin=0;
        const auto center_row=[&]{for(auto i=row_begin;i<placements.size();++i)placements[i].y+=(row_height-placements[i].height)/2;row_begin=placements.size();};
        const auto finish_line=[&]{center_row();content_width=std::max(content_width,x+px(10));y+=row_height;
            numbered_lines.push_back({0,logical_top,gutter_width,y});logical_top=y;x=start;row_height=line;};
        for(const auto& p:pieces){
            if(p.newline){finish_line();continue;}
            auto value=p.source;if(p.hole){const auto i=static_cast<std::size_t>(std::ranges::find(holes,p.item)-holes.begin());
                value=drag()?(values[i]==empty_slot?"":block.choices[values[i]]):editors[i]->document().text;}
            const auto token=drag()||!p.hole?token_size(p.item,value):SIZE{};
            const bool empty=p.hole&&drag()&&value.empty();
            int cell=drag()?(empty?px(18):token.cx):p.hole?std::max(px(78),measure(value)+px(22)):token.cx;
            if(wrap&&x>start&&x+cell>right){center_row();y+=row_height;x=start;row_height=line;}
            if(wrap)cell=std::min(cell,available);
            int cell_height=drag()?(empty?px(22):token.cy):line;
            if(!p.hole&&!drag()){
                SendMessageW(p.item,EM_SETTARGETDEVICE,0,wrap?0:1);
                SetWindowPos(p.item,nullptr,0,0,cell,line,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
                SendMessageW(p.item,EM_REQUESTRESIZE,0,0);
                if(const auto measured=requested_heights.find(p.item);measured!=requested_heights.end())cell_height=std::max(line,measured->second+px(4));
            }
            placements.push_back({p.item,x,y,cell,cell_height});x+=cell;row_height=std::max(row_height,cell_height);
        }
        finish_line();
        if(wrap){content_width=width;horizontal=0;}
        else horizontal=std::clamp(horizontal,0,std::max(0,content_width-width));
        for(const auto& p:placements){
            const int left=p.x-horizontal;MoveWindow(p.item,left,p.y,p.width,p.height,TRUE);
            if(drag()){RECT area{};GetClientRect(p.item,&area);SendMessageW(p.item,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&area));}
            // The fixed number column must not be covered by an overflowing
            // piece while its source row is scrolled horizontally.
            const auto clip=CreateRectRgn(std::clamp(gutter_width-left,0,p.width),0,p.width,p.height);
            if(clip&&!SetWindowRgn(p.item,clip,TRUE))DeleteObject(clip);
        }
        if(palette&&!floating){y+=px(16);const auto bank=layout_palette(width);MoveWindow(palette,0,y,width,bank,TRUE);y+=bank;}
        height=y+px(8)+(content_width>width?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0);
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_width-1),static_cast<UINT>(std::max(1,width)),horizontal,0};
        SetScrollInfo(host,SB_HORZ,&info,TRUE);
        reader_theme::refresh_scrollbars(host);InvalidateRect(host,nullptr,FALSE);
    }
    void paint_numbers(HDC dc)const{
        if(!dc)return;const auto saved=SaveDC(dc);RECT bounds{};GetClientRect(host,&bounds);bounds.right=gutter_width;
        if(!numbered_lines.empty())bounds.bottom=std::min(bounds.bottom,numbered_lines.back().bottom);
        const auto colors=reader_theme::palette(host);
        const auto fill=CreateSolidBrush(colors.high_contrast?reader_theme::background(host):RGB(20,20,20));FillRect(dc,&bounds,fill);DeleteObject(fill);
        IntersectClipRect(dc,0,0,gutter_width,bounds.bottom);SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,colors.high_contrast?colors.text:RGB(111,119,130));SelectObject(dc,font);
        for(std::size_t i=0;i<numbered_lines.size();++i){auto rect=numbered_lines[i];rect.left=0;rect.right-=px(3);
            const auto label=std::to_wstring(i+1);
            if(!drag()&&i<inline_baselines.size()){
                SetTextAlign(dc,TA_LEFT|TA_BASELINE);TextOutW(dc,0,inline_baselines[i],label.c_str(),static_cast<int>(label.size()));
            }else DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);}
        RestoreDC(dc,saved);
    }
    void focus_blank(HWND from,bool backwards){
        const auto current=static_cast<std::size_t>(std::ranges::find(holes,from)-holes.begin());
        if(current>=holes.size())return;
        const auto target=holes[(current+holes.size()+(backwards?holes.size()-1:1))%holes.size()];
        if(!IsWindowEnabled(target))return;
        SetFocus(target);SendMessageW(target,EM_SCROLLCARET,0,0);
        RECT area{},bounds{};GetClientRect(host,&area);GetWindowRect(target,&bounds);MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&bounds),2);
        if(bounds.left<gutter_width)horizontal-=gutter_width-bounds.left;
        else if(bounds.right>area.right)horizontal+=bounds.right-area.right;
        layout(area.right);
    }
    void relayout(){RECT area{};GetClientRect(host,&area);layout(area.right);if(floating){GetClientRect(palette,&area);layout_palette(area.right);}}
    void assign(std::size_t slot,std::size_t choice){
        if(!enabled||slot>=holes.size()||(choice!=empty_slot&&choice>=block.choices.size())||values[slot]==choice)return;
        updating=true;
        // The identity is the physical token, never its text. A filled-slot
        // drop swaps both tokens; a bank drop returns the displaced one there.
        const auto displaced=values[slot];const auto origin=choice==empty_slot?values.end():std::ranges::find(values,choice);
        if(origin!=values.end())*origin=displaced;
        else if(displaced!=empty_slot)bank_order.push_back(displaced);
        if(choice!=empty_slot)std::erase(bank_order,choice);
        values[slot]=choice;refresh_slots();updating=false;relayout();if(changed)changed();
    }
    void activate(std::size_t choice){if(!enabled||choice>=cards.size())return;
        const auto placed=std::ranges::find(values,choice);
        if(placed!=values.end())assign(static_cast<std::size_t>(placed-values.begin()),empty_slot);
        else if(const auto blank=std::ranges::find(values,empty_slot);blank!=values.end())assign(static_cast<std::size_t>(blank-values.begin()),choice);
    }
    void release(){const auto item=pressed_item;pressed_item=nullptr;dragged=empty_slot;dragging=false;if(item&&GetCapture()==item)ReleaseCapture();}
    void drop(std::size_t choice,POINT point){
        std::size_t nearest=empty_slot;long long distance=std::numeric_limits<long long>::max();
        for(std::size_t i=0;i<holes.size();++i){RECT bounds{};GetWindowRect(holes[i],&bounds);const auto dx=point.x-(bounds.left+bounds.right)/2,dy=point.y-(bounds.top+bounds.bottom)/2;
            const auto squared=static_cast<long long>(dx)*dx+static_cast<long long>(dy)*dy;if(squared<distance){distance=squared;nearest=i;}}
        if(nearest!=empty_slot&&distance<=static_cast<long long>(px(52))*px(52)){assign(nearest,choice);return;}
        RECT bank{};GetWindowRect(palette,&bank);if(PtInRect(&bank,point)){
            const auto previous=std::ranges::find(values,choice);if(previous!=values.end())assign(static_cast<std::size_t>(previous-values.begin()),empty_slot);}
    }
    static LRESULT CALLBACK label_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,label_proc,4);return DefSubclassProc(item,message,wp,lp);}
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_MOUSEMOVE||message==WM_LBUTTONDBLCLK){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};MapWindowPoints(item,GetParent(item),&p,1);return SendMessageW(GetParent(item),message,wp,MAKELPARAM(p.x,p.y));}
        if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL)return SendMessageW(GetParent(item),message,wp,lp);
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK child_proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);const int id=GetDlgCtrlID(item);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,child_proc,1);return DefSubclassProc(item,message,wparam,lparam);}
        if(!self.drag()&&self.holes.size()>1&&id>=100&&id<1000){
            if(message==WM_GETDLGCODE)return DefSubclassProc(item,message,wparam,lparam)|DLGC_WANTTAB;
            if(message==WM_KEYDOWN&&wparam==VK_TAB&&!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)){
                if(!(lparam&(LPARAM{1}<<30)))self.focus_blank(item,(GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
            if((message==WM_CHAR&&wparam==L'\t')||(message==WM_KEYUP&&wparam==VK_TAB))return 0;
        }
        if(!self.drag()&&id>=100&&id<1000&&(message==WM_PAINT||message==WM_PRINTCLIENT)){
            const auto result=DefSubclassProc(item,message,wparam,lparam);
            const auto dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wparam):GetDC(item);
            self.paint_slot(item,dc);if(message==WM_PAINT&&dc)ReleaseDC(item,dc);return result;
        }
        if(message==WM_ENABLE&&id>=100&&id<1000&&!wparam&&!reader_theme::palette(item).high_contrast){
            SendMessageW(item,EM_SETBKGNDCOLOR,0,reader_theme::background(item));InvalidateRect(item,nullptr,FALSE);return 0;
        }
        if(self.drag()){
            if(message==WM_ENABLE&&id>=100&&id<1000&&!wparam&&!reader_theme::palette(item).high_contrast){
                // Keep the actual WS_DISABLED state while retaining RichEdit's
                // explicit dark background; this display is already read-only.
                SendMessageW(item,EM_SETBKGNDCOLOR,0,reader_theme::background(item));InvalidateRect(item,nullptr,TRUE);return 0;}
            if(message==WM_PAINT||message==WM_PRINTCLIENT){const auto result=DefSubclassProc(item,message,wparam,lparam);
                if(id>=100&&id<1000){const auto dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(wparam):GetDC(item);self.paint_slot(item,dc);if(message!=WM_PRINTCLIENT)ReleaseDC(item,dc);}return result;}
            if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL){self.release();return SendMessageW(id>=2000?self.palette:self.host,message,wparam,lparam);}
            if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE){self.release();return 0;}
            if(!self.enabled)return DefSubclassProc(item,message,wparam,lparam);
            if(message==WM_LBUTTONDOWN){self.release();const auto choice=id>=2000?static_cast<std::size_t>(id-2000):self.values[static_cast<std::size_t>(id-100)];
                if(choice==empty_slot)return 0;self.dragged=choice;self.pressed_item=item;self.pressed=GetTickCount64();self.down={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ClientToScreen(item,&self.down);SetFocus(item);SetCapture(item);return 0;}
            if(message==WM_MOUSEMOVE&&self.pressed_item==item&&(wparam&MK_LBUTTON)){POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ClientToScreen(item,&point);
                if(GetTickCount64()-self.pressed>=20&&(std::abs(point.x-self.down.x)>=self.px(4)||std::abs(point.y-self.down.y)>=self.px(4)))self.dragging=true;return 0;}
            if(message==WM_LBUTTONUP){if(self.pressed_item==item){const auto choice=self.dragged;const auto dragging=self.dragging;POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ClientToScreen(item,&point);
                    const bool click=std::abs(point.x-self.down.x)<self.px(4)&&std::abs(point.y-self.down.y)<self.px(4);self.release();if(dragging)self.drop(choice,point);else if(click)self.activate(choice);}return 0;}
            if(message==WM_LBUTTONDBLCLK)return 0;
            if(message==WM_KEYDOWN&&id>=100&&id<1000&&(wparam==VK_DELETE||wparam==VK_BACK)){self.assign(static_cast<std::size_t>(id-100),empty_slot);return 0;}
        }
        return DefSubclassProc(item,message,wparam,lparam);
    }
    static LRESULT CALLBACK palette_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,palette_proc,3);return DefSubclassProc(item,message,wp,lp);}
        if(message==WM_DRAWITEM||message==WM_COMMAND||message==WM_NOTIFY)return SendMessageW(self.host,message,wp,lp);
        LRESULT result{};if(reader_theme::control_color(message,wp,lp,result))return result;
        if(message==WM_SIZE){self.layout_palette(LOWORD(lp));return 0;}
        if(message==WM_HSCROLL||message==WM_MOUSEHWHEEL){RECT bounds{};GetClientRect(item,&bounds);SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(item,SB_HORZ,&info);
            if(message==WM_MOUSEHWHEEL)self.palette_horizontal+=GET_WHEEL_DELTA_WPARAM(wp)*self.px(48)/WHEEL_DELTA;
            else switch(LOWORD(wp)){case SB_LINELEFT:self.palette_horizontal-=self.px(24);break;case SB_LINERIGHT:self.palette_horizontal+=self.px(24);break;case SB_PAGELEFT:self.palette_horizontal-=bounds.right;break;case SB_PAGERIGHT:self.palette_horizontal+=bounds.right;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:self.palette_horizontal=info.nTrackPos;break;default:break;}
            self.layout_palette(bounds.right);return 0;}
        if(message==WM_MOUSEWHEEL){self.release();return SendMessageW(GetParent(self.host),message,wp,lp);}
        if(message==WM_ERASEBKGND||message==WM_PAINT||message==WM_PRINTCLIENT){PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(item,&paint):reinterpret_cast<HDC>(wp);
            const auto color=reader_theme::background(item);self.frame(item,dc,10,color,color,true);if(message==WM_PAINT)EndPaint(item,&paint);return message==WM_ERASEBKGND?TRUE:0;}
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK background_proc(HWND item,UINT message,WPARAM keys,LPARAM point,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY)RemoveWindowSubclass(item,background_proc,2);
        // Only an actual background hit reaches this host; holes, cards and
        // selectable source pieces retain their existing input/selection.
        if(self.lesson_gesture)if(const auto result=self.lesson_gesture(item,message,keys,point))return *result;
        return DefSubclassProc(item,message,keys,point);
    }
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);self->host=item;SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(item,message,wparam,lparam);
        LRESULT result{};if(reader_theme::control_color(message,wparam,lparam,result))return result;
        if(message==WM_DRAWITEM){const auto& draw=*reinterpret_cast<DRAWITEMSTRUCT*>(lparam);if(self->paint_card(draw)||reader_theme::draw_button(draw))return TRUE;}
        if(message==WM_NOTIFY){const auto note=reinterpret_cast<NMHDR*>(lparam);
            if(note&&note->code==EN_REQUESTRESIZE){const auto request=reinterpret_cast<REQRESIZE*>(lparam);
                self->requested_heights[note->hwndFrom]=request->rc.bottom-request->rc.top;return 0;}}
        if(message==WM_SIZE){self->layout(LOWORD(lparam));return 0;}
        if(message==WM_HSCROLL||message==WM_MOUSEHWHEEL){
            RECT r{};GetClientRect(item,&r);SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(item,SB_HORZ,&info);
            if(message==WM_MOUSEHWHEEL)self->horizontal+=GET_WHEEL_DELTA_WPARAM(wparam)*self->px(48)/WHEEL_DELTA;
            else switch(LOWORD(wparam)){case SB_LINELEFT:self->horizontal-=self->px(24);break;case SB_LINERIGHT:self->horizontal+=self->px(24);break;
                case SB_PAGELEFT:self->horizontal-=r.right;break;case SB_PAGERIGHT:self->horizontal+=r.right;break;
                case SB_THUMBTRACK:case SB_THUMBPOSITION:self->horizontal=info.nTrackPos;break;default:break;}
            self->layout(r.right);return 0;
        }
        if(message==WM_MOUSEWHEEL){self->release();return SendMessageW(GetParent(item),message,wparam,lparam);}
        if(message==WM_ERASEBKGND){RECT r{};GetClientRect(item,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,reader_theme::brush(reader_theme::Surface::card));return TRUE;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);self->paint_numbers(dc);EndPaint(item,&paint);return 0;}
        if(message==WM_PRINTCLIENT){self->paint_numbers(reinterpret_cast<HDC>(wparam));return 0;}
        if(message==WM_COMMAND&&!self->updating){
            const int id=LOWORD(wparam);if(HIWORD(wparam)==EN_CHANGE&&id>=100&&id<1000&&!self->drag()){
                const auto i=static_cast<std::size_t>(id-100);if(i<self->editors.size()){self->updating=true;self->editors[i]->capture_change();style_cpp(self->holes[i]);self->updating=false;self->relayout();if(self->changed)self->changed();}
                return 0;
            }
            if(HIWORD(wparam)==BN_CLICKED&&id>=2000&&id<2000+static_cast<int>(self->cards.size())){self->activate(static_cast<std::size_t>(id-2000));return 0;}
        }
        return DefWindowProcW(item,message,wparam,lparam);
    }
};
CppFillView::CppFillView(HWND p,const curriculum::Block& b,std::string_view saved,std::function<void()> c,std::function<void()> s,
    std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)> gesture)
    :impl_(std::make_unique<Impl>(p,b,saved,std::move(c),std::move(s),std::move(gesture))){}
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
void CppFillView::set_completed(bool value){
    impl_->completed=value;for(const auto item:impl_->holes)reader_theme::completed_control(item,value);
    InvalidateRect(impl_->host,nullptr,FALSE);
}
void CppFillView::set_enabled(bool value){impl_->release();impl_->enabled=value;for(auto h:impl_->holes)EnableWindow(h,value);for(auto h:impl_->cards)EnableWindow(h,value);}
void CppFillView::set_wrap(bool value){if(impl_->wrap==value)return;impl_->wrap=value;RECT bounds{};GetClientRect(impl_->host,&bounds);impl_->layout(bounds.right);}
void CppFillView::scroll_horizontal_to(int offset){impl_->horizontal=offset;RECT bounds{};GetClientRect(impl_->host,&bounds);impl_->layout(bounds.right);}
void CppFillView::set_text_scale(int percentage){
    percentage=std::clamp(percentage,75,150);if(impl_->text_scale==percentage)return;impl_->text_scale=percentage;
    const auto next=CreateFontW(-MulDiv(18*percentage,static_cast<int>(GetDpiForWindow(impl_->host)),96*100),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,reader_theme::code_font_face());
    if(!next)throw std::runtime_error("Cannot scale code-hole text");
    struct Scale{int percentage;HFONT font;};Scale scale{percentage,next};
    const auto children=[](HWND item,LPARAM data)->BOOL{const auto& scale=*reinterpret_cast<Scale*>(data);wchar_t name[32]{};GetClassNameW(item,name,32);
        if(_wcsnicmp(name,L"RichEdit",8)==0)SendMessageW(item,EM_SETZOOM,scale.percentage,100);else SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(scale.font),FALSE);return TRUE;};
    EnumChildWindows(impl_->host,children,reinterpret_cast<LPARAM>(&scale));
    if(impl_->floating)EnumChildWindows(impl_->palette,children,reinterpret_cast<LPARAM>(&scale));
    if(impl_->font)DeleteObject(impl_->font);impl_->font=next;RECT bounds{};GetClientRect(impl_->host,&bounds);impl_->layout(bounds.right);
}
HWND CppFillView::palette_window()const{return impl_->palette;}
void CppFillView::set_palette_host(HWND parent){if(!impl_->palette)return;impl_->release();const auto target=parent?parent:impl_->host;
    reader_theme::set_accent(impl_->palette,std::nullopt);impl_->floating=parent!=nullptr;SetParent(impl_->palette,target);impl_->relayout();}
int CppFillView::palette_height_for_width(int width){return impl_->layout_palette(std::max(1,width));}
void CppFillView::reset(){set_completed(false);impl_->release();impl_->updating=true;for(const auto& e:impl_->editors)e->reset(windows::EditorDocument{});
    if(impl_->drag()){std::ranges::fill(impl_->values,empty_slot);impl_->bank_order.resize(impl_->cards.size());std::iota(impl_->bank_order.begin(),impl_->bank_order.end(),0);impl_->refresh_slots();}impl_->updating=false;impl_->relayout();}
}
