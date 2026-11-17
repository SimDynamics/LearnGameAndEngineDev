#include "presentation/windows/simulation_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "core/simulation.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lgaed::curriculum;
using namespace lgaed::presentation;
constexpr int scenario_id=103,array_id=105,step_id=106,explanation_id=107,variables_id=108,
    output_id=110,previous_id=111,next_id=112,reset_id=113,input_id=115,apply_id=116,restore_id=117,error_id=118,
    input_format_id=120,step_selector_id=121,group_first_id=200;
std::filesystem::path failure_captures;
void capture(HWND window,const std::filesystem::path& path);
void fitted_text(SimulationView& view);
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void pump(HWND dialog=nullptr){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT&&(!dialog||!IsDialogMessageW(dialog,&message))){TranslateMessage(&message);DispatchMessageW(&message);}}
struct Window {HWND value=nullptr;~Window(){if(IsWindow(value))DestroyWindow(value);}};
std::vector<SimulationValue> numbers(std::initializer_list<std::int64_t> values){std::vector<SimulationValue> result;for(const auto value:values)result.emplace_back(value);return result;}
SimulationSpec fixture(SimulationViewMode mode) {
    SimulationSpec spec;spec.view=mode;spec.editable=true;
    spec.title="Recorrer un array por grupos";
    spec.description="Cada casilla conserva su índice. El intervalo resaltado empieza en inicio y termina antes de fin.";
    spec.values=numbers({10,3,-8,3,3,3});
    spec.labels={"Datos ordenados","inicio","fin","frecuencia","máxima","mayores","Salida"};
    spec.explanations={"El array tiene {size} datos. Todavía no se ha recorrido ningún grupo.",
        "El grupo {group} empieza en el índice {start} con el valor {value}.",
        "El valor {value} continúa. El límite fin pasa a {end} y la frecuencia es {frequency}.",
        "El grupo de {value} tiene frecuencia {frequency}; supera la máxima anterior. Los candidatos son {leaders}.",
        "El grupo de {value} empata con la máxima {maximum}. Se conservan {leaders}.",
        "El grupo de {value} tiene frecuencia {frequency}. La máxima sigue siendo {maximum}.",
        "El recorrido termina en el límite {end}. La máxima es {maximum} y los mayores son {leaders}.",
        "El array está vacío; no hay grupos ni candidatos."};
    spec.output_template="{value}:{frequency}";
    spec.scenarios={{"tie","Empate entre dos grupos",numbers({9,4,9,4})},{"empty","Array vacío",{}}};
    return spec;
}
void resize(SimulationView& view,int width) {
    const auto height=view.height_for_width(width);check(height>0,"The responsive view has no height");
    check(view.height_for_width(width)==height,"Repeated measurement changed the simulation height");
    MoveWindow(view.window(),0,0,width,height,FALSE);pump();
    check(view.height_for_width(width)==height,"Applying the measured size changed the simulation height");
    RECT client{};GetClientRect(view.window(),&client);check(client.bottom-client.top==height,"The native host did not retain its measured height");
    fitted_text(view);
}
void click(SimulationView& view,int id) {
    const auto button=GetDlgItem(view.window(),id);check(button&&IsWindowEnabled(button),"A required simulation button is absent or disabled");
    SendMessageW(button,BM_CLICK,0,0);pump();
}
// The fixture has six ordered cells in one row at the tested wide width.
// Exercise the real Win32 canvas without injecting desktop-wide input.
void click_group_cell(SimulationView& view,int cell,bool drag=false,bool modified=false) {
    const auto canvas=GetDlgItem(view.window(),array_id);check(canvas,"Array canvas is missing");
    RECT area{};GetClientRect(canvas,&area);
    check(area.right>=700&&area.bottom>80,"Expected one visible row of six cells");
    const int x=(cell*2+1)*area.right/12;
    const int y=MulDiv(70,static_cast<int>(GetDpiForWindow(canvas)),96);
    check(y<area.bottom,"Selected cell is outside the rendered canvas");
    const auto keys=static_cast<WPARAM>(MK_LBUTTON|(modified?MK_CONTROL:0));
    SendMessageW(canvas,WM_LBUTTONDOWN,keys,MAKELPARAM(x,y));
    if(drag)SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+45,y));
    SendMessageW(canvas,WM_LBUTTONUP,modified?MK_CONTROL:0,MAKELPARAM(x,y));pump();
}
std::string native_text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));return utf8(value);
}
std::string text(SimulationView& view,int id){const auto window=GetDlgItem(view.window(),id);check(window,"A simulation control is missing");
    wchar_t type[48]{};GetClassNameW(window,type,48);return std::wstring_view(type)==MSFTEDIT_CLASS?edit_text(window):native_text(window);}
int window_height(HWND window){RECT area{};GetClientRect(window,&area);return static_cast<int>(area.bottom-area.top);}
void key(HWND dialog,WPARAM value,bool control=false) {
    std::array<BYTE,256> previous{};check(GetKeyboardState(previous.data()),"Cannot preserve the test-thread keyboard state");
    struct RestoreKeys {std::array<BYTE,256>& keys;~RestoreKeys(){SetKeyboardState(keys.data());}} restore{previous};
    auto keys=previous;
    for(const auto modifier:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})keys[modifier]=0;
    if(control)keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;
    check(SetKeyboardState(keys.data()),"Cannot set the isolated test-thread keyboard modifiers");
    const auto target=GetFocus();check(target,"Keyboard input has no actual focused window");
    check(PostMessageW(target,WM_KEYDOWN,value,1)&&PostMessageW(target,WM_KEYUP,value,(LPARAM{1}<<30)|(LPARAM{1}<<31)|1),
        "Cannot queue native keyboard input");pump(dialog);
}
int rich_edit_scale(HWND edit) {
    int numerator=0,denominator=0;
    SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
    return denominator?MulDiv(numerator,100,denominator):100;
}
void control_wheel(HWND host,HWND edit,int delta) {
    {
        std::array<BYTE,256> previous{};
        check(GetKeyboardState(previous.data()),"Cannot preserve the test-thread keyboard state for Ctrl+wheel");
        struct RestoreKeys {std::array<BYTE,256>& keys;~RestoreKeys(){SetKeyboardState(keys.data());}} restore{previous};
        auto keys=previous;
        for(const auto key:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})keys[key]=0;
        keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;
        check(SetKeyboardState(keys.data()),"Cannot set the isolated test-thread Ctrl modifier");
        RECT client{};GetClientRect(edit,&client);POINT point{client.right/2,client.bottom/2};ClientToScreen(edit,&point);
        // Exercise RichEdit's real local gesture without desktop-global input.
        SendMessageW(edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,static_cast<WORD>(delta)),MAKELPARAM(point.x,point.y));
    }
    pump(host);
}
struct RichEditSample {
    CHARFORMAT2W base{},first{};
    PARAFORMAT2 paragraph{};
};
RichEditSample rich_edit_sample(HWND edit) {
    RichEditSample result;result.base.cbSize=sizeof(result.base);result.first.cbSize=sizeof(result.first);
    result.paragraph.cbSize=sizeof(result.paragraph);
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&result.base));
    CHARRANGE before{},sample{0,1};POINT scroll{};
    SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&sample));
    SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&result.first));
    SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&result.paragraph));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));
    SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
    return result;
}
std::map<int,std::string> rich_edit_contents(SimulationView& view) {
    std::map<int,std::string> result;
    for(auto child=GetWindow(view.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        wchar_t type[48]{};GetClassNameW(child,type,48);
        if((GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)&&std::wstring_view(type)==MSFTEDIT_CLASS)
            result.emplace(GetDlgCtrlID(child),edit_text(child));
    }
    return result;
}
std::map<int,std::string> rich_edit_fonts(SimulationView& view,int expected_scale) {
    std::map<int,std::string> result;
    for(auto child=GetWindow(view.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        wchar_t type[48]{};GetClassNameW(child,type,48);
        if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)||std::wstring_view(type)!=MSFTEDIT_CLASS)continue;
        check(rich_edit_scale(child)==expected_scale,"A visible RichEdit has an unexpected text scale");
        const auto sample=rich_edit_sample(child);
        result.emplace(GetDlgCtrlID(child),utf8(sample.base.szFaceName)+":"+std::to_string(sample.base.yHeight)+";"+
            utf8(sample.first.szFaceName)+":"+std::to_string(sample.first.yHeight));
    }
    return result;
}
std::string name(HWND window) {
    Microsoft::WRL::ComPtr<IAccessible> accessible;
    check(SUCCEEDED(AccessibleObjectFromWindow(window,OBJID_CLIENT,IID_PPV_ARGS(accessible.GetAddressOf()))),"Cannot read the real accessible object");
    VARIANT child{};child.vt=VT_I4;child.lVal=CHILDID_SELF;BSTR value=nullptr;
    check(SUCCEEDED(accessible->get_accName(child,&value))&&value,"A native control has no accessible name");
    const auto result=utf8(value);SysFreeString(value);return result;
}
void bounds(SimulationView& view,const char* context="responsive layout") {
    RECT area{};GetClientRect(view.window(),&area);
    for(auto child=GetWindow(view.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))continue;
        RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&box),2);
        if(box.left<0||box.right>area.right||box.top<0||box.bottom>area.bottom){wchar_t type[96]{};GetClassNameW(child,type,96);
            const auto rectangle=[](const RECT& value){return "["+std::to_string(value.left)+","+std::to_string(value.top)+","+
                std::to_string(value.right)+","+std::to_string(value.bottom)+"]";};
            const auto detail=std::string(context)+": control "+std::to_string(GetDlgCtrlID(child))+" ("+utf8(type)+") "+rectangle(box)+
                " escaped viewport "+rectangle(area);
            if(!failure_captures.empty())try{capture(view.window(),failure_captures/"failed-responsive-layout.bmp");}
                catch(const std::exception& error){std::cerr<<"Cannot capture failure: "<<error.what()<<'\n';}
            throw std::runtime_error(detail);}
        check(box.right>box.left&&box.bottom>box.top,"A visible simulation control has zero size");
    }
}
void plain_ink(HWND window) {
    CHARFORMAT2W format{};format.cbSize=sizeof(format);
    SendMessageW(window,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
    check((format.dwMask&CFM_COLOR)&&!(format.dwEffects&CFE_AUTOCOLOR)&&format.crTextColor==reader_theme::foreground(window),
        "The plain input/output default ink does not use the current reader palette");
    CHARRANGE before{},sample{0,1};SendMessageW(window,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&sample));format={};format.cbSize=sizeof(format);
    SendMessageW(window,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    SendMessageW(window,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));
    check((format.dwMask&CFM_COLOR)&&!(format.dwEffects&CFE_AUTOCOLOR)&&format.crTextColor==reader_theme::foreground(window),
        "Existing plain input/output text retained unreadable ink");
}
struct HeightRequest {HWND target=nullptr;int required=-1;};
LRESULT CALLBACK height_request_probe(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
    auto* probe=reinterpret_cast<HeightRequest*>(raw);
    if(message==WM_NOTIFY&&lp){const auto* header=reinterpret_cast<NMHDR*>(lp);
        if(header->hwndFrom==probe->target&&header->code==EN_REQUESTRESIZE){const auto* request=reinterpret_cast<REQRESIZE*>(lp);
            probe->required=static_cast<int>(request->rc.bottom-request->rc.top);}}
    return DefSubclassProc(window,message,wp,lp);
}
void fitted_text(SimulationView& view) {
    for(auto child=GetWindow(view.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        const auto style=GetWindowLongPtrW(child,GWL_STYLE);wchar_t type[48]{};GetClassNameW(child,type,48);
        if(!(style&WS_VISIBLE)||std::wstring_view(type)!=MSFTEDIT_CLASS||GetDlgCtrlID(child)==input_id)continue;
        HeightRequest probe{child};constexpr UINT_PTR probe_id=0x53494d48;
        check(SetWindowSubclass(view.window(),height_request_probe,probe_id,reinterpret_cast<DWORD_PTR>(&probe)),"Cannot inspect native text height");
        SendMessageW(child,EM_REQUESTRESIZE,0,0);RemoveWindowSubclass(view.window(),height_request_probe,probe_id);
        check(probe.required>=0,"The visible RichEdit did not provide its native required height");
        const bool bounded=(GetDlgCtrlID(child)==explanation_id||GetDlgCtrlID(child)==output_id)&&(style&WS_TABSTOP);
        if(!bounded&&probe.required>window_height(child))throw std::runtime_error("RichEdit "+std::to_string(GetDlgCtrlID(child))+
            " needs "+std::to_string(probe.required)+" pixels but has "+std::to_string(window_height(child))+"; text would be clipped");
    }
}
std::map<int,std::string> layout_snapshot(SimulationView& view) {
    std::map<int,std::string> result;
    const auto rectangle=[](const RECT& value){return "["+std::to_string(value.left)+","+std::to_string(value.top)+","+
        std::to_string(value.right)+","+std::to_string(value.bottom)+"]";};
    for(auto child=GetWindow(view.window(),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        const auto style=GetWindowLongPtrW(child,GWL_STYLE);if(!(style&WS_VISIBLE))continue;
        RECT box{};GetWindowRect(child,&box);MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&box),2);
        auto detail="rect="+rectangle(box);
        wchar_t type[48]{};GetClassNameW(child,type,48);
        if(std::wstring_view(type)==MSFTEDIT_CLASS){
            const auto sample=rich_edit_sample(child);const auto& base=sample.base;const auto& first=sample.first;const auto& paragraph=sample.paragraph;
            int numerator=0,denominator=0;SendMessageW(child,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            RECT client{},format{};GetClientRect(child,&client);SendMessageW(child,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
            const auto lines=SendMessageW(child,EM_GETLINECOUNT,0,0);
            GETTEXTLENGTHEX length{GTL_NUMCHARS|GTL_PRECISE,1200};
            const auto characters=SendMessageW(child,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
            detail+=" client="+rectangle(client)+" format="+rectangle(format)+" lines="+std::to_string(lines)+" chars="+std::to_string(characters)+
                " baseTwips="+std::to_string(base.yHeight)+
                " firstTwips="+std::to_string(first.yHeight)+" face="+utf8(first.szFaceName)+
                " zoom="+std::to_string(numerator)+"/"+std::to_string(denominator)+" effects="+std::to_string(first.dwEffects)+
                " spacing="+std::to_string(first.sSpacing)+" weight="+std::to_string(first.wWeight)+" offset="+std::to_string(first.yOffset)+
                " paragraph=[mask:"+std::to_string(paragraph.dwMask)+",alignment:"+std::to_string(paragraph.wAlignment)+
                ",left:"+std::to_string(paragraph.dxStartIndent)+",right:"+std::to_string(paragraph.dxRightIndent)+
                ",offset:"+std::to_string(paragraph.dxOffset)+",before:"+std::to_string(paragraph.dySpaceBefore)+
                ",after:"+std::to_string(paragraph.dySpaceAfter)+",line:"+std::to_string(paragraph.dyLineSpacing)+
                ",rule:"+std::to_string(paragraph.bLineSpacingRule)+"]";
            for(const auto axis:{SB_VERT,SB_HORZ}){SCROLLINFO scroll{sizeof(scroll),SIF_ALL};const auto available=GetScrollInfo(child,axis,&scroll);
                detail+=std::string(axis==SB_VERT?" vertical=[":" horizontal=[")+"style:"+
                    std::to_string(bool(style&(axis==SB_VERT?WS_VSCROLL:WS_HSCROLL)))+",available:"+std::to_string(bool(available))+
                    ",min:"+std::to_string(scroll.nMin)+",max:"+std::to_string(scroll.nMax)+",page:"+std::to_string(scroll.nPage)+
                    ",pos:"+std::to_string(scroll.nPos)+",track:"+std::to_string(scroll.nTrackPos)+"]";}
            for(auto line=std::max<LRESULT>(0,lines-2);line<lines;++line){
                const auto at=SendMessageW(child,EM_LINEINDEX,static_cast<WPARAM>(line),0);
                const auto count=at>=0?SendMessageW(child,EM_LINELENGTH,static_cast<WPARAM>(at),0):-1;
                detail+=" line["+std::to_string(line)+"]="+std::to_string(at)+":"+std::to_string(count);}
        }
        result.emplace(GetDlgCtrlID(child),std::move(detail));
    }
    return result;
}
void report_layout_difference(const char* stage,const std::map<int,std::string>& before,const std::map<int,std::string>& after) {
    for(const auto& [id,value]:after){const auto previous=before.find(id);
        if(previous==before.end()||previous->second!=value)std::cerr<<"ZOOM "<<stage<<" control "<<id<<" before: "<<
            (previous==before.end()?"absent":previous->second)<<"; after: "<<value<<'\n';}
}
struct ScrollState {
    CHARRANGE selection{};POINT position{};SCROLLINFO vertical{sizeof(SCROLLINFO),SIF_ALL};
    LRESULT first=0,lines=0,characters=0,dialog_flags=0;LONG_PTR style=0;bool has_range=false;
};
ScrollState scroll_state(HWND edit) {
    ScrollState result;SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&result.selection));
    SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&result.position));
    result.first=SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0);result.lines=SendMessageW(edit,EM_GETLINECOUNT,0,0);
    GETTEXTLENGTHEX length{GTL_NUMCHARS|GTL_PRECISE,1200};
    result.characters=SendMessageW(edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    result.has_range=GetScrollInfo(edit,SB_VERT,&result.vertical)!=FALSE;result.style=GetWindowLongPtrW(edit,GWL_STYLE);
    MSG next{};next.hwnd=edit;next.message=WM_KEYDOWN;next.wParam=VK_NEXT;next.lParam=1;
    result.dialog_flags=SendMessageW(edit,WM_GETDLGCODE,VK_NEXT,reinterpret_cast<LPARAM>(&next));return result;
}
std::string scroll_description(const ScrollState& state) {
    return "selection=["+std::to_string(state.selection.cpMin)+","+std::to_string(state.selection.cpMax)+"] first="+
        std::to_string(state.first)+" scroll=["+std::to_string(state.position.x)+","+std::to_string(state.position.y)+"] lines="+
        std::to_string(state.lines)+" chars="+std::to_string(state.characters)+" range=[available:"+std::to_string(state.has_range)+
        ",min:"+std::to_string(state.vertical.nMin)+",max:"+std::to_string(state.vertical.nMax)+",page:"+std::to_string(state.vertical.nPage)+
        ",pos:"+std::to_string(state.vertical.nPos)+"] style="+std::to_string(state.style)+" nextFlags="+std::to_string(state.dialog_flags);
}
struct KeyboardDelivery {int home=0,page_down=0,page_up=0;};
LRESULT CALLBACK keyboard_delivery_probe(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
    auto* delivery=reinterpret_cast<KeyboardDelivery*>(raw);
    if(message==WM_KEYDOWN){if(wp==VK_HOME)++delivery->home;else if(wp==VK_NEXT)++delivery->page_down;else if(wp==VK_PRIOR)++delivery->page_up;}
    return DefSubclassProc(window,message,wp,lp);
}
struct KeyboardProbe {
    HWND window=nullptr;KeyboardDelivery delivered;static constexpr UINT_PTR id=0x53494d4b;
    explicit KeyboardProbe(HWND target):window(target){
        check(SetWindowSubclass(window,keyboard_delivery_probe,id,reinterpret_cast<DWORD_PTR>(&delivered)),"Cannot inspect native keyboard delivery");}
    ~KeyboardProbe(){if(IsWindow(window))RemoveWindowSubclass(window,keyboard_delivery_probe,id);}
};
void capture(HWND window,const std::filesystem::path& path) {
    RECT area{};GetClientRect(window,&area);const int width=area.right,height=area.bottom;
    check(width>0&&height>0&&height<20000,"Invalid native capture dimensions");
    const auto screen=GetDC(window);const auto dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    check(dc&&bitmap&&bits,"Cannot allocate a real Windows UI capture");const auto previous=SelectObject(dc,bitmap);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    GdiFlush();const auto length=static_cast<DWORD>(width*height*4);
    BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+length;
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    file.write(static_cast<const char*>(bits),length);check(bool(file),"Cannot save native simulation capture");
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,screen);
}
void group_colors_are_visible(HWND canvas) {
    if(reader_theme::palette(canvas).high_contrast)return;
    RECT bounds{};GetClientRect(canvas,&bounds);const auto screen=GetDC(canvas),dc=CreateCompatibleDC(screen);
    const auto bitmap=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);check(dc&&bitmap,"Cannot inspect real array colors");
    const auto old=SelectObject(dc,bitmap);SendMessageW(canvas,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
    const auto dpi=static_cast<int>(GetDpiForWindow(canvas));const auto inset=MulDiv(8,dpi,96),y=MulDiv(30,dpi,96);
    const auto first=GetPixel(dc,inset,y),second=GetPixel(dc,bounds.right/6+inset,y),third=GetPixel(dc,bounds.right/3+inset,y);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(canvas,screen);
    check(first!=CLR_INVALID&&first!=second&&second==third,"Array mode did not visibly distinguish equal-value groups by color");
}
void navigation(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::walkthrough);const auto authored=spec;const auto trace=build_simulation_trace(spec);
    int changes=0,layouts=0;SimulationView view(host,spec,"es",{[&]{++changes;},[&]{++layouts;}});
    resize(view,1050);bounds(view,"initial walkthrough");plain_ink(GetDlgItem(view.window(),input_id));
    check(view.step_index()==0&&view.step_count()==trace.frames.size(),"The view did not select the calculated initial frame");
    check(!IsWindowEnabled(GetDlgItem(view.window(),previous_id))&&IsWindowEnabled(GetDlgItem(view.window(),next_id)),"Initial step navigation limits are wrong");
    const auto initial=text(view,explanation_id),initial_array=name(GetDlgItem(view.window(),array_id));
    check(initial_array.find("índice 0: -8")!=initial_array.npos&&initial_array.find("índice 5: 10")!=initial_array.npos,
        "The accessible array differs from its sorted data");
    auto regions=view.reading_regions();check(regions.size()>=4&&regions.front().key=="title"&&regions[1].key=="description", "Stable authored reading regions disappeared");
    check(std::ranges::none_of(regions,[](const auto& r){return r.source_text.find("-8:1\n")!=r.source_text.npos;}),"A future output was exposed to narration");
    const auto first_dynamic=regions[2].key;
    const auto step_selector=GetDlgItem(view.window(),step_selector_id);
    check(step_selector&&SendMessageW(step_selector,CB_GETCOUNT,0,0)==static_cast<LRESULT>(trace.frames.size())&&
        SendMessageW(step_selector,CB_GETCURSEL,0,0)==0,
        "The native step selector does not expose every computed frame");
    const auto middle=trace.candidate_frames[1];
    SendMessageW(step_selector,CB_SETCURSEL,static_cast<WPARAM>(middle),0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(step_selector_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(step_selector));pump();
    check(view.step_index()==middle&&text(view,explanation_id)==trace.frames[middle].explanation&&
        text(view,output_id)==trace.frames[middle].output,
        "Direct step selection lost the phase explanation or accumulated output");
    SendMessageW(step_selector,CB_SETCURSEL,0,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(step_selector_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(step_selector));pump();
    check(view.step_index()==0&&text(view,explanation_id)==initial&&
        view.reading_regions()[2].key==first_dynamic,
        "Returning to the first step retained stale narration or changed the reading identity");
    click(view,next_id);check(view.step_index()==1&&text(view,explanation_id)==trace.frames[1].explanation,"A native Next button did not select the next calculated frame");
    SendMessageW(GetDlgItem(view.window(),next_id),WM_KEYDOWN,VK_RETURN,1);
    SendMessageW(GetDlgItem(view.window(),next_id),WM_KEYDOWN,VK_RETURN,LPARAM{1}<<30);
    SendMessageW(GetDlgItem(view.window(),next_id),WM_KEYUP,VK_RETURN,1);pump();
    check(view.step_index()==2,"One physical Enter activated more than one step");
    check(text(view,output_id)==trace.frames[2].output,"The public output did not match the selected frame");
    plain_ink(GetDlgItem(view.window(),output_id));
    click(view,previous_id);check(view.step_index()==1&&text(view,output_id)==trace.frames[1].output,"Previous did not restore the exact prior output");
    click(view,reset_id);check(view.step_index()==0&&text(view,explanation_id)==initial&&name(GetDlgItem(view.window(),array_id))==initial_array,
        "Reset changed the data or failed to restore the original frame");
    check(view.reading_regions()[2].key==first_dynamic&&spec==authored,"Replay changed the authored model or its initial region identity");
    check(!view.go_to_step(view.step_count())&&view.step_index()==0,"An invalid step changed the view");
    check(view.go_to_step(trace.candidate_frames[1]),"Cannot select the calculated middle group");
    resize(view,1050);bounds(view,"wide walkthrough");if(!captures.empty())capture(view.window(),captures/"walkthrough-wide.bmp");
    view.set_text_scale(150);resize(view,360);bounds(view,"narrow walkthrough");if(!captures.empty())capture(view.window(),captures/"walkthrough-narrow-150.bmp");
    check(view.go_to_step(view.step_count()-1),"Cannot reach the final step");
    check(!IsWindowEnabled(GetDlgItem(view.window(),next_id))&&text(view,output_id)==trace.frames.back().output,
        "The final native view has incorrect output or enables another step");
    check(name(GetDlgItem(view.window(),array_id)).find("inicio = 6; fin = 6")!=std::string::npos,
        "The past-end marker was mistaken for an actual array cell");
    view.set_text_scale(100);resize(view,1050);if(!captures.empty())capture(view.window(),captures/"walkthrough-end-boundary.bmp");
    check(changes>=6&&layouts==changes,"UI changes did not refresh layout and current narration together");
}
void editable_data(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::walkthrough);SimulationView view(host,spec,"es");resize(view,820);
    check(view.go_to_step(view.step_count()-1),"Cannot prepare an edited data test");
    const auto previous_output=text(view,output_id),previous_array=name(GetDlgItem(view.window(),array_id));const auto previous_step=view.step_index();
    set_edit_text(GetDlgItem(view.window(),input_id),"1\n2147483648\n");click(view,apply_id);
    check(view.step_index()==previous_step&&text(view,output_id)==previous_output&&name(GetDlgItem(view.window(),array_id))==previous_array,
        "An invalid edited integer partially replaced the last valid trace");
    check(!text(view,error_id).empty()&&text(view,input_id)=="1\n2147483648\n","Rejected input or its visible error was discarded");
    const auto rejected=text(view,error_id);click(view,previous_id);
    check(text(view,error_id)==rejected&&text(view,input_id)=="1\n2147483648\n",
        "Navigating the last valid trace hid the unresolved edited-data error");
    click(view,next_id);check(text(view,error_id)==rejected&&text(view,output_id)==previous_output,
        "Returning to a frame silently accepted the rejected input");
    set_edit_text(GetDlgItem(view.window(),input_id),"7\r\n-2\r\n7\r\n");click(view,apply_id);
    check(SendMessageW(GetDlgItem(view.window(),step_selector_id),CB_GETCOUNT,0,0)==static_cast<LRESULT>(view.step_count()),
        "Applying input left old entries in the step selector");
    check(view.step_index()==0&&text(view,error_id).empty()&&view.go_to_step(view.step_count()-1)&&text(view,output_id)=="-2:1\n7:2\n",
        "Applying valid input retained authored results instead of recalculating");
    const auto changed_identity=view.reading_regions()[2].key;
    click(view,restore_id);check(view.step_index()==0&&view.go_to_step(view.step_count()-1)&&text(view,output_id)==previous_output,
        "Restoring data did not return to the active scenario");
    check(view.reading_regions()[2].key!=changed_identity,"Different data borrowed a dynamic reading identity");
    const auto selector=GetDlgItem(view.window(),scenario_id);SendMessageW(selector,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_index()==0&&view.go_to_step(view.step_count()-1)&&text(view,output_id)=="4:2\n9:2\n"&&
        text(view,variables_id).find("mayores: 4, 9")!=std::string::npos,"Selecting a native scenario did not calculate its tie");
    resize(view,820);if(!captures.empty())capture(view.window(),captures/"edited-scenario-tie.bmp");
    SendMessageW(selector,CB_SETCURSEL,2,0);SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_count()==1&&view.step_index()==0&&text(view,output_id).empty()&&
        SendMessageW(GetDlgItem(view.window(),step_selector_id),CB_GETCOUNT,0,0)==1&&
        SendMessageW(GetDlgItem(view.window(),step_selector_id),CB_GETCURSEL,0,0)==0&&
        !IsWindowEnabled(GetDlgItem(view.window(),next_id)),"The empty scenario fabricated another step or output");
    check(name(GetDlgItem(view.window(),array_id)).find("No hay valores")!=std::string::npos,"Empty accessible array is ambiguous");
}
void modes(HWND host,const std::filesystem::path& captures) {
    for(const auto [mode,file]:std::array{
            std::pair{SimulationViewMode::array,"array-wide.bmp"},std::pair{SimulationViewMode::groups,"groups-wide.bmp"},
            std::pair{SimulationViewMode::candidates,"candidates-wide.bmp"},std::pair{SimulationViewMode::summary,"summary-wide.bmp"}}){
        auto spec=fixture(mode);spec.editable=false;
        // The lesson reader handles canvas mouse messages for its drag gestures.
        // Simulate a consuming parent handler to protect the click interaction.
        SimulationCallbacks gestures{};
        gestures.passive_gesture=[](HWND,UINT message,WPARAM,LPARAM)->std::optional<LRESULT>{
            if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)return LRESULT{0};
            return std::nullopt;
        };
        SimulationView view(host,spec,"es",gestures);resize(view,960);
        const auto navigation_selector=GetDlgItem(view.window(),step_selector_id);
        check(navigation_selector&&bool(GetWindowLongPtrW(navigation_selector,GWL_STYLE)&WS_VISIBLE)==
            (mode==SimulationViewMode::candidates),
            "Static and group views should not show the step selector");
        if(mode==SimulationViewMode::groups){
            const auto group=GetDlgItem(view.window(),group_first_id);check(group&&SendMessageW(group,BM_GETCHECK,0,0)==BST_CHECKED,"The group selector lacks its initial checked state");
            SendMessageW(group,WM_KEYDOWN,VK_RIGHT,1);pump();
            check(view.step_index()==1&&SendMessageW(GetDlgItem(view.window(),group_first_id+1),BM_GETCHECK,0,0)==BST_CHECKED,
                "Keyboard group navigation did not update the radio state");
            check(text(view,variables_id).find("inicio: 1\nfin (exclusivo): 5")!=std::string::npos&&text(view,variables_id).find("frecuencia: 4")!=std::string::npos,
                "Group selection showed another group's interval or frequency");
            check(name(GetDlgItem(view.window(),array_id)).find("[1, 5)")!=std::string::npos,"The selected interval has no non-color description");
            click_group_cell(view,5);
            check(view.step_index()==2&&SendMessageW(GetDlgItem(view.window(),group_first_id+2),BM_GETCHECK,0,0)==BST_CHECKED&&
                text(view,variables_id).find("frecuencia: 1")!=std::string::npos,
                "Clicking a rendered cell did not select its group after the parent consumed mouse events");
            click_group_cell(view,0,true);
            check(view.step_index()==2,"Dragging from a cell was incorrectly treated as a click");
            click_group_cell(view,0,false,true);
            check(view.step_index()==2,"Ctrl+click on an array cell changed the selected group");
            click_group_cell(view,0);
            check(view.step_index()==0&&SendMessageW(GetDlgItem(view.window(),group_first_id),BM_GETCHECK,0,0)==BST_CHECKED&&
                text(view,variables_id).find("frecuencia: 1")!=std::string::npos,
                "Clicking the first cell did not restore its group or radio selection");
            RECT selector{},array{};GetWindowRect(group,&selector);GetWindowRect(GetDlgItem(view.window(),array_id),&array);
            check(selector.bottom<=array.top,"The native group selector is not before the array");
            wchar_t statistics_class[96]{};GetClassNameW(GetDlgItem(view.window(),variables_id),statistics_class,96);
            check(std::wstring_view(statistics_class)==L"LearnGameAndEngineDev.SimulationStatistics",
                "Statistics fell back to a paragraph instead of the real value cards");
            const auto reading=view.reading_regions();check(std::ranges::any_of(reading,[](const auto& region){
                return region.target&&GetDlgCtrlID(region.target)==variables_id&&region.passive_label&&region.source_text.find("frecuencia: 4")!=std::string::npos;}),
                "The current large value cards lack their accessible narration summary");
        }else if(mode==SimulationViewMode::candidates){
            check(view.step_count()==3&&text(view,step_id)=="Grupo 1 de 3","Candidates mode did not count completed groups correctly");
            check(text(view,variables_id).find("Valor examinado: -8")!=std::string::npos,"Candidates mode did not identify its first examined value");click(view,next_id);
            check(text(view,variables_id).find("Valor examinado: 3")!=std::string::npos&&text(view,variables_id).find("Valor examinado: -8")==std::string::npos,
                "Advancing retained the previous examined candidate");
            check(text(view,step_id)=="Grupo 2 de 3","The completed-group counter did not advance with its examined value");
            check(text(view,variables_id).find("máxima: 4")!=std::string::npos&&text(view,output_id)=="-8:1\n3:4\n","Candidate state was not derived from its completion frame");
            check(SendMessageW(navigation_selector,CB_GETCOUNT,0,0)==static_cast<LRESULT>(view.step_count()),
                "Candidate selector does not contain each computed group");
            SendMessageW(navigation_selector,CB_SETCURSEL,2,0);
            SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(step_selector_id,CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(navigation_selector));pump();
            check(view.step_index()==2&&text(view,output_id)=="-8:1\n3:4\n10:1\n",
                "Jumping to a candidate group did not refresh the complete trace");
            click_group_cell(view,2);
            check(view.step_index()==1&&text(view,output_id)=="-8:1\n3:4\n"&&
                SendMessageW(navigation_selector,CB_GETCURSEL,0,0)==1,
                "Clicking a member of the middle group did not select its completed candidate frame");
        }else{
            check(view.step_count()==1&&!(GetWindowLongPtrW(GetDlgItem(view.window(),next_id),GWL_STYLE)&WS_VISIBLE),"A static mode has visible step controls");
            if(mode==SimulationViewMode::summary)check(text(view,variables_id)=="máxima: 4\nmayores: 3","Summary values were not calculated");
            else {check(name(GetDlgItem(view.window(),array_id)).find("Grupo 2: índices 1 a 4")!=std::string::npos,
                "Colored groups have no corresponding non-color group identity");group_colors_are_visible(GetDlgItem(view.window(),array_id));}
        }
        bounds(view,file);if(!captures.empty())capture(view.window(),captures/file);
        view.set_text_scale(150);resize(view,340);bounds(view,file);
        if(mode==SimulationViewMode::groups&&!captures.empty())capture(view.window(),captures/"groups-narrow-150.bmp");
        if(mode==SimulationViewMode::groups||mode==SimulationViewMode::candidates){const auto selector=GetDlgItem(view.window(),scenario_id);
            SendMessageW(selector,CB_SETCURSEL,2,0);SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(selector));pump();
            check(view.step_index()==0&&view.step_count()==1&&name(GetDlgItem(view.window(),array_id)).find("No hay valores")!=std::string::npos,
                "An empty group/candidate scenario reused a prior group's state");
            if(mode==SimulationViewMode::groups)check(!GetDlgItem(view.window(),group_first_id),"An empty group view retained a stale radio option");}
    }
}
void text_and_independent_views(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::summary);spec.text_values=true;spec.scenarios.clear();
    spec.values={std::string("patio"),std::string("sol"),std::string("faro"),std::string("patio"),std::string("sol")};
    SimulationView first(host,spec,"es"),second(host,spec,"es");resize(first,800);resize(second,800);
    check(text(first,output_id)=="faro:1\npatio:2\nsol:2\n"&&text(first,variables_id)=="máxima: 2\nmayores: \"patio\", \"sol\"",
        "Text array maxima and ties differ from their input");
    set_edit_text(GetDlgItem(first.window(),input_id)," sitio \n sitio \nsol");click(first,apply_id);
    check(text(first,output_id)==" sitio :2\nsol:1\n"&&text(second,output_id)=="faro:1\npatio:2\nsol:2\n",
        "Editing text lost meaningful spaces or modified another view");
    const auto long_input=std::string(120,'x')+"\n😀";
    set_edit_text(GetDlgItem(first.window(),input_id),long_input);click(first,apply_id);
    const auto long_trace=build_simulation_trace(spec,simulation_parse_input(spec,long_input));
    check(text(first,explanation_id)==long_trace.frames.back().explanation&&text(first,explanation_id).find("😀")!=std::string::npos,
        "The native explanation lost the literal Unicode surrogate pair");
    const auto literal_text=rich_edit_contents(first),original_fonts=rich_edit_fonts(first,100);
    const auto apply_scale=[&](int percentage){
        const auto before=layout_snapshot(first);first.set_text_scale(percentage);
        const auto before_pump=layout_snapshot(first);
        try{
            resize(first,340);bounds(first,"narrow long edited text after zoom");
            check(rich_edit_contents(first)==literal_text,"Zoom changed literal simulation text, including its Unicode or final newline");
            check(rich_edit_fonts(first,percentage)==original_fonts,"Zoom changed the base or first-character font");
            check(text(first,explanation_id)==long_trace.frames.back().explanation&&text(first,input_id)==long_input,
                "Zoom changed the calculated explanation or edited source data");
        }catch(...){
            report_layout_difference("immediately after zoom",before,before_pump);
            report_layout_difference("after measurement and pump",before_pump,layout_snapshot(first));
            if(!captures.empty())capture(first.window(),captures/"text-restored-zoom-failure.bmp");throw;
        }
        return first.height_for_width(340);
    };
    const auto narrow=apply_scale(150);
    if(!captures.empty())capture(first.window(),captures/"text-narrow-long-value.bmp");
    const auto original_layout=layout_snapshot(first);resize(first,800);
    check(first.height_for_width(800)<narrow,"Widening long text retained the previous narrow height");
    resize(first,340);check(first.height_for_width(340)==narrow,"Returning to the same narrow layout changed its height");
    check(apply_scale(100)<narrow,"Reducing text scale retained the enlarged layout");
    const auto restored=apply_scale(150);const auto normalized_layout=layout_snapshot(first);
    // RichEdit may normalize its final empty line on the first restoration.
    // The actual contract is preserved content, fitted text and a stable layout
    // thereafter; every subsequent restoration must match exactly.
    if(restored!=narrow){std::cout<<"INFO first zoom restoration height "<<narrow<<" -> "<<restored<<'\n';
        report_layout_difference("first restoration",original_layout,normalized_layout);}
    for(int cycle=1;cycle<=3;++cycle){
        check(apply_scale(100)<restored,"A repeated zoom reduction retained the enlarged layout");
        const auto repeated=apply_scale(150);
        if(repeated!=restored){report_layout_difference("unstable repeated restoration",normalized_layout,layout_snapshot(first));
            if(!captures.empty())capture(first.window(),captures/"text-restored-zoom-failure.bmp");
            throw std::runtime_error("Zoom restoration cycle "+std::to_string(cycle)+" changed the normalized height from "+
                std::to_string(restored)+" to "+std::to_string(repeated));}
    }
    check(text(second,output_id)=="faro:1\npatio:2\nsol:2\n"&&rich_edit_scale(GetDlgItem(second.window(),input_id))==100,
        "Repeated zoom altered another simulation's data or scale");
    if(!captures.empty())capture(first.window(),captures/"text-restored-zoom-150.bmp");
}
void empty_text_values(HWND host,const std::filesystem::path& captures) {
    const std::string empty_label="\"\" (cadena vacía)",space_label="\" \" (1 espacio)";
    const auto select=[](SimulationView& view,int id,int index) {
        const auto control=GetDlgItem(view.window(),id);
        check(control&&SendMessageW(control,CB_SETCURSEL,index,0)!=CB_ERR,"Cannot select the real simulation dropdown");
        SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control));pump();
    };
    auto grouped=fixture(SimulationViewMode::groups);grouped.text_values=true;
    grouped.values={std::string(""),std::string("patio"),std::string("patio")};
    grouped.scenarios={{"empty","Colección vacía",{}},{"empty-label","Una etiqueta vacía",{std::string("")}},
        {"space","Vacía y espacio",{std::string(""),std::string(" ")}}};
    const auto authored=grouped;
    {
        SimulationView view(host,grouped,"es");resize(view,820);
        check(name(GetDlgItem(view.window(),array_id)).find("índice 0: "+empty_label)!=std::string::npos&&
            name(GetDlgItem(view.window(),group_first_id)).find(empty_label)!=std::string::npos,
            "An empty label has no unambiguous array or group-selector representation");
        check(text(view,variables_id)=="inicio: 0\nfin (exclusivo): 1\nfrecuencia: 1",
            "The empty label was omitted from its group interval");
        click(view,group_first_id+1);
        check(text(view,variables_id)=="inicio: 1\nfin (exclusivo): 3\nfrecuencia: 2"&&text(view,output_id)==":1\npatio:2\n",
            "Selecting patio changed raw values or reused the empty label's frequency");
        click(view,group_first_id);
        check(view.step_index()==0&&text(view,output_id)==":1\n","Returning to the empty group did not restore its raw output");
        select(view,scenario_id,1);
        check(view.step_count()==1&&!GetDlgItem(view.window(),group_first_id)&&
            name(GetDlgItem(view.window(),array_id)).find("No hay valores")!=std::string::npos&&text(view,output_id).empty(),
            "An empty collection retained an empty-label group");
        select(view,scenario_id,2);
        check(GetDlgItem(view.window(),group_first_id)&&!GetDlgItem(view.window(),group_first_id+1)&&
            text(view,variables_id)=="inicio: 0\nfin (exclusivo): 1\nfrecuencia: 1"&&text(view,output_id)==":1\n",
            "A scenario with one empty label became an empty collection");
        select(view,scenario_id,3);
        check(name(GetDlgItem(view.window(),group_first_id)).find(empty_label)!=std::string::npos&&
            name(GetDlgItem(view.window(),group_first_id+1)).find(space_label)!=std::string::npos,
            "Empty and space-only labels have indistinguishable native selectors");
        click(view,group_first_id+1);
        check(text(view,variables_id)=="inicio: 1\nfin (exclusivo): 2\nfrecuencia: 1"&&text(view,output_id)==":1\n :1\n",
            "A space-only label was trimmed or merged with the empty label");
        view.set_text_scale(150);resize(view,340);bounds(view,"empty-label selectors at narrow width");
        if(!captures.empty())capture(view.window(),captures/"empty-label-groups-narrow-150.bmp");
    }
    {
        auto candidates=grouped;candidates.view=SimulationViewMode::candidates;
        candidates.values={std::string(""),std::string(" ")};candidates.scenarios.clear();
        SimulationView view(host,candidates,"es");resize(view,820);
        check(view.step_count()==2&&text(view,variables_id)=="Etiqueta examinada: "+empty_label+
            "\nfrecuencia: 1\nmáxima: 1\nmayores: "+empty_label,
            "The empty string was presented as no candidate");
        const auto first=text(view,variables_id),output=text(view,output_id);const auto reading=view.reading_regions();
        click(view,next_id);
        check(text(view,variables_id)=="Etiqueta examinada: "+space_label+
            "\nfrecuencia: 1\nmáxima: 1\nmayores: "+empty_label+", "+space_label&&
            text(view,output_id)==":1\n :1\n","The tie lost or conflated the empty and space-only candidates");
        const auto regions=view.reading_regions();
        check(std::ranges::any_of(regions,[&](const auto& region){return region.source_text.find(
            "mayores: "+empty_label+", "+space_label)!=std::string::npos;}),
            "Current narration omitted one of the visibly distinct candidates");
        click(view,previous_id);const auto reversed=view.reading_regions();
        check(text(view,variables_id)==first&&text(view,output_id)==output&&reading.size()==reversed.size(),
            "Previous did not restore the empty-label candidate state");
        for(std::size_t i=0;i<reading.size();++i)
            check(reading[i].key==reversed[i].key&&reading[i].source_text==reversed[i].source_text,
                "Previous changed an empty-label reading identity or its text");
    }
    {
        auto spec=grouped;spec.view=SimulationViewMode::walkthrough;spec.values={std::string("")};spec.scenarios.clear();
        SimulationView view(host,spec,"es");resize(view,820);
        const auto editor=GetDlgItem(view.window(),input_id),format=GetDlgItem(view.window(),input_format_id);
        check(format&&SendMessageW(format,CB_GETCURSEL,0,0)==1&&text(view,input_id)=="[\"\"]",
            "An authored empty label did not open in the explicit list editor");
        const auto finish=[&] {
            while(view.step_index()+1<view.step_count()){
                const auto previous=view.step_index();click(view,next_id);
                check(view.step_index()==previous+1,"Next did not advance the empty-label traversal");}
        };
        finish();
        check(text(view,output_id)==":1\n"&&text(view,variables_id).find("máxima: 1\nmayores: "+empty_label)!=std::string::npos,
            "The complete traversal lost its empty-label maximum or changed the raw output");
        const auto original_step=view.step_index(),original_count=view.step_count();
        const auto original_array=name(GetDlgItem(view.window(),array_id)),original_output=text(view,output_id);
        select(view,input_format_id,0);
        check(SendMessageW(format,CB_GETCURSEL,0,0)==1&&text(view,input_id)=="[\"\"]"&&!text(view,error_id).empty()&&
            view.step_index()==original_step&&view.step_count()==original_count&&text(view,output_id)==original_output&&
            name(GetDlgItem(view.window(),array_id))==original_array,
            "Switching an empty label to line format silently discarded a value or applied data");
        set_edit_text(editor,"[\"\",]");click(view,apply_id);
        check(text(view,input_id)=="[\"\",]"&&!text(view,error_id).empty()&&view.step_index()==original_step&&
            text(view,output_id)==original_output,"Malformed list input partially replaced the last valid traversal");
        select(view,input_format_id,0);
        check(SendMessageW(format,CB_GETCURSEL,0,0)==1&&text(view,input_id)=="[\"\",]"&&
            view.step_index()==original_step&&text(view,output_id)==original_output,
            "Changing format discarded an invalid draft or applied it to the simulation");
        set_edit_text(editor,"[]");click(view,apply_id);
        check(view.step_index()==0&&view.step_count()==1&&text(view,error_id).empty()&&text(view,output_id).empty()&&
            text(view,variables_id).find("máxima: 0\nmayores: ninguno")!=std::string::npos,
            "Applying an empty collection fabricated an empty-string candidate");
        set_edit_text(editor,"[\"\", \" \"]");click(view,apply_id);finish();
        check(text(view,output_id)==":1\n :1\n"&&text(view,variables_id).find(
            "máxima: 1\nmayores: "+empty_label+", "+space_label)!=std::string::npos,
            "The list editor failed to preserve two distinct invisible text values");
        // Format conversion changes the pending draft only. It must not apply
        // that draft, restart the traversal or reinterpret legacy line syntax.
        set_edit_text(editor,"[\"patio\", \"patio\"]");
        const auto before_conversion=view.step_index();const auto before_output=text(view,output_id);
        select(view,input_format_id,0);
        check(SendMessageW(format,CB_GETCURSEL,0,0)==0&&text(view,input_id)=="patio\npatio"&&text(view,error_id).empty()&&
            view.step_index()==before_conversion&&text(view,output_id)==before_output,
            "Changing to line format applied the pending draft or reset the step");
        set_edit_text(editor,"[\"patio\"]");click(view,apply_id);finish();
        check(text(view,output_id)=="[\"patio\"]:1\n","Legacy line input was reinterpreted as a JSON list");
        const auto literal_step=view.step_index();const auto literal_output=text(view,output_id);
        select(view,input_format_id,1);
        check(SendMessageW(format,CB_GETCURSEL,0,0)==1&&text(view,input_id)=="[\"[\\\"patio\\\"]\"]"&&
            view.step_index()==literal_step&&text(view,output_id)==literal_output,
            "Converting a legacy bracketed label changed its value or the active traversal");
        click(view,apply_id);finish();check(text(view,output_id)==literal_output,"Applying the converted list changed a literal bracketed label");
        click(view,restore_id);
        check(view.step_index()==0&&SendMessageW(format,CB_GETCURSEL,0,0)==1&&text(view,input_id)=="[\"\"]",
            "Restore did not recover the authored empty label with a lossless editor format");
        finish();check(text(view,output_id)==":1\n","Restore retained edited data instead of the original empty label");
    }
    check(grouped==authored,"Exploration mutated the authored text values or scenarios");
}
void scale_limits_and_local_control_wheel(HWND host) {
    auto spec=fixture(SimulationViewMode::summary);SimulationView first(host,spec,"es"),second(host,spec,"es");
    resize(first,800);resize(second,800);
    const auto first_contents=rich_edit_contents(first),second_contents=rich_edit_contents(second);
    first.set_text_scale(900);resize(first,800);bounds(first,"maximum simulation zoom");
    (void)rich_edit_fonts(first,150);(void)rich_edit_fonts(second,100);
    first.set_text_scale(100);resize(first,800);
    const auto input=GetDlgItem(first.window(),input_id);
    const auto original_step=first.step_index();const auto original_array=name(GetDlgItem(first.window(),array_id));
    for(int step=0;step<40&&rich_edit_scale(input)<150;++step){
        const auto before=rich_edit_scale(input);control_wheel(host,input,WHEEL_DELTA);const auto after=rich_edit_scale(input);
        check(after>before&&after<=150,"Ctrl+wheel did not enlarge the local data editor within its 150 percent limit");
    }
    check(rich_edit_scale(input)==150,"Ctrl+wheel did not reach the native 150 percent limit");
    control_wheel(host,input,WHEEL_DELTA);check(rich_edit_scale(input)==150,"An extra Ctrl+wheel event exceeded 150 percent");
    control_wheel(host,input,-WHEEL_DELTA);check(rich_edit_scale(input)>0&&rich_edit_scale(input)<150,
        "Ctrl+wheel did not reduce the local data editor's text scale");
    check(rich_edit_scale(GetDlgItem(first.window(),explanation_id))==100&&rich_edit_scale(GetDlgItem(first.window(),output_id))==100,
        "The local input gesture changed another simulation control's scale");
    (void)rich_edit_fonts(second,100);
    check(rich_edit_contents(first)==first_contents&&rich_edit_contents(second)==second_contents&&
        first.step_index()==original_step&&name(GetDlgItem(first.window(),array_id))==original_array,
        "Ctrl+wheel changed simulation data, replay position or another view");
}
void parent_width_changes(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);int layouts=0,changes=0;
    SimulationView view(host,spec,"es",{[&]{++changes;},[&]{++layouts;}});resize(view,800);
    const auto wide_height=view.height_for_width(800);
    MoveWindow(view.window(),0,0,340,wide_height,FALSE);pump();
    check(layouts==1&&changes==0,"A physical width change did not request layout only");
    const auto narrow_height=view.height_for_width(340);check(narrow_height>wide_height,"The changed width did not compute its new required height");
    resize(view,340);bounds(view,"parent width change");
    check(layouts==1&&changes==0,"Applying the requested height reentered parent reflow or changed narration");
    set_edit_text(GetDlgItem(view.window(),input_id),"1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12");click(view,apply_id);
    check(view.height_for_width(340)>narrow_height,"Edited data reused a stale layout cache");
    resize(view,340);bounds(view,"edited data at fixed width");
}
void long_labels_and_literal_values(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::groups);spec.scenarios.clear();spec.text_values=true;spec.editable=false;
    spec.values={std::string("a&b"),std::string("x")};
    SimulationView baseline(host,spec,"es");baseline.set_text_scale(150);resize(baseline,340);
    const auto short_array=window_height(GetDlgItem(baseline.window(),array_id));
    const auto short_cards=window_height(GetDlgItem(baseline.window(),variables_id));
    const auto short_radio=window_height(GetDlgItem(baseline.window(),group_first_id+1));
    spec.values[1]=std::string(128,'x');spec.labels.start=std::string(80,'S');spec.labels.end=std::string(80,'E');
    spec.labels.frequency=std::string(80,'F');
    SimulationView view(host,spec,"es");view.set_text_scale(150);resize(view,340);bounds(view,"narrow long group labels");
    const auto first=GetDlgItem(view.window(),group_first_id),second=GetDlgItem(view.window(),group_first_id+1);
    check(native_text(first).find("a&&b")!=std::string::npos&&name(first).find("a&b")!=std::string::npos,
        "A literal ampersand became a radio-button mnemonic or changed the accessible value");
    check(native_text(second).find('\n')!=std::string::npos&&window_height(second)>short_radio,
        "A long group value was not physically wrapped in its native selector");
    check(window_height(GetDlgItem(view.window(),array_id))>short_array+50&&
        window_height(GetDlgItem(view.window(),variables_id))>short_cards+50,
        "Long markers or card labels retained a clipping-height layout");
    const auto caption=wide(native_text(second));const auto dc=GetDC(second);
    const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(second,WM_GETFONT,0,0)));
    RECT button{};GetClientRect(second,&button);const auto inset=MulDiv(MulDiv(28,static_cast<int>(GetDpiForWindow(second)),96),150,100);
    for(std::size_t at=0;at<caption.size();){const auto newline=caption.find(L'\n',at);const auto end=newline==caption.npos?caption.size():newline;
        SIZE extent{};GetTextExtentPoint32W(dc,caption.data()+at,static_cast<int>(end-at),&extent);
        check(extent.cx<=button.right-inset,"A native radio line is wider than its allotted text region");at=end+1;}
    SelectObject(dc,old);ReleaseDC(second,dc);
    if(!captures.empty())capture(view.window(),captures/"groups-long-labels-narrow-150.bmp");
}
void scrollable_text_keyboard(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::summary);spec.values.clear();
    for(std::int64_t value=0;value<64;++value)spec.values.emplace_back(value);
    spec.explanations.done.clear();for(int line=0;line<70;++line)spec.explanations.done+="Explicación de este paso.\n";
    SimulationView view(host,spec,"es");resize(view,800);bounds(view,"scrollable explanation/output");
    const auto explanation=GetDlgItem(view.window(),explanation_id),output=GetDlgItem(view.window(),output_id);
    check((GetWindowLongPtrW(explanation,GWL_STYLE)&WS_TABSTOP)&&(GetWindowLongPtrW(output,GWL_STYLE)&WS_TABSTOP),
        "Scrollable read-only content cannot be reached with Tab");
    KeyboardProbe explanation_probe(explanation),output_probe(output);
    const auto original_contents=rich_edit_contents(view);
    if(!captures.empty())capture(view.window(),captures/"keyboard-scroll-initial.bmp");
    const auto selector=GetDlgItem(view.window(),scenario_id);SetFocus(selector);
    check(GetFocus()==selector,"Cannot focus the isolated native scenario control");
    bool visited_explanation=false,visited_output=false;std::vector<std::string> failures;
    for(int attempt=0;attempt<20&&(!visited_explanation||!visited_output);++attempt){
        const auto explanation_before=scroll_state(explanation),output_before=scroll_state(output);
        key(host,VK_TAB);const auto focused=GetFocus();
        if(focused!=explanation&&focused!=output)continue;
        if((focused==explanation&&visited_explanation)||(focused==output&&visited_output))continue;
        const auto id=GetDlgCtrlID(focused);auto& delivered=focused==explanation?explanation_probe.delivered:output_probe.delivered;
        auto history="before Tab: "+scroll_description(focused==explanation?explanation_before:output_before);
        const auto observe=[&](const char* phase){const auto state=scroll_state(focused);history+="; "+std::string(phase)+": "+scroll_description(state);return state;};
        (void)observe("after Tab");
        try{
            // Tab may select the document or retain its previous caret. Set a
            // known starting point through the same real keyboard route before
            // requiring PageDown to reveal text below that point.
            key(host,VK_HOME,true);const auto origin=observe("after Ctrl+Home");
            check(GetFocus()==focused&&origin.first==0&&origin.position.y==0,"Ctrl+Home did not reach the start of the focused read-only content");
            check(origin.has_range&&origin.vertical.nMax-origin.vertical.nMin+1>static_cast<int>(origin.vertical.nPage),
                "The bounded read-only content has no native overflow range");
            key(host,VK_NEXT);const auto down=observe("after PageDown");
            check(GetFocus()==focused&&down.first>origin.first&&down.position.y>origin.position.y,
                "PageDown did not reveal hidden read-only simulation content from its verified origin");
            if(!captures.empty())capture(view.window(),captures/("keyboard-control-"+std::to_string(id)+"-pagedown.bmp"));
            key(host,VK_PRIOR);const auto up=observe("after PageUp");
            check(GetFocus()==focused&&up.first<down.first&&up.position.y<down.position.y,
                "PageUp did not return toward the start of the read-only simulation content");
            check(delivered.home==1&&delivered.page_down==1&&delivered.page_up==1,"The queued navigation keys did not reach the actual focused RichEdit");
            check(rich_edit_contents(view)==original_contents,"Keyboard scrolling changed the literal simulation content");
        }catch(const std::exception& error){
            (void)observe("at failure");
            failures.push_back("control "+std::to_string(id)+": "+error.what()+"; "+history+
                "; delivered=[home:"+std::to_string(delivered.home)+",down:"+std::to_string(delivered.page_down)+",up:"+
                std::to_string(delivered.page_up)+"] focus="+std::to_string(GetDlgCtrlID(GetFocus())));
            if(!captures.empty())try{capture(view.window(),captures/("keyboard-control-"+std::to_string(id)+"-failure.bmp"));}
                catch(const std::exception& capture_error){std::cerr<<"Cannot capture keyboard failure: "<<capture_error.what()<<'\n';}
        }
        std::cout<<"KEYBOARD control "<<id<<' '<<history<<"; delivered=[home:"<<delivered.home<<",down:"<<delivered.page_down<<",up:"<<delivered.page_up<<"]\n";
        if(focused==explanation)visited_explanation=true;else visited_output=true;
    }
    if(!visited_explanation||!visited_output)failures.push_back("Real IsDialogMessage Tab traversal skipped a scrollable control: explanation="+
        std::to_string(visited_explanation)+", output="+std::to_string(visited_output));
    const auto original_output=text(view,output_id);check(original_output.find("63:1\n")!=std::string::npos,"The scrolled output lost its last line");
    SendMessageW(selector,CB_SETCURSEL,2,0);SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(selector));pump(host);
    check(!(GetWindowLongPtrW(output,GWL_STYLE)&WS_TABSTOP)&&!(GetWindowLongPtrW(explanation,GWL_STYLE)&WS_TABSTOP),
        "Short replacement content retained unnecessary scrolling tab stops");
    SetFocus(nullptr);
    if(!failures.empty()){std::string report;for(const auto& failure:failures){if(!report.empty())report+='\n';report+=failure;}throw std::runtime_error(report);}
}
}

int main(int argc,char** argv)try {
    check(argc<=2,"Expected an optional native capture directory");
    const auto initialized=OleInitialize(nullptr);check(SUCCEEDED(initialized),"Cannot initialize native simulation accessibility");
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&common),"Cannot initialize native controls");
    reader_theme::prepare_fonts();
    Window host{CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Isolated simulation QA",WS_OVERLAPPEDWINDOW,
        0,0,1200,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};check(host.value,"Cannot create hidden simulation QA host");
    const auto captures=argc==2?std::filesystem::path(wide(argv[1])):std::filesystem::path{};
    failure_captures=captures;
    int failures=0;
    const auto run=[&](const char* label,const auto& scenario){
        try{scenario();std::cout<<"PASS simulation scenario: "<<label<<'\n';}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL simulation scenario "<<label<<": "<<error.what()<<'\n';}
        catch(...){++failures;std::cerr<<"FAIL simulation scenario "<<label<<": unknown native failure\n";}
        pump(host.value);
    };
    run("navigation",[&]{navigation(host.value,captures);});
    run("editable_data",[&]{editable_data(host.value,captures);});
    run("modes",[&]{modes(host.value,captures);});
    run("text_and_independent_views",[&]{text_and_independent_views(host.value,captures);});
    run("empty_text_values",[&]{empty_text_values(host.value,captures);});
    run("scale_limits_and_local_control_wheel",[&]{scale_limits_and_local_control_wheel(host.value);});
    run("parent_width_changes",[&]{parent_width_changes(host.value);});
    run("long_labels_and_literal_values",[&]{long_labels_and_literal_values(host.value,captures);});
    run("scrollable_text_keyboard",[&]{scrollable_text_keyboard(host.value,captures);});
    check(!IsWindowVisible(host.value)&&GetAncestor(GetForegroundWindow(),GA_ROOT)!=host.value,"Simulation QA activated the desktop");
    if(failures){std::cerr<<"FAIL native simulation view: "<<failures<<" independent scenario(s) failed\n";return 1;}
    std::cout<<"PASS native simulation view: actual buttons, keyboard, exact replay, editable/scenario data, groups, candidates, summary, current narration and responsive controls\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native simulation view: "<<error.what()<<'\n';return 1;}
