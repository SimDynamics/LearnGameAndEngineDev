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
#include <cstdlib>
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
    input_format_id=120,step_selector_id=121,original_order_id=122,transition_id=123,
    previous_decision_id=124,next_decision_id=125,result_comparison_id=126,
    frequencies_id=127,frequencies_label_id=128,frequency_filter_id=129,
    progress_label_id=130,progress_id=131,group_first_id=200;
std::filesystem::path failure_captures;
void capture(HWND window,const std::filesystem::path& path);
void fitted_text(SimulationView& view);
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
// The QA host is deliberately hidden. IsWindowVisible follows ancestors,
// so it reports false even when a child has the WS_VISIBLE style applied.
// Check the child's own style and keep the hidden-host assertion intact.
bool control_shown(HWND child) {
    return child&&(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0;
}
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
// Click the center of the exact rendered cell, also when rows wrap.
// Win32 messages stay in the isolated test host; no desktop input.
void click_array_cell(SimulationView& view,std::size_t cell,bool drag=false,bool modified=false) {
    const auto canvas=GetDlgItem(view.window(),array_id);check(canvas,"Array canvas is missing");
    const auto rect=view.cell_bounds_in_canvas(cell);
    check(rect.has_value(),"The selected cell does not exist");
    const POINT point{(rect->left+rect->right)/2,(rect->top+rect->bottom)/2};
    check(PtInRect(&*rect,point),"The rendered cell has no clickable interior");
    const auto keys=static_cast<WPARAM>(MK_LBUTTON|(modified?MK_CONTROL:0));
    SendMessageW(canvas,WM_LBUTTONDOWN,keys,MAKELPARAM(point.x,point.y));
    if(drag)SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x+45,point.y));
    SendMessageW(canvas,WM_LBUTTONUP,modified?MK_CONTROL:0,MAKELPARAM(point.x,point.y));pump();
}
// Hit testing uses the chart's actual client coordinates, clipped to the
// visible viewport even when the group has been scrolled or filtered.
void click_frequency_card(SimulationView& view,std::size_t group,
        bool drag=false,bool modified=false,bool double_click=false) {
    const auto chart=GetDlgItem(view.window(),frequencies_id);
    check(chart,"The native histogram is missing");
    const auto rect=view.frequency_card_bounds_in_canvas(group);
    check(rect.has_value(),"The requested histogram card is not visible");
    const POINT point{(rect->left+rect->right)/2,(rect->top+rect->bottom)/2};
    check(PtInRect(&*rect,point),"Histogram card has no clickable interior");
    const auto keys=static_cast<WPARAM>(MK_LBUTTON|(modified?MK_CONTROL:0));
    SendMessageW(chart,WM_LBUTTONDOWN,keys,MAKELPARAM(point.x,point.y));
    if(double_click)SendMessageW(chart,WM_LBUTTONDBLCLK,keys,MAKELPARAM(point.x,point.y));
    if(drag)SendMessageW(chart,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x+45,point.y));
    SendMessageW(chart,WM_LBUTTONUP,modified?MK_CONTROL:0,
        MAKELPARAM(point.x,point.y));
    pump();
}
std::string native_text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));return utf8(value);
}
std::string text(SimulationView& view,int id){const auto window=GetDlgItem(view.window(),id);check(window,"A simulation control is missing");
    wchar_t type[48]{};GetClassNameW(window,type,48);return std::wstring_view(type)==MSFTEDIT_CLASS?edit_text(window):native_text(window);}
int window_height(HWND window){RECT area{};GetClientRect(window,&area);return static_cast<int>(area.bottom-area.top);}
void key(HWND dialog,WPARAM value,bool control=false,bool shift=false) {
    std::array<BYTE,256> previous{};check(GetKeyboardState(previous.data()),"Cannot preserve the test-thread keyboard state");
    struct RestoreKeys {std::array<BYTE,256>& keys;~RestoreKeys(){SetKeyboardState(keys.data());}} restore{previous};
    auto keys=previous;
    for(const auto modifier:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU})keys[modifier]=0;
    if(control)keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;
    if(shift)keys[VK_SHIFT]=keys[VK_LSHIFT]=0x80;
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
        int numerator=0,denominator=0;
        check(SendMessageW(child,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),
            reinterpret_cast<LPARAM>(&denominator))!=FALSE,
            "Cannot retrieve the native zoom accepted by RichEdit");
        check(numerator>0&&numerator<=64&&denominator>0&&denominator<=64,
            "RichEdit returned an invalid zoom numerator/denominator");
        check(rich_edit_scale(child)==expected_scale,
            "A visible RichEdit did not apply the requested text scale");
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
        // The native layout deliberately bounds five read-only fields and
        // makes them keyboard-scrollable when their full text exceeds the cap.
        // A scrollable viewport is not a clipped, unreachable text region.
        const auto id=GetDlgCtrlID(child);
        const bool bounded=(id==explanation_id||id==output_id||
            id==original_order_id||id==transition_id||
            id==result_comparison_id)&&(style&WS_TABSTOP);
        if(!bounded&&probe.required>window_height(child))throw std::runtime_error("RichEdit "+std::to_string(id)+
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
// Native WM_PRINT inspection: distinguish the border and "Actual" heading
// from an otherwise identical final histogram, without a desktop capture.
std::vector<COLORREF> frequency_header_pixels(SimulationView& view,std::size_t group) {
    const auto bounds=view.frequency_card_bounds_in_canvas(group);
    check(bounds.has_value(),"The histogram group to inspect is not visible");
    const auto chart=GetDlgItem(view.window(),frequencies_id);
    RECT client{};GetClientRect(chart,&client);
    const auto screen=GetDC(chart);
    check(screen!=nullptr,"Cannot inspect histogram native paint surface");
    const auto dc=CreateCompatibleDC(screen);
    const auto bitmap=CreateCompatibleBitmap(screen,
        std::max<LONG>(1,client.right),std::max<LONG>(1,client.bottom));
    check(dc&&bitmap,"Cannot allocate histogram paint capture");
    const auto previous=SelectObject(dc,bitmap);
    SendMessageW(chart,WM_PRINT,reinterpret_cast<WPARAM>(dc),
        PRF_CLIENT|PRF_ERASEBKGND);
    std::vector<COLORREF> pixels;
    const auto right=std::min<LONG>(bounds->right-2,bounds->left+160);
    const auto bottom=std::min<LONG>(bounds->bottom-2,bounds->top+36);
    for(LONG y=bounds->top+2;y<bottom;y+=2)
        for(LONG x=bounds->left+2;x<right;x+=2)
            pixels.push_back(GetPixel(dc,x,y));
    SelectObject(dc,previous);DeleteObject(bitmap);
    DeleteDC(dc);ReleaseDC(chart,screen);
    check(!pixels.empty(),"The histogram header paint is empty");
    return pixels;
}
// Catch an omitted native label before the larger interaction scenarios
// misdiagnose the resulting missing output, narration or layout state.
void native_text_control_contract(HWND host) {
    const auto spec=fixture(SimulationViewMode::walkthrough);
    SimulationView view(host,spec,"es");
    const std::array<int,16> required{
        100,101,102,104,106,107,109,110,
        114,118,119,122,123,126,128,130
    };
    for(const auto id:required){
        const auto control=GetDlgItem(view.window(),id);
        if(!control)throw std::runtime_error("Missing authored native text control "+std::to_string(id));
        wchar_t type[48]{};
        check(GetClassNameW(control,type,48)>0&&std::wstring_view(type)==MSFTEDIT_CLASS,
            "An authored native text control is not a RichEdit");
        constexpr LONG_PTR expected=ES_READONLY|ES_MULTILINE;
        check((GetWindowLongPtrW(control,GWL_STYLE)&expected)==expected,
            "An authored native text control lost its read-only multiline contract");
    }
    resize(view,820);
    const auto trace=build_simulation_trace(spec);
    check(text(view,output_id)==trace.frames.front().output,
        "The mounted output does not match the initial computed frame");
    check(view.go_to_step(trace.frames.size()-1)&&
        text(view,output_id)==trace.frames.back().output,
        "The mounted native output does not expose the completed computed result");
}
void current_frequency_decision(HWND host,const std::filesystem::path& captures) {
    const auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,850);
    const auto chart=GetDlgItem(view.window(),frequencies_id);
    const auto current_count=[&](const SimulationView& target)->std::string{
        for(const auto& region:target.reading_regions())
            if(region.key.find("/frequencies/")!=std::string::npos)
                return region.source_text;
        return {};
    };
    check(current_count(view).empty(),
        "The initial frame fabricated a current histogram decision");
    check(view.go_to_step(2),"Cannot inspect the first completed decision");
    const auto first=name(chart);
    check(first.find("Grupo 1: -8, frecuencia 1, candidato al máximo actual, grupo de la decisión actual")!=std::string::npos&&
        current_count(view)==first,
        "The first group is not identified as both the current decision and a candidate");
    check(view.go_to_step(3)&&
        name(chart).find("grupo de la decisión actual")==std::string::npos&&
        name(chart).find("Grupo 1: -8, frecuencia 1")!=std::string::npos,
        "Starting a new group reused the previous group decision marker");
    check(view.go_to_step(7),"Cannot inspect the second decision");
    const auto winning=name(chart);
    check(winning.find("Grupo 1: -8, frecuencia 1, grupo de la decisión actual")==std::string::npos&&
        winning.find("Grupo 2: 3, frecuencia 4, candidato al máximo actual, grupo de la decisión actual")!=std::string::npos,
        "The winner of the second comparison was not marked as current");
    check(view.go_to_step(8)&&
        name(chart).find("grupo de la decisión actual")==std::string::npos,
        "Counting another element incorrectly marked a completed decision");

    check(view.go_to_step(9),"Cannot inspect the losing group decision");
    const auto losing=name(chart);
    check(losing.find("Grupo 2: 3, frecuencia 4, candidato al máximo actual")!=std::string::npos&&
        losing.find("Grupo 3: 10, frecuencia 1, grupo de la decisión actual")!=std::string::npos&&
        losing.find("Grupo 3: 10, frecuencia 1, candidato al máximo actual")==std::string::npos,
        "The currently examined losing group was confused with the winning candidate");
    const auto painted_current=frequency_header_pixels(view,2);
    if(!captures.empty())capture(view.window(),captures/"frequencies-current-loser.bmp");
    check(view.go_to_step(view.step_count()-1),"Cannot inspect the completed walkthrough");
    const auto done=name(chart);
    check(done.find("Grupo 3: 10, frecuencia 1")!=std::string::npos&&
        done.find("grupo de la decisión actual")==std::string::npos,
        "Done retained the marker of an already completed group comparison");
    const auto painted_done=frequency_header_pixels(view,2);
    check(painted_current!=painted_done,
        "The current group marker was not rendered in the native histogram header");
    check(view.go_to_step(9)&&name(chart)==losing,
        "Returning to the same decision did not restore the current group marker");

    click(view,frequency_filter_id);
    check(name(chart).find("Grupo 3:")==std::string::npos&&
        name(chart).find("grupo de la decisión actual")==std::string::npos,
        "The filtered candidate-only chart showed a losing current group");
    click(view,frequency_filter_id);
    check(name(chart)==losing,
        "Showing all groups did not restore the current losing decision");
    click_frequency_card(view,1);
    check(view.step_index()==7&&
        name(chart).find("Grupo 2: 3, frecuencia 4, candidato al máximo actual, grupo de la decisión actual")!=std::string::npos,
        "Clicking a completed group did not move the current decision marker");

    view.set_text_scale(150);resize(view,340);
    bounds(view,"current histogram decision at 150 percent");
    check(name(chart).find("grupo de la decisión actual")!=std::string::npos,
        "The readable current decision was lost during zoom/reflow");

    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(current_count(view).empty(),"A new scenario leaked the old current group");
    check(view.go_to_step(6),"Cannot inspect the equality decision");
    const auto tie=name(chart);
    check(tie.find("Grupo 1: 4, frecuencia 2, candidato al máximo actual")!=std::string::npos&&
        tie.find("Grupo 1: 4, frecuencia 2, candidato al máximo actual, grupo de la decisión actual")==std::string::npos&&
        tie.find("Grupo 2: 9, frecuencia 2, candidato al máximo actual, grupo de la decisión actual")!=std::string::npos,
        "A tie did not distinguish the current equal-frequency group from earlier candidates");

    auto candidates=fixture(SimulationViewMode::candidates);
    SimulationView examined(host,candidates,"es");resize(examined,820);
    const auto by_group=GetDlgItem(examined.window(),frequencies_id);
    check(name(by_group).find("Grupo 1: -8, frecuencia 1, candidato al máximo actual, grupo de la decisión actual")!=std::string::npos,
        "Candidates view did not mark its initially examined group");
    check(examined.go_to_step(1)&&
        name(by_group).find("Grupo 2: 3, frecuencia 4, candidato al máximo actual, grupo de la decisión actual")!=std::string::npos,
        "Candidates view failed to distinguish the selected group from older observations");

    auto summary=fixture(SimulationViewMode::summary);
    SimulationView final(host,summary,"es");resize(final,820);
    check(name(GetDlgItem(final.window(),frequencies_id)).find(
        "grupo de la decisión actual")==std::string::npos,
        "A static summary invented a current per-group decision");
    check(spec==authored,"Marking the current group modified authored data");
}
void histogram_card_navigation(HWND host) {
    const auto input=fixture(SimulationViewMode::walkthrough);
    const auto authored=input;
    const auto expected=build_simulation_trace(input);
    int changes=0,layouts=0;
    SimulationCallbacks gestures{};
    gestures.changed=[&]{++changes;};
    gestures.layout_changed=[&]{++layouts;};
    // A consuming reader parent must not suppress a valid short click.
    gestures.passive_gesture=[](HWND,UINT message,WPARAM,LPARAM)->std::optional<LRESULT>{
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)return LRESULT{0};
        return std::nullopt;
    };
    SimulationView view(host,input,"es",gestures);resize(view,850);
    const auto chart=GetDlgItem(view.window(),frequencies_id);
    check(!view.frequency_card_bounds_in_canvas(0)&&
        !view.frequency_card_bounds_in_canvas(1),
        "An initial walkthrough exposed a future card for clicking");
    check(view.go_to_step(7)&&view.frequency_card_bounds_in_canvas(0)&&
        view.frequency_card_bounds_in_canvas(1)&&
        !view.frequency_card_bounds_in_canvas(2),
        "A partially observed later group became clickable prematurely");
    const auto before=changes;
    click_frequency_card(view,0,true);
    click_frequency_card(view,0,false,true);
    click_frequency_card(view,0,false,false,true);
    check(view.step_index()==7&&changes==before,
        "Dragging, Ctrl+click or double-click incorrectly navigated the chart");
    SendMessageW(chart,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(1,1));
    SendMessageW(chart,WM_LBUTTONUP,0,MAKELPARAM(1,1));pump();
    check(view.step_index()==7,"Clicking outside any card changed the computed step");
    click_frequency_card(view,0);
    check(view.step_index()==expected.candidate_frames[0]&&
        text(view,output_id)=="-8:1\n"&&
        text(view,step_id)=="Paso 3 de 11"&&
        !view.frequency_card_bounds_in_canvas(1),
        "The first bar did not jump to its completed decision without future output");
    check(view.go_to_step(9),"Cannot prepare all completed histogram groups");
    click_frequency_card(view,1);
    check(view.step_index()==expected.candidate_frames[1]&&
        text(view,output_id)=="-8:1\n3:4\n"&&
        SendMessageW(GetDlgItem(view.window(),step_selector_id),CB_GETCURSEL,0,0)==
            static_cast<LRESULT>(expected.candidate_frames[1]),
        "Clicking the second bar selected a different group or stale step selector");
    check(view.go_to_step(9),"Cannot prepare filtered histogram");
    click(view,frequency_filter_id);
    check(!view.frequency_card_bounds_in_canvas(0)&&
        view.frequency_card_bounds_in_canvas(1)&&
        !view.frequency_card_bounds_in_canvas(2),
        "Filtered bar geometry reused the old visual slot as a group identity");
    click_frequency_card(view,1);
    check(view.step_index()==expected.candidate_frames[1]&&
        text(view,output_id)=="-8:1\n3:4\n"&&
        name(chart).find("Grupo 1:")==std::string::npos&&
        name(chart).find("Grupo 3:")==std::string::npos,
        "Filtered click selected visual slot zero rather than original group two");
    check(view.go_to_step(0)&&!view.frequency_card_bounds_in_canvas(1),
        "Returning to the beginning preserved a clickable future histogram");
    check(changes>=5&&layouts==changes&&input==authored,
        "Histogram exploration mutated authored data or failed to update narration");

    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(view.go_to_step(6)&&
        view.frequency_card_bounds_in_canvas(0)&&
        view.frequency_card_bounds_in_canvas(1),
        "A tie scenario failed to rebuild its clickable groups");
    click_frequency_card(view,0);
    check(view.step_index()==3&&text(view,output_id)=="4:2\n"&&
        !view.frequency_card_bounds_in_canvas(1),
        "Tie scenario kept future candidate output after choosing its first group");
    SendMessageW(scenario,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(view.step_count()==1&&!view.frequency_card_bounds_in_canvas(0),
        "An empty scenario retained clickable histogram geometry");

    auto candidates=fixture(SimulationViewMode::candidates);
    SimulationView candidate_view(host,candidates,"es");resize(candidate_view,850);
    check(candidate_view.go_to_step(2),"Cannot prepare completed candidate groups");
    click_frequency_card(candidate_view,1);
    check(candidate_view.step_index()==1&&
        text(candidate_view,output_id)=="-8:1\n3:4\n",
        "Candidates mode did not navigate to the clicked completed group");
    check(candidate_view.go_to_step(2),"Cannot prepare a filtered candidate chart");
    click(candidate_view,frequency_filter_id);
    check(candidate_view.frequency_card_bounds_in_canvas(1)&&
        !candidate_view.frequency_card_bounds_in_canvas(0),
        "Candidate filter changed the original group-to-card mapping");
    click_frequency_card(candidate_view,1);
    check(candidate_view.step_index()==1&&
        text(candidate_view,variables_id).find("Valor examinado: 3")!=std::string::npos,
        "Candidate filter click selected a different group");

    auto summary=fixture(SimulationViewMode::summary);
    SimulationView static_view(host,summary,"es");resize(static_view,850);
    const auto initial_regions=static_view.reading_regions();
    check(static_view.frequency_card_bounds_in_canvas(0).has_value(),
        "Summary chart failed to draw the factual first group");
    click_frequency_card(static_view,0);
    const auto after_regions=static_view.reading_regions();
    check(static_view.step_index()==0&&
        after_regions.size()==initial_regions.size()&&
        std::equal(initial_regions.begin(),initial_regions.end(),after_regions.begin(),
            [](const SimulationReadingRegion& left,const SimulationReadingRegion& right){
                return left.key==right.key&&left.source_text==right.source_text;
            }),
        "A static summary reacted to a histogram click");

    // A scrollbar changes the client width and scroll offset. Use a late
    // group to ensure that both painting and clicking share that geometry.
    auto large=fixture(SimulationViewMode::walkthrough);
    large.scenarios.clear();large.editable=false;large.values.clear();
    for(std::int64_t v=0;v<64;++v)large.values.emplace_back(v);
    const auto large_trace=build_simulation_trace(large);
    SimulationView scrolled(host,large,"es");scrolled.set_text_scale(150);
    resize(scrolled,340);
    check(scrolled.go_to_step(scrolled.step_count()-1),"Cannot complete long histogram");
    const auto long_chart=GetDlgItem(scrolled.window(),frequencies_id);
    check(!scrolled.frequency_card_bounds_in_canvas(63)&&
        scrolled.frequency_card_bounds_in_canvas(0),
        "Long-chart viewport showed a distant group without scrolling");
    SendMessageW(long_chart,WM_VSCROLL,SB_BOTTOM,0);pump();
    check(scrolled.frequency_card_bounds_in_canvas(63)&&
        !scrolled.frequency_card_bounds_in_canvas(0),
        "Scrolling to the bottom did not update hit-test coordinates");
    click_frequency_card(scrolled,63);
    check(scrolled.step_index()==large_trace.candidate_frames[63]&&
        text(scrolled,output_id).find("63:1\n")!=std::string::npos,
        "Clicking a scrolled card did not land at the last decision");
    check(scrolled.go_to_step(0)&&
        !scrolled.frequency_card_bounds_in_canvas(63),
        "Returning to the start retained an off-screen future card");
}
void progressive_frequency_bars(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,850);
    const auto bars=GetDlgItem(view.window(),frequencies_id);
    const auto heading=GetDlgItem(view.window(),frequencies_label_id);
    check(bars&&heading&&!control_shown(bars)&&!control_shown(heading),
        "The initial walkthrough revealed a not-yet-completed group");
    const auto shown_group_count=[&](const SimulationView& current)->std::string{
        for(const auto& region:current.reading_regions())
            if(region.key.find("/frequencies")!=std::string::npos)return region.source_text;
        return {};
    };
    check(shown_group_count(view).empty()&&native_text(bars).empty(),
        "The hidden chart exposed future groups to native accessibility");
    check(view.go_to_step(1)&&!control_shown(bars)&&shown_group_count(view).empty(),
        "Counting an element revealed a frequency before its group's decision");
    check(view.go_to_step(2)&&control_shown(bars)&&control_shown(heading),
        "A completed group has no visual frequency representation");
    check(!(GetWindowLongPtrW(bars,GWL_STYLE)&WS_TABSTOP)&&
        !(GetWindowLongPtrW(bars,GWL_STYLE)&WS_VSCROLL),
        "A one-group chart should not consume an extra keyboard tab stop");
    const auto first=name(bars);
    check(text(view,frequencies_label_id).find("escala máxima visible 1")!=std::string::npos,
        "The chart did not identify the frequency scale currently known");
    check(first.find("Frecuencias de 1 grupo terminado")!=std::string::npos&&
        first.find("Grupo 1: -8, frecuencia 1, candidato al máximo actual")!=std::string::npos&&
        first.find("Grupo 2:")==std::string::npos&&
        shown_group_count(view)==first,
        "The first bar does not describe precisely the completed groups");
    wchar_t type[96]{};GetClassNameW(bars,type,96);
    check(std::wstring_view(type)==L"LearnGameAndEngineDev.SimulationFrequencies",
        "Frequency bars are not drawn by their native Win32 control");
    check(view.go_to_step(5)&&
        name(bars).find("Frecuencias de 1 grupo terminado")!=std::string::npos&&
        name(bars).find("Grupo 1: -8, frecuencia 1")!=std::string::npos&&
        name(bars).find("Grupo 2:")==std::string::npos&&
        name(bars).find("grupo de la decisión actual")==std::string::npos,
        "Counting the next group exposed its frequency or kept a stale decision marker");
    check(view.go_to_step(7),"Could not complete the second group");
    const auto second=name(bars);
    check(text(view,frequencies_label_id).find("escala máxima visible 4")!=std::string::npos,
        "Discovering a higher frequency did not change the chart's declared scale");
    check(second.find("Frecuencias de 2 grupos terminados")!=std::string::npos&&
        second.find("Grupo 1: -8, frecuencia 1")!=std::string::npos&&
        second.find("Grupo 2: 3, frecuencia 4, candidato al máximo actual")!=std::string::npos&&
        second.find("Grupo 3:")==std::string::npos,
        "The second group's bars did not update after its maximum decision");
    check(view.go_to_step(8)&&
        name(bars).find("Frecuencias de 2 grupos terminados")!=std::string::npos&&
        name(bars).find("Grupo 2: 3, frecuencia 4")!=std::string::npos&&
        name(bars).find("Grupo 3:")==std::string::npos&&
        name(bars).find("grupo de la decisión actual")==std::string::npos,
        "Starting the last group revealed its future frequency or a stale decision marker");
    check(view.go_to_step(9),"Could not complete the final group");
    const auto third=name(bars);
    check(third.find("Frecuencias de 3 grupos terminados")!=std::string::npos&&
        third.find("Grupo 3: 10, frecuencia 1")!=std::string::npos&&
        third.find("Grupo 3: 10, frecuencia 1, candidato")==std::string::npos&&
        third.find("Grupo 2: 3, frecuencia 4, candidato al máximo actual")!=std::string::npos,
        "The chart marked a losing group as a candidate");
    view.set_text_scale(150);resize(view,340);bounds(view,"frequency chart at 150 percent");
    RECT native{};GetClientRect(bars,&native);
    check(native.right>200&&native.bottom>250,
        "Frequency chart did not wrap bars at the narrower reader width");
    if(!captures.empty())capture(view.window(),captures/"frequencies-narrow-150.bmp");
    check(view.go_to_step(0)&&!control_shown(bars)&&shown_group_count(view).empty(),
        "Returning to initial step retained future histogram bars");
    check(view.go_to_step(view.step_count()-1)&&
        name(bars).find("Frecuencias de 3 grupos terminados")!=std::string::npos&&
        name(bars).find("Grupo 3: 10, frecuencia 1")!=std::string::npos&&
        name(bars).find("grupo de la decisión actual")==std::string::npos&&
        spec==authored,
        "Terminal walkthrough lost completed groups or retained a fictitious current decision");

    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(!control_shown(bars)&&shown_group_count(view).empty()&&view.step_index()==0,
        "Changing scenario leaked prior frequencies or retained the old frame");
    check(view.go_to_step(3)&&name(bars).find("Frecuencias de 1 grupo terminado")!=std::string::npos&&
        name(bars).find("Grupo 1: 4, frecuencia 2")!=std::string::npos,
        "The tie scenario did not compute its first completed group");
    check(view.go_to_step(6),"The tie scenario cannot reach the second completed group");
    const auto tie=name(bars);
    check(tie.find("Frecuencias de 2 grupos terminados")!=std::string::npos&&
        tie.find("Grupo 1: 4, frecuencia 2, candidato al máximo actual")!=std::string::npos&&
        tie.find("Grupo 2: 9, frecuencia 2, candidato al máximo actual")!=std::string::npos,
        "Equal-frequency groups did not remain simultaneous candidate bars");

    SendMessageW(scenario,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(view.step_count()==1&&!control_shown(bars)&&
        native_text(bars).empty()&&shown_group_count(view).empty()&&
        !(GetWindowLongPtrW(bars,GWL_STYLE)&WS_TABSTOP),
        "The empty scenario retained a stale histogram, navigation or narration");

    for(const auto mode:{SimulationViewMode::array,SimulationViewMode::groups}) {
        auto other=fixture(mode);
        SimulationView static_view(host,other,"es");resize(static_view,850);
        const auto static_bars=GetDlgItem(static_view.window(),frequencies_id);
        check(static_bars&&!control_shown(static_bars)&&
            shown_group_count(static_view).empty(),
            "An array-only or group-selector mode exposed unrelated histogram bars");
    }
    {
        auto candidates=fixture(SimulationViewMode::candidates);
        SimulationView visit(host,candidates,"es");resize(visit,850);
        const auto candidate_bars=GetDlgItem(visit.window(),frequencies_id);
        check(control_shown(candidate_bars)&&
            name(candidate_bars).find("Frecuencias de 1 grupo terminado")!=std::string::npos,
            "Candidates mode cannot visualize its first completed group");
        check(visit.go_to_step(1)&&
            name(candidate_bars).find("Frecuencias de 2 grupos terminados")!=std::string::npos&&
            name(candidate_bars).find("Grupo 3:")==std::string::npos,
            "Candidates mode revealed the future third group");
    }
    {
        auto summary=fixture(SimulationViewMode::summary);
        SimulationView final(host,summary,"es");resize(final,850);
        const auto all=GetDlgItem(final.window(),frequencies_id);
        check(control_shown(all)&&name(all).find("Frecuencias de 3 grupos terminados")!=std::string::npos,
            "Summary does not show all verified groups");
        auto texts=fixture(SimulationViewMode::summary);
        texts.text_values=true;texts.scenarios.clear();
        texts.values={std::string(""),std::string(" "),std::string(" ")};
        SimulationView quoted(host,texts,"es");resize(quoted,850);
        const auto text_bars=GetDlgItem(quoted.window(),frequencies_id);
        check(name(text_bars).find("Grupo 1: \"\" (cadena vacía), frecuencia 1")!=std::string::npos&&
            name(text_bars).find("Grupo 2: \" \" (1 espacio), frecuencia 2, candidato al máximo actual")!=std::string::npos,
            "The chart lost meaningful spaces or empty string group identities");
    }
    {
        auto many=fixture(SimulationViewMode::summary);many.scenarios.clear();
        many.values.clear();
        for(int value=0;value<64;++value)many.values.emplace_back(static_cast<std::int64_t>(value));
        SimulationView scaled(host,many,"es");scaled.set_text_scale(150);
        resize(scaled,340);bounds(scaled,"64 groups in responsive histogram");
        const auto all=GetDlgItem(scaled.window(),frequencies_id);
        RECT size{};GetClientRect(all,&size);
        const auto dpi=static_cast<int>(GetDpiForWindow(all));
        const auto limit=MulDiv(MulDiv(440,dpi,96),150,100);
        SCROLLINFO initial{sizeof(initial),SIF_ALL};
        check(size.bottom>0&&size.bottom<=limit&&
            (GetWindowLongPtrW(all,GWL_STYLE)&WS_VSCROLL)&&
            (GetWindowLongPtrW(all,GWL_STYLE)&WS_TABSTOP)&&
            GetScrollInfo(all,SB_VERT,&initial)&&initial.nPos==0&&
            initial.nMax+1>static_cast<int>(initial.nPage)&&
            name(all).find("Grupo 64: 63, frecuencia 1")!=std::string::npos,
            "The 64-group histogram is not bounded, keyboard-reachable or scrollable");
        const auto authored=scaled.reading_regions();
        SendMessageW(all,WM_VSCROLL,SB_BOTTOM,0);pump();
        SCROLLINFO bottom{sizeof(bottom),SIF_ALL};
        GetScrollInfo(all,SB_VERT,&bottom);
        check(bottom.nPos>0&&bottom.nPos==bottom.nMax-static_cast<int>(bottom.nPage)+1&&
            scaled.reading_regions().size()==authored.size(),
            "Dragging to the final histogram groups changed narration or stopped early");
        SendMessageW(all,WM_KEYDOWN,VK_HOME,1);pump();
        SCROLLINFO top{sizeof(top),SIF_ALL};
        GetScrollInfo(all,SB_VERT,&top);
        check(top.nPos==0,"Home did not return to the start of the histogram");
        SendMessageW(all,WM_KEYDOWN,VK_NEXT,1);pump();
        SCROLLINFO paged{sizeof(paged),SIF_ALL};
        GetScrollInfo(all,SB_VERT,&paged);
        check(paged.nPos>0&&paged.nPos<bottom.nPos,
            "PageDown did not move within the histogram without changing steps");
        SendMessageW(all,WM_KEYDOWN,VK_END,1);pump();
        SCROLLINFO ended{sizeof(ended),SIF_ALL};
        GetScrollInfo(all,SB_VERT,&ended);
        check(ended.nPos==bottom.nPos&&
            name(all).find("Grupo 64: 63, frecuencia 1")!=std::string::npos,
            "End did not reach the final group or changed the accessible values");
        SendMessageW(all,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);pump();
        SCROLLINFO wheeled{sizeof(wheeled),SIF_ALL};
        GetScrollInfo(all,SB_VERT,&wheeled);
        check(wheeled.nPos<ended.nPos,
            "Mouse wheel did not scroll the chart within its own viewport");
        SendMessageW(all,WM_KEYDOWN,VK_HOME,1);pump();
        SCROLLINFO zero{sizeof(zero),SIF_POS};
        GetScrollInfo(all,SB_VERT,&zero);
        check(zero.nPos==0,"The histogram did not restore its original scroll origin");
        check(scaled.step_index()==0,"Scrolling histogram cards modified the summary state");
    }
}
void candidate_frequency_filter(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,850);
    const auto bars=GetDlgItem(view.window(),frequencies_id);
    const auto filter=GetDlgItem(view.window(),frequency_filter_id);
    check(bars&&filter&&!control_shown(filter),
        "The filter is visible before any completed groups exist");
    check(view.go_to_step(2)&&!control_shown(filter),
        "A one-group chart presented a redundant candidate filter");
    check(view.go_to_step(7),"Cannot reach the second completed group");
    const auto original=name(bars);
    check(control_shown(filter)&&native_text(filter)=="Mostrar solo candidatos"&&
        original.find("Frecuencias de 2 grupos terminados")!=std::string::npos,
        "A completed losing group should allow comparing all bars with the leaders");
    const auto reading=[&](const SimulationView& current)->std::string{
        for(const auto& region:current.reading_regions())
            if(region.key.find("/frequencies")!=std::string::npos)return region.source_text;
        return {};
    };
    const auto reading_key=[&](const SimulationView& current)->std::string{
        for(const auto& region:current.reading_regions())
            if(region.key.find("/frequencies/")!=std::string::npos)return region.key;
        return {};
    };
    const auto original_key=reading_key(view);
    check(original_key.find("/frequencies/all")!=std::string::npos,
        "Full-histogram narration has no explicit mode identity");
    const auto output=text(view,output_id);
    const auto step=view.step_index();
    click(view,frequency_filter_id);
    const auto only=name(bars);
    const auto candidate_key=reading_key(view);
    check(candidate_key.find("/frequencies/candidates")!=std::string::npos&&
        candidate_key!=original_key,
        "Filtering the same model step reused the full-histogram narration identity");
    check(view.step_index()==step&&text(view,output_id)==output&&
        native_text(filter)=="Mostrar todos los grupos"&&
        only.find("Frecuencias de 1 grupo candidato entre 2 grupos terminados")!=std::string::npos&&
        only.find("Grupo 1:")==std::string::npos&&
        only.find("Grupo 2: 3, frecuencia 4, candidato al máximo actual")!=std::string::npos&&
        only.find("Grupo 3:")==std::string::npos&&reading(view)==only,
        "Filtering candidates showed a losing/future group or changed the trace state");
    click(view,frequency_filter_id);
    check(name(bars)==original&&native_text(filter)=="Mostrar solo candidatos"&&
        view.step_index()==step&&text(view,output_id)==output&&
        reading_key(view)==original_key,
        "Showing all groups did not restore the same computed bars and reading identity");

    check(view.go_to_step(9),"Cannot complete the third group");
    click(view,frequency_filter_id);
    const auto third=name(bars);
    check(reading_key(view).find("/frequencies/candidates")!=std::string::npos&&
        reading_key(view)!=candidate_key,
        "Candidate mode at a new step reused the previous step's narration identity");
    check(third.find("Frecuencias de 1 grupo candidato entre 3 grupos terminados")!=std::string::npos&&
        third.find("Grupo 1:")==std::string::npos&&
        third.find("Grupo 2: 3, frecuencia 4")!=std::string::npos&&
        third.find("Grupo 3:")==std::string::npos,
        "Candidate-only mode did not account for the last completed losing group");
    check(view.go_to_step(0)&&!control_shown(bars)&&reading(view).empty()&&
        view.step_index()==0,"Returning to the beginning leaked future candidate bars");
    check(view.go_to_step(2)&&name(bars).find("Grupo 2:")==std::string::npos,
        "Returning to an earlier decision leaked a later candidate");
    click(view,frequency_filter_id);
    check(native_text(filter)=="Mostrar solo candidatos",
        "The filter could not be disabled after navigating backwards");
    const auto selector=GetDlgItem(view.window(),scenario_id);
    SendMessageW(selector,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_index()==0&&!control_shown(filter),
        "Changing the scenario did not reset the candidate filter");
    check(view.go_to_step(6),"Cannot reach the tie decision");
    const auto tie=name(bars);
    check(tie.find("Grupo 1: 4, frecuencia 2, candidato al máximo actual")!=std::string::npos&&
        tie.find("Grupo 2: 9, frecuencia 2, candidato al máximo actual")!=std::string::npos&&
        !control_shown(filter),
        "A tie incorrectly hid a candidate or offered an ineffective filter");

    SendMessageW(selector,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(!control_shown(filter)&&!control_shown(bars)&&reading(view).empty(),
        "An empty scenario retained a previous candidate filter");
    check(spec==authored,"Exploring frequency filters mutated authored lesson data");

    auto summary=fixture(SimulationViewMode::summary);
    SimulationView final(host,summary,"es");resize(final,850);
    const auto summary_bars=GetDlgItem(final.window(),frequencies_id);
    const auto summary_filter=GetDlgItem(final.window(),frequency_filter_id);
    check(control_shown(summary_filter),"Summary with losing groups has no candidate filter");
    const auto summary_full_key=reading_key(final);
    click(final,frequency_filter_id);
    check(reading_key(final)!=summary_full_key&&
        reading_key(final).find("/frequencies/candidates")!=std::string::npos,
        "Static summary reused the full-histogram reading identity after filtering");
    check(final.step_index()==0&&name(summary_bars).find("Frecuencias de 1 grupo candidato")!=std::string::npos&&
        name(summary_bars).find("Grupo 1:")==std::string::npos&&
        name(summary_bars).find("Grupo 3:")==std::string::npos,
        "Filtering a static summary changed a step or included losing groups");

    auto labels=fixture(SimulationViewMode::summary);
    labels.text_values=true;labels.scenarios.clear();
    labels.values={std::string(""),std::string(" "),std::string(" ")};
    SimulationView text_view(host,labels,"es");resize(text_view,850);
    click(text_view,frequency_filter_id);
    const auto text_chart=name(GetDlgItem(text_view.window(),frequencies_id));
    check(text_chart.find("Grupo 1:")==std::string::npos&&
        text_chart.find("Grupo 2: \" \" (1 espacio), frecuencia 2")!=std::string::npos&&
        text_chart.find("Frecuencias de 1 grupo candidato")!=std::string::npos,
        "A whitespace-only candidate was confused with an empty string when filtering");

    auto many=fixture(SimulationViewMode::summary);many.scenarios.clear();
    many.values.clear();
    for(std::int64_t value=0;value<62;++value)many.values.emplace_back(value);
    many.values.emplace_back(std::int64_t{0});
    SimulationView scaled(host,many,"es");scaled.set_text_scale(150);
    resize(scaled,340);bounds(scaled,"many histogram candidates at narrow zoom");
    const auto many_bars=GetDlgItem(scaled.window(),frequencies_id);
    SCROLLINFO before{sizeof(before),SIF_ALL};
    check(GetScrollInfo(many_bars,SB_VERT,&before)&&
        before.nMax+1>static_cast<int>(before.nPage),
        "The unfiltered 62-group chart does not have an internal scroll range");
    click(scaled,frequency_filter_id);
    RECT filtered_viewport{};GetClientRect(many_bars,&filtered_viewport);
    check(name(many_bars).find("Frecuencias de 1 grupo candidato entre 62 grupos terminados")!=std::string::npos&&
        name(many_bars).find("Grupo 2:")==std::string::npos&&
        !(GetWindowLongPtrW(many_bars,GWL_STYLE)&WS_TABSTOP)&&
        filtered_viewport.bottom>0&&filtered_viewport.bottom<=MulDiv(MulDiv(440,
            static_cast<int>(GetDpiForWindow(many_bars)),96),150,100),
        "Filtering the long chart failed to shrink its viewport and drop the scroll focus");
    click(scaled,frequency_filter_id);
    SCROLLINFO after{sizeof(after),SIF_ALL};
    check(GetScrollInfo(many_bars,SB_VERT,&after)&&after.nPos==0&&
        after.nMax+1>static_cast<int>(after.nPage)&&
        name(many_bars).find("Grupo 62: 61, frecuencia 1")!=std::string::npos,
        "Restoring all 62 groups lost the full scroll range or the last group");
}
void calculated_step_changes(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    const auto trace=build_simulation_trace(spec);
    SimulationView view(host,spec,"es");resize(view,820);
    const auto delta=GetDlgItem(view.window(),transition_id);
    check(delta&&!control_shown(delta)&&text(view,transition_id).empty(),
        "An initial frame fabricated a previous state");
    const auto delta_key=[&](const SimulationView& current){
        for(const auto& region:current.reading_regions())
            if(region.key.find("/transition")!=std::string::npos)return region.key;
        return std::string{};
    };
    check(delta_key(view).empty(),"The reader exposed a delta before the first observation");
    check(view.go_to_step(1),"The first counted frame is missing");
    const auto first=text(view,transition_id);
    check(control_shown(delta)&&first.find("Cambios desde el estado anterior:")!=std::string::npos&&
        first.find("grupo: 0 → 1")!=std::string::npos&&
        first.find("fin: 0 → 1")!=std::string::npos&&
        first.find("frecuencia: 0 → 1")!=std::string::npos&&
        first.find("Salida:") == std::string::npos,
        "The first count does not show its exact changed variables");
    check(first.find("Dato contado: índice 0 = -8")!=std::string::npos&&
        first.find("Comparación:")==std::string::npos,
        "The first count lost the value and index actually observed");
    const auto first_key=delta_key(view);
    check(!first_key.empty(),"The current-step changes are missing from narration");
    check(view.go_to_step(2),"Cannot inspect a completed group decision");
    const auto maximum=text(view,transition_id);
    check(maximum.find("máxima: 0 → 1")!=std::string::npos&&
        maximum.find("mayores: ninguno → -8")!=std::string::npos&&
        maximum.find("Salida: +1 línea")!=std::string::npos&&
        maximum.find("10:1")==std::string::npos&&delta_key(view)!=first_key,
        "A decision delta omitted the new maximum or exposed future output");
    check(maximum.find("Comparación: frecuencia 1 > máxima anterior 0. Nuevo máximo: reemplaza candidatos.")!=std::string::npos&&
        maximum.find("Dato contado:")==std::string::npos,
        "The first decision used a future maximum or incorrectly counted a new element");
    check(view.go_to_step(4),"Cannot select a later count directly");
    const auto progress=text(view,transition_id);
    check(progress.find("fin: 2 → 3")!=std::string::npos&&
        progress.find("frecuencia: 1 → 2")!=std::string::npos&&
        progress.find("máxima:")==std::string::npos&&
        progress.find("Salida:")==std::string::npos,
        "A direct jump compared the last visited frame instead of adjacent model states");
    check(progress.find("Dato contado: índice 2 = 3")!=std::string::npos&&
        progress.find("Comparación:")==std::string::npos,
        "Jumping to a count used another frame's observed element");
    check(view.go_to_step(7),"Cannot inspect a later maximum");
    const auto new_max=text(view,transition_id);
    check(new_max.find("máxima: 1 → 4")!=std::string::npos&&
        new_max.find("mayores: -8 → 3")!=std::string::npos&&
        new_max.find("Salida: +1 línea")!=std::string::npos&&
        trace.frames[7].output=="-8:1\n3:4\n",
        "A later decision changed candidates or accumulated output incorrectly");
    check(new_max.find("Comparación: frecuencia 4 > máxima anterior 1. Nuevo máximo: reemplaza candidatos.")!=std::string::npos,
        "The larger group did not explain its verified strict comparison");
    check(view.go_to_step(9),"Cannot inspect the losing group");
    const auto below=text(view,transition_id);
    check(below.find("Comparación: frecuencia 1 < máxima anterior 4. No supera el máximo: conserva los candidatos.")!=std::string::npos&&
        below.find("máxima:")==std::string::npos&&
        below.find("mayores:")==std::string::npos,
        "The losing group wrongly changed the maximum or concealed the comparison");
    check(view.go_to_step(view.step_count()-1),"The terminal frame is missing");
    check(text(view,transition_id).find("frecuencia: 1 → 0")!=std::string::npos&&
        text(view,transition_id).find("Salida:")==std::string::npos,
        "The final frame invented another emitted line");
    check(view.go_to_step(0)&&!control_shown(delta)&&delta_key(view).empty(),
        "Reset leaked a completed transition to the initial frame");
    check(spec==authored,"Navigating transitions modified the authored lesson");
    set_edit_text(GetDlgItem(view.window(),input_id),"2147483648");
    click(view,apply_id);
    check(!text(view,error_id).empty()&&text(view,transition_id).empty(),
        "Invalid edited data fabricated a transition despite rejection");
    set_edit_text(GetDlgItem(view.window(),input_id),"1\n3\n1");
    click(view,apply_id);
    check(view.go_to_step(2)&&text(view,transition_id).find("mayores: ninguno → 1")!=std::string::npos,
        "The transition still uses the old data after applying a new valid array");
    click(view,restore_id);
    check(view.step_index()==0&&!control_shown(delta)&&
        view.go_to_step(2)&&text(view,transition_id)==maximum,
        "Restoration did not reconstruct the original verified transition");

    const auto tie_scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(tie_scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(tie_scenario));pump();
    check(view.go_to_step(6),"The tie scenario lacks its equality decision");
    const auto tie=text(view,transition_id);
    check(tie.find("Comparación: frecuencia 2 = máxima anterior 2. Empate: agrega el valor a los candidatos.")!=std::string::npos&&
        tie.find("mayores: 4 → 4, 9")!=std::string::npos,
        "Equal frequencies were not explained as a tie with both candidates");
    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(view.step_count()==1&&text(view,transition_id).empty()&&!control_shown(delta)&&
        delta_key(view).empty(),"An empty scenario retained a previous data transition");

    for(const auto mode:{SimulationViewMode::groups,SimulationViewMode::candidates}) {
        auto derived=fixture(mode);derived.editable=false;
        SimulationView other(host,derived,"es");resize(other,850);
        const auto current=text(other,transition_id);
        check(current.find("grupo: 0 → 1")!=std::string::npos&&
            current.find("Salida: +1 línea")!=std::string::npos,
            "A completed-group view did not compare its first group with the empty state");
        check(other.go_to_step(1),"Cannot inspect the next completed group");
        const auto next=text(other,transition_id);
        check(current.find("Comparación: frecuencia 1 > máxima anterior 0. Nuevo máximo: reemplaza candidatos.")!=std::string::npos&&
            next.find("Comparación: frecuencia 4 > máxima anterior 1. Nuevo máximo: reemplaza candidatos.")!=std::string::npos,
            "Group decisions did not use each preceding completed group");
        check(next.find("grupo: 1 → 2")!=std::string::npos&&
            next.find("máxima: 1 → 4")!=std::string::npos&&
            next.find("Salida: +1 línea")!=std::string::npos,
            "Group navigation does not compare adjacent completed groups");
    }
    for(const auto mode:{SimulationViewMode::array,SimulationViewMode::summary}) {
        auto static_spec=fixture(mode);
        SimulationView still(host,static_spec,"es");resize(still,820);
        check(!control_shown(GetDlgItem(still.window(),transition_id))&&
            text(still,transition_id).empty(),
            "A static simulation fabricated an intermediate transition");
    }
    auto texts=fixture(SimulationViewMode::walkthrough);
    texts.text_values=true;texts.scenarios.clear();
    texts.values={std::string(""),std::string(" "),std::string(" ")};
    SimulationView quoted(host,texts,"es");resize(quoted,820);
    check(quoted.go_to_step(1)&&
        text(quoted,transition_id).find("Dato contado: índice 0 = \"\" (cadena vacía)")!=std::string::npos,
        "An empty text value was confused with no observed element");
    check(quoted.go_to_step(2)&&
        text(quoted,transition_id).find("mayores: ninguno → \"\" (cadena vacía)")!=std::string::npos,
        "An empty text candidate was confused with no candidate");
    check(quoted.go_to_step(3)&&
        text(quoted,transition_id).find("Dato contado: índice 1 = \" \" (1 espacio)")!=std::string::npos,
        "The observation discarded the significant space in a text value");
    check(quoted.go_to_step(5)&&
        text(quoted,transition_id).find("mayores: \"\" (cadena vacía) → \" \" (1 espacio)")!=std::string::npos,
        "Significant spaces were discarded from a candidate change");

    auto multiline=fixture(SimulationViewMode::walkthrough);
    multiline.scenarios.clear();multiline.output_template="Valor {value}\nFrecuencia {frequency}";
    SimulationView lines(host,multiline,"es");resize(lines,820);
    check(lines.go_to_step(2)&&
        text(lines,transition_id).find("Salida: +2 líneas")!=std::string::npos,
        "Multiple output lines were incorrectly counted as one");
    auto long_labels=fixture(SimulationViewMode::walkthrough);
    long_labels.scenarios.clear();
    long_labels.labels.leaders=std::string(80,'L');
    long_labels.values.clear();
    for(int value=0;value<64;++value)long_labels.values.emplace_back(static_cast<std::int64_t>(value));
    SimulationView narrow(host,long_labels,"es");narrow.set_text_scale(150);
    resize(narrow,340);bounds(narrow,"long transition labels at 150 percent");
    check(narrow.go_to_step(2)&&
        !text(narrow,transition_id).empty(),"Long changed-variable labels were clipped or hidden");
}
void neighboring_values_explain_group_boundaries(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,820);
    const auto current=[&]{return text(view,transition_id);};
    const auto reading=[&]{
        for(const auto& region:view.reading_regions())
            if(region.key.find("/transition")!=std::string::npos)
                return region.source_text;
        return std::string{};
    };
    check(view.step_index()==0&&current().empty()&&reading().empty(),
        "The initial frame invented a previous-element comparison");
    check(view.go_to_step(1)&&
        current().find("Dato contado: índice 0 = -8")!=std::string::npos&&
        current().find("Primera posición: el índice 0 no tiene un elemento anterior.")!=std::string::npos&&
        current().find("Relación con el anterior:")==std::string::npos,
        "The first element tried to read the preceding index -1");
    check(view.go_to_step(2)&&
        current().find("Relación con el anterior:")==std::string::npos,
        "A completed-group decision falsely compared another pair of elements");

    check(view.go_to_step(3),"Missing the first element of the second group");
    const auto boundary=current();
    check(boundary.find("Relación con el anterior: índice 0 = -8; índice 1 = 3. Son diferentes: comienza un grupo nuevo.")!=std::string::npos&&
        boundary.find("índice 2 =")==std::string::npos&&
        reading()==boundary,
        "The second group did not explain the distinct previous observed value");
    check(view.go_to_step(4),"Missing the repeated value");
    const auto equal=current();
    check(equal.find("Relación con el anterior: índice 1 = 3; índice 2 = 3. Son iguales: continúa el grupo.")!=std::string::npos&&
        equal.find("índice 3 =")==std::string::npos,
        "Repeated neighbors did not explain why the group continues");
    check(view.go_to_step(6)&&
        current().find("Relación con el anterior: índice 3 = 3; índice 4 = 3. Son iguales: continúa el grupo.")!=std::string::npos,
        "The last repeated value compared a future neighbor or wrong index");
    check(view.go_to_step(8)&&
        current().find("Relación con el anterior: índice 4 = 3; índice 5 = 10. Son diferentes: comienza un grupo nuevo.")!=std::string::npos&&
        current().find("índice 6 =")==std::string::npos,
        "The final group compared a past visited frame rather than adjacent values");
    for(const auto stage:{7,9,10}){
        check(view.go_to_step(static_cast<std::size_t>(stage))&&
            current().find("Relación con el anterior:")==std::string::npos&&
            current().find("Primera posición:")==std::string::npos,
            "Decision or final frames fabricated a new comparison of neighbors");
    }
    check(view.go_to_step(3)&&current()==boundary&&
        view.go_to_step(4)&&current()==equal,
        "Direct jumps rewrote neighboring-element comparisons");

    const auto selector=GetDlgItem(view.window(),scenario_id);
    SendMessageW(selector,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_index()==0&&current().empty(),
        "A scenario change retained a previous neighboring comparison");
    check(view.go_to_step(4)&&
        current().find("Relación con el anterior: índice 1 = 4; índice 2 = 9. Son diferentes: comienza un grupo nuevo.")!=std::string::npos,
        "The tie scenario used old array values for the group boundary");

    set_edit_text(GetDlgItem(view.window(),input_id),"1\n1\n2");
    click(view,apply_id);
    check(view.step_index()==0&&current().empty(),
        "Applying new data kept the previous comparison visible");
    check(view.go_to_step(2)&&
        current().find("Relación con el anterior: índice 0 = 1; índice 1 = 1. Son iguales: continúa el grupo.")!=std::string::npos,
        "Newly applied equal values did not recalculate the comparison");
    check(view.go_to_step(4)&&
        current().find("Relación con el anterior: índice 1 = 1; índice 2 = 2. Son diferentes: comienza un grupo nuevo.")!=std::string::npos,
        "Newly applied distinct values reused an older boundary");
    const auto saved=current();
    set_edit_text(GetDlgItem(view.window(),input_id),"2147483648");
    click(view,apply_id);
    check(!text(view,error_id).empty()&&current()==saved,
        "Rejected input incorrectly replaced the applied neighbor comparison");
    click(view,restore_id);
    check(view.step_index()==0&&current().empty()&&
        view.go_to_step(4)&&
        current().find("Relación con el anterior: índice 1 = 4; índice 2 = 9.")!=std::string::npos,
        "Restoring a scenario did not recover its own neighbor values");

    SendMessageW(selector,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_count()==1&&current().empty()&&reading().empty(),
        "An empty array fabricated predecessor comparisons");

    auto text_spec=fixture(SimulationViewMode::walkthrough);
    text_spec.text_values=true;text_spec.scenarios.clear();
    text_spec.values={std::string(""),std::string(" "),std::string(" ")};
    SimulationView words(host,text_spec,"es");resize(words,820);
    check(words.go_to_step(3)&&
        text(words,transition_id).find(
            "Relación con el anterior: índice 0 = \"\" (cadena vacía); índice 1 = \" \" (1 espacio). Son diferentes: comienza un grupo nuevo.")!=std::string::npos,
        "An empty text value was confused with a significant space at the boundary");
    check(words.go_to_step(4)&&
        text(words,transition_id).find(
            "Relación con el anterior: índice 1 = \" \" (1 espacio); índice 2 = \" \" (1 espacio). Son iguales: continúa el grupo.")!=std::string::npos,
        "Two space-only text values were incorrectly treated as distinct");

    auto unchanged=fixture(SimulationViewMode::walkthrough);
    unchanged.scenarios.clear();unchanged.sort=false;
    unchanged.values=numbers({-8,0,0});
    SimulationView already_sorted(host,unchanged,"es");resize(already_sorted,820);
    check(already_sorted.go_to_step(3)&&
        text(already_sorted,transition_id).find(
            "Relación con el anterior: índice 0 = -8; índice 1 = 0. Son diferentes: comienza un grupo nuevo.")!=std::string::npos,
        "Neighbor comparisons should work without sorting a copy");
    for(const auto mode:{SimulationViewMode::array,SimulationViewMode::summary,
                         SimulationViewMode::groups,SimulationViewMode::candidates}){
        auto other_spec=fixture(mode);
        SimulationView other(host,other_spec,"es");resize(other,820);
        check(text(other,transition_id).find("Relación con el anterior:")==std::string::npos,
            "Non-walkthrough views incorrectly claimed to count a new element");
    }
    view.set_text_scale(150);resize(view,340);
    bounds(view,"empty-neighbor scenario at 150 percent");
    check(spec==authored,"Neighbor-comparison exploration modified the authored lesson");
}
void completed_scenario_comparison(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,850);
    const auto comparison=GetDlgItem(view.window(),result_comparison_id);
    const auto reading_key=[&](const SimulationView& current) {
        for(const auto& region:current.reading_regions())
            if(region.key.find("/result-comparison")!=std::string::npos)return region.key;
        return std::string{};
    };
    check(comparison&&!control_shown(comparison)&&
        text(view,result_comparison_id).empty()&&reading_key(view).empty(),
        "Authored input must not compare itself with itself");

    const auto selector=GetDlgItem(view.window(),scenario_id);
    SendMessageW(selector,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_count()==8&&view.step_index()==0&&!control_shown(comparison)&&
        text(view,result_comparison_id).empty()&&reading_key(view).empty(),
        "Selecting a scenario exposed future final results before its walkthrough");
    check(view.go_to_step(6)&&text(view,output_id)=="4:2\n9:2\n"&&
        text(view,result_comparison_id).empty()&&!control_shown(comparison),
        "A final group decision revealed the scenario comparison before Done");
    check(view.go_to_step(7),"Cannot reach the completed scenario");
    const auto tie=text(view,result_comparison_id);
    check(control_shown(comparison)&&
        tie.find("Comparación de resultados: datos iniciales → datos aplicados")!=std::string::npos&&
        tie.find("Cantidad de datos: 6 → 4")!=std::string::npos&&
        tie.find("Cantidad de grupos: 3 → 2")!=std::string::npos&&
        tie.find("máxima: 4 → 2")!=std::string::npos&&
        tie.find("mayores: 3 → 4, 9")!=std::string::npos&&
        !reading_key(view).empty()&&spec==authored,
        "The complete scenario comparison changed authored data or final statistics");
    const auto tie_key=reading_key(view);
    check(view.go_to_step(0)&&text(view,result_comparison_id).empty()&&
        !control_shown(comparison)&&reading_key(view).empty(),
        "Going backwards leaked a final comparison into the first step");
    check(view.go_to_step(7)&&reading_key(view)==tie_key&&
        text(view,result_comparison_id)==tie,
        "Returning to the final step did not recover the deterministic comparison");

    set_edit_text(GetDlgItem(view.window(),input_id),"2147483648");
    click(view,apply_id);
    check(!text(view,error_id).empty()&&text(view,result_comparison_id)==tie,
        "Rejected input replaced the applied comparison with an invalid draft");

    set_edit_text(GetDlgItem(view.window(),input_id),"7\n7\n7");
    click(view,apply_id);
    check(view.step_index()==0&&!control_shown(comparison)&&
        reading_key(view).empty(),"A newly applied array displayed its result too early");
    check(view.go_to_step(view.step_count()-1),"Cannot complete an edited scenario");
    const auto edited=text(view,result_comparison_id);
    check(edited.find("Cantidad de datos: 6 → 3")!=std::string::npos&&
        edited.find("Cantidad de grupos: 3 → 1")!=std::string::npos&&
        edited.find("máxima: 4 → 3")!=std::string::npos&&
        edited.find("mayores: 3 → 7")!=std::string::npos&&
        reading_key(view)!=tie_key,
        "Applying edited values retained the previous scenario result");

    click(view,restore_id);
    check(view.step_index()==0&&!control_shown(comparison)&&
        text(view,result_comparison_id).empty()&&reading_key(view).empty(),
        "Restoring a scenario kept the old comparison");
    SendMessageW(selector,CB_SETCURSEL,0,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.go_to_step(view.step_count()-1)&&!control_shown(comparison)&&
        spec==authored,"Returning to authored values displayed a redundant comparison");

    SendMessageW(selector,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    const auto empty=text(view,result_comparison_id);
    check(view.step_count()==1&&control_shown(comparison)&&
        empty.find("Cantidad de datos: 6 → 0")!=std::string::npos&&
        empty.find("Cantidad de grupos: 3 → 0")!=std::string::npos&&
        empty.find("máxima: 4 → 0")!=std::string::npos&&
        empty.find("mayores: 3 → ninguno")!=std::string::npos,
        "An empty scenario failed to explain its terminal outcome");

    for(const auto mode:{SimulationViewMode::groups,SimulationViewMode::candidates}) {
        auto other_spec=fixture(mode);
        SimulationView other(host,other_spec,"es");resize(other,850);
        const auto scenario=GetDlgItem(other.window(),scenario_id);
        SendMessageW(scenario,CB_SETCURSEL,1,0);
        SendMessageW(other.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
            reinterpret_cast<LPARAM>(scenario));pump();
        check(!control_shown(GetDlgItem(other.window(),result_comparison_id))&&
            other.step_count()==2,
            "A group-level view revealed a future scenario outcome");
        check(other.go_to_step(1)&&
            text(other,result_comparison_id).find("mayores: 3 → 4, 9")!=std::string::npos,
            "The last completed group did not reveal its scenario comparison");
    }
    {
        auto summary=fixture(SimulationViewMode::summary);
        SimulationView view_summary(host,summary,"es");resize(view_summary,850);
        set_edit_text(GetDlgItem(view_summary.window(),input_id),"3\n3\n3\n3\n10\n-8");
        click(view_summary,apply_id);
        const auto equivalent=text(view_summary,result_comparison_id);
        check(control_shown(GetDlgItem(view_summary.window(),result_comparison_id))&&
            equivalent.find("Cantidad de datos: 6 → 6 (sin cambio)")!=std::string::npos&&
            equivalent.find("Cantidad de grupos: 3 → 3 (sin cambio)")!=std::string::npos&&
            equivalent.find("máxima: 4 → 4 (sin cambio)")!=std::string::npos&&
            equivalent.find("mayores: 3 → 3 (sin cambio)")!=std::string::npos,
            "A reordered input with the same result should explain what stayed equal");
    }
    {
        auto strings=fixture(SimulationViewMode::summary);
        strings.scenarios.clear();strings.text_values=true;
        strings.values={std::string(""),std::string(" "),std::string(" ")};
        SimulationView view_strings(host,strings,"es");resize(view_strings,850);
        set_edit_text(GetDlgItem(view_strings.window(),input_id),"[\"\", \"\"]");
        click(view_strings,apply_id);
        const auto comparison_text=text(view_strings,result_comparison_id);
        check(comparison_text.find("máxima: 2 → 2 (sin cambio)")!=std::string::npos&&
            comparison_text.find("mayores: \" \" (1 espacio) → \"\" (cadena vacía)")!=std::string::npos,
            "Comparison confused an empty text candidate with a significant space");
    }
    {
        auto static_spec=fixture(SimulationViewMode::array);
        SimulationView static_view(host,static_spec,"es");resize(static_view,850);
        const auto scenario=GetDlgItem(static_view.window(),scenario_id);
        SendMessageW(scenario,CB_SETCURSEL,1,0);
        SendMessageW(static_view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
            reinterpret_cast<LPARAM>(scenario));pump();
        check(!control_shown(GetDlgItem(static_view.window(),result_comparison_id))&&
            text(static_view,result_comparison_id).empty(),
            "An array-only view disclosed final candidates without a terminal step");
    }
}
void decision_navigation(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    const auto computed=build_simulation_trace(spec);
    check(computed.candidate_frames==std::vector<std::size_t>({2,7,9}),
        "Fixture no longer exposes three independently calculated decisions");
    int changes=0,layouts=0;
    SimulationView view(host,spec,"es",{
        [&]{++changes;},[&]{++layouts;}
    });
    resize(view,850);
    const auto previous=GetDlgItem(view.window(),previous_decision_id);
    const auto next=GetDlgItem(view.window(),next_decision_id);
    check(previous&&next&&control_shown(previous)&&control_shown(next)&&
        !IsWindowEnabled(previous)&&IsWindowEnabled(next),
        "Walkthrough decision controls are absent or have wrong initial bounds");
    const auto pick=[&](std::size_t index,std::string_view output) {
        check(view.step_index()==index&&text(view,output_id)==output&&
            text(view,explanation_id)==computed.frames[index].explanation&&
            SendMessageW(GetDlgItem(view.window(),step_selector_id),CB_GETCURSEL,0,0)==static_cast<LRESULT>(index),
            "Decision navigation did not select the exact model state and current narration");
    };
    click(view,next_decision_id);pick(2,"-8:1\n");
    check(!IsWindowEnabled(previous)&&IsWindowEnabled(next),
        "First decision is not the earliest available decision");
    click(view,next_decision_id);pick(7,"-8:1\n3:4\n");
    click(view,next_decision_id);pick(9,"-8:1\n3:4\n10:1\n");
    check(!IsWindowEnabled(next)&&IsWindowEnabled(previous),
        "Final decision incorrectly allows jumping past its last comparison");
    click(view,previous_decision_id);pick(7,"-8:1\n3:4\n");
    click(view,previous_decision_id);pick(2,"-8:1\n");
    check(!IsWindowEnabled(previous),"First decision has a nonexistent previous comparison");
    check(view.go_to_step(8)&&IsWindowEnabled(previous)&&IsWindowEnabled(next),
        "Between two decisions the controls did not offer both directions");
    click(view,previous_decision_id);pick(7,"-8:1\n3:4\n");
    check(view.go_to_step(8),"Unable to inspect the state before the last decision");
    click(view,next_decision_id);pick(9,"-8:1\n3:4\n10:1\n");
    check(view.go_to_step(10)&&IsWindowEnabled(previous)&&!IsWindowEnabled(next),
        "The terminal state invented another future decision");
    click(view,previous_decision_id);pick(9,"-8:1\n3:4\n10:1\n");
    click(view,reset_id);pick(0,"");
    check(!IsWindowEnabled(previous)&&IsWindowEnabled(next),
        "Reset retained navigation history instead of current model decisions");
    check(changes>=10&&layouts==changes&&spec==authored,
        "Decision jumps modified authored input or failed to refresh the reader");

    set_edit_text(GetDlgItem(view.window(),input_id),"5\n5\n5");
    click(view,apply_id);
    check(view.step_count()==6&&!IsWindowEnabled(previous)&&IsWindowEnabled(next),
        "Replacing the array retained decision targets from the previous trace");
    click(view,next_decision_id);
    check(view.step_index()==4&&text(view,output_id)=="5:3\n"&&
        !IsWindowEnabled(previous)&&!IsWindowEnabled(next),
        "A single group introduced a second decision or reused previous outputs");
    click(view,restore_id);pick(0,"");
    click(view,next_decision_id);pick(2,"-8:1\n");
    check(IsWindowEnabled(next)&&spec==authored,
        "Restoration did not rebuild all original decision targets");

    const auto selector=GetDlgItem(view.window(),scenario_id);
    SendMessageW(selector,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_index()==0&&view.step_count()==8&&IsWindowEnabled(next),
        "Scenario change retained previous group count or jump position");
    click(view,next_decision_id);
    check(view.step_index()==3&&text(view,output_id)=="4:2\n",
        "First scenario decision was not derived from the selected input");
    click(view,next_decision_id);
    check(view.step_index()==6&&text(view,output_id)=="4:2\n9:2\n"&&
        !IsWindowEnabled(next),"Tie scenario skipped its equality decision");
    click(view,previous_decision_id);
    check(view.step_index()==3&&text(view,output_id)=="4:2\n",
        "Backward scenario jump retained a future candidate");

    SendMessageW(selector,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_count()==1&&view.step_index()==0&&
        !control_shown(previous)&&!control_shown(next)&&
        !IsWindowEnabled(previous)&&!IsWindowEnabled(next),
        "An empty array exposed nonexistent decision navigation");
    for(const auto mode:{SimulationViewMode::array,SimulationViewMode::groups,
                         SimulationViewMode::candidates,SimulationViewMode::summary}) {
        auto different=fixture(mode);
        SimulationView other(host,different,"es");resize(other,850);
        check(!control_shown(GetDlgItem(other.window(),previous_decision_id))&&
            !control_shown(GetDlgItem(other.window(),next_decision_id)),
            "Decision shortcuts appeared outside a step-by-step walkthrough");
    }
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
// Inspect the rendered footer of a particular cell using the native
// WM_PRINT path; no desktop input or visible QA window is required.
std::vector<COLORREF> counted_cell_footer_pixels(SimulationView& view,std::size_t index) {
    const auto region=view.cell_bounds_in_canvas(index);
    check(region.has_value(),"Cannot sample a cell outside the applied data");
    const auto canvas=GetDlgItem(view.window(),array_id);
    RECT client{};GetClientRect(canvas,&client);
    const auto screen=GetDC(canvas);
    check(screen!=nullptr,"Cannot access the native array paint surface");
    const auto dc=CreateCompatibleDC(screen);
    const auto bitmap=CreateCompatibleBitmap(screen,std::max(1L,client.right),std::max(1L,client.bottom));
    check(dc&&bitmap,"Cannot create a native array pixel snapshot");
    const auto old=SelectObject(dc,bitmap);
    SendMessageW(canvas,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
    const int dpi=static_cast<int>(GetDpiForWindow(canvas));
    const int inset=std::max(2,MulDiv(3,dpi,96));
    const int footer=std::max(8,MulDiv(19,dpi,96));
    std::vector<COLORREF> pixels;
    for(int y=std::max(region->top+inset,region->bottom-inset-footer);
            y<region->bottom-inset;y+=2){
        for(int x=region->left+inset;x<region->right-inset;x+=2)
            pixels.push_back(GetPixel(dc,x,y));
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(canvas,screen);
    check(!pixels.empty(),"The counted-cell footer was clipped to an empty rectangle");
    return pixels;
}
void walkthrough_processed_progress(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,850);
    const auto progress=GetDlgItem(view.window(),progress_id);
    const auto label=GetDlgItem(view.window(),progress_label_id);
    check(progress&&label&&control_shown(progress)&&control_shown(label),
        "The walkthrough lacks its native processed-element progress");
    wchar_t klass[96]{};GetClassNameW(progress,klass,96);
    check(std::wstring_view(klass)==PROGRESS_CLASSW,
        "The processed-element display is not a native Windows progress control");
    const auto progress_value=[&]{
        return SendMessageW(progress,PBM_GETPOS,0,0);
    };
    const auto progress_reading=[&](const SimulationView& current){
        for(const auto& region:current.reading_regions())
            if(region.key.find("/progress")!=std::string::npos)return region.source_text;
        return std::string{};
    };
    const auto check_stage=[&](std::size_t index,int expected,std::string_view caption){
        check(view.go_to_step(index),"Cannot reach a computed progress frame");
        check(progress_value()==expected&&text(view,progress_label_id)==caption&&
            progress_reading(view)==caption,
            "The progress bar, native label and current narration disagree");
    };
    check(SendMessageW(progress,PBM_GETRANGE,FALSE,0)==6&&
        !(GetWindowLongPtrW(progress,GWL_STYLE)&WS_TABSTOP),
        "A progress bar has an incorrect range or consumes a keyboard tab stop");
    check_stage(0,0,"Elementos recorridos: 0 de 6");
    check_stage(1,1,"Elementos recorridos: 1 de 6");
    check_stage(2,1,"Elementos recorridos: 1 de 6");
    check_stage(4,3,"Elementos recorridos: 3 de 6");
    check_stage(7,5,"Elementos recorridos: 5 de 6");
    check_stage(8,6,"Elementos recorridos: 6 de 6");
    check(text(view,output_id)=="-8:1\n3:4\n"&&
        text(view,progress_label_id)=="Elementos recorridos: 6 de 6",
        "Reading the last element accidentally completed the final group decision");
    check_stage(9,6,"Elementos recorridos: 6 de 6");
    check_stage(view.step_count()-1,6,"Elementos recorridos: 6 de 6");
    check_stage(3,2,"Elementos recorridos: 2 de 6");
    check_stage(0,0,"Elementos recorridos: 0 de 6");
    if(!captures.empty())capture(view.window(),captures/"processed-progress-initial.bmp");

    view.set_text_scale(150);resize(view,340);
    bounds(view,"processed progress at 150 percent");
    check(control_shown(progress)&&progress_value()==0&&
        text(view,progress_label_id)=="Elementos recorridos: 0 de 6",
        "Narrow zoom clipped or reset the native progress values");
    click_array_cell(view,4);
    check(view.step_index()==6&&progress_value()==5&&
        text(view,progress_label_id)=="Elementos recorridos: 5 de 6",
        "Selecting a rendered cell did not update progress to its exclusive end");
    if(!captures.empty())capture(view.window(),captures/"processed-progress-zoom-150.bmp");

    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(SendMessageW(progress,PBM_GETRANGE,FALSE,0)==4&&
        progress_value()==0&&text(view,progress_label_id)=="Elementos recorridos: 0 de 4",
        "Changing scenario retained the previous array's progress range");
    check_stage(3,2,"Elementos recorridos: 2 de 4");
    check_stage(6,4,"Elementos recorridos: 4 de 4");
    const auto good=text(view,progress_label_id);
    set_edit_text(GetDlgItem(view.window(),input_id),"2147483648");
    click(view,apply_id);
    check(!text(view,error_id).empty()&&progress_value()==4&&
        text(view,progress_label_id)==good,
        "A rejected edit corrupted the last valid processed-element progress");
    set_edit_text(GetDlgItem(view.window(),input_id),"5\n5");
    click(view,apply_id);
    check(view.step_index()==0&&
        SendMessageW(progress,PBM_GETRANGE,FALSE,0)==2&&progress_value()==0,
        "Applying new valid values retained the old progress range or position");
    check_stage(view.step_count()-1,2,"Elementos recorridos: 2 de 2");
    click(view,restore_id);
    check(view.step_index()==0&&
        SendMessageW(progress,PBM_GETRANGE,FALSE,0)==4&&progress_value()==0,
        "Restoring current scenario did not reset the native progress");

    SendMessageW(scenario,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(view.step_count()==1&&!control_shown(progress)&&
        !control_shown(label)&&progress_reading(view).empty()&&
        progress_value()==0,
        "An empty scenario exposed a misleading progress indicator");
    SendMessageW(scenario,CB_SETCURSEL,0,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(view.step_index()==0&&control_shown(progress)&&
        SendMessageW(progress,PBM_GETRANGE,FALSE,0)==6&&progress_value()==0,
        "Restoring original authored input did not recover its progress range");
    check(spec==authored,"Visual progress modified the authored simulation contract");

    for(const auto mode:{SimulationViewMode::array,SimulationViewMode::groups,
                         SimulationViewMode::candidates,SimulationViewMode::summary}) {
        auto other_spec=fixture(mode);
        SimulationView other(host,other_spec,"es");resize(other,840);
        check(!control_shown(GetDlgItem(other.window(),progress_id))&&
            !control_shown(GetDlgItem(other.window(),progress_label_id))&&
            progress_reading(other).empty(),
            "A non-walkthrough view invented a processed-element progress");
    }

    auto text_spec=fixture(SimulationViewMode::walkthrough);
    text_spec.text_values=true;text_spec.scenarios.clear();
    text_spec.values={std::string(""),std::string(" "),std::string(" ")};
    SimulationView text_view(host,text_spec,"es");resize(text_view,820);
    check(text_view.go_to_step(1)&&
        SendMessageW(GetDlgItem(text_view.window(),progress_id),PBM_GETPOS,0,0)==1&&
        text(text_view,progress_label_id)=="Elementos recorridos: 1 de 3",
        "Counting an empty text value changed the total element count");
    check(text_view.go_to_step(3)&&
        text(text_view,progress_label_id)=="Elementos recorridos: 2 de 3",
        "Counting a space-only value skipped an array position");
}
void counted_cell_emphasis(HWND host,const std::filesystem::path& captures) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,1050);
    const auto canvas=GetDlgItem(view.window(),array_id);
    const auto initial=name(canvas);
    check(initial.find("Casilla contada ahora:")==std::string::npos,
        "The initial model state fabricated a counted element");

    check(view.go_to_step(4),"Cannot reach a calculated advance frame");
    const auto second_count=name(canvas);
    check(second_count.find("Casilla contada ahora: índice 2 = 3.")!=std::string::npos&&
        second_count.find("Casilla contada ahora: índice 3") ==std::string::npos,
        "The highlighted cell does not identify the current observation");
    const auto footer_when_counted=counted_cell_footer_pixels(view,2);
    const auto regions=view.reading_regions();
    const auto found=std::ranges::find_if(regions,[](const auto& region){
        return GetDlgCtrlID(region.target)==array_id;
    });
    check(found!=regions.end()&&found->source_text==second_count,
        "The counted-cell explanation is missing from native narration");
    if(!captures.empty())capture(view.window(),captures/"counted-cell-index-2.bmp");

    check(view.go_to_step(5),"Cannot reach the next calculated advance frame");
    const auto third_count=name(canvas);
    check(third_count.find("Casilla contada ahora: índice 3 = 3.")!=std::string::npos&&
        third_count.find("Casilla contada ahora: índice 2") ==std::string::npos,
        "Advancing kept the previous cell marked as the newly counted one");
    const auto footer_when_previous=counted_cell_footer_pixels(view,2);
    check(footer_when_counted!=footer_when_previous,
        "The counted-cell outline and non-color footer were not painted");
    if(!captures.empty())capture(view.window(),captures/"counted-cell-index-3.bmp");

    check(view.go_to_step(7)&&name(canvas).find("Casilla contada ahora:")==std::string::npos,
        "The new-maximum comparison pretended to count another element");
    check(view.go_to_step(9)&&name(canvas).find("Casilla contada ahora:")==std::string::npos,
        "A below-maximum decision pretended to count the last element again");
    check(view.go_to_step(view.step_count()-1)&&
        name(canvas).find("Casilla contada ahora:")==std::string::npos,
        "The terminal model state retained a stale counted-cell label");
    check(view.go_to_step(8)&&
        name(canvas).find("Casilla contada ahora: índice 5 = 10.")!=std::string::npos,
        "A new group's first observation did not focus its actual cell");

    view.set_text_scale(150);resize(view,360);
    bounds(view,"counted cell with zoom and multiple array rows");
    click_array_cell(view,4);
    check(view.step_index()==6&&
        name(canvas).find("Casilla contada ahora: índice 4 = 3.")!=std::string::npos,
        "Zoomed cell click did not show the correct current observation");
    if(!captures.empty())capture(view.window(),captures/"counted-cell-narrow-150.bmp");

    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(name(canvas).find("Casilla contada ahora:")==std::string::npos,
        "Scenario replacement kept the previous input's highlighted cell");
    click_array_cell(view,3);
    check(name(canvas).find("Casilla contada ahora: índice 3 = 9.")!=std::string::npos,
        "The tie scenario used a stale observed value or sorted index");
    SendMessageW(scenario,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(name(canvas).find("Casilla contada ahora:")==std::string::npos&&
        !view.cell_bounds_in_canvas(0),
        "An empty scenario kept the last observation or a stale cell");

    auto words=fixture(SimulationViewMode::walkthrough);
    words.text_values=true;words.scenarios.clear();
    words.values={std::string(""),std::string(" "),std::string(" ")};
    SimulationView text_view(host,words,"es");resize(text_view,820);
    check(text_view.go_to_step(1)&&
        name(GetDlgItem(text_view.window(),array_id)).find(
            "Casilla contada ahora: índice 0 = \"\" (cadena vacía).")!=std::string::npos,
        "An empty text observation lost its unambiguous accessible value");
    check(text_view.go_to_step(3)&&
        name(GetDlgItem(text_view.window(),array_id)).find(
            "Casilla contada ahora: índice 1 = \" \" (1 espacio).")!=std::string::npos,
        "A space-only observation was confused with an empty string");

    for(const auto mode:{SimulationViewMode::array,SimulationViewMode::summary,
                         SimulationViewMode::groups,SimulationViewMode::candidates}){
        auto other=fixture(mode);
        SimulationView other_view(host,other,"es");resize(other_view,820);
        check(name(GetDlgItem(other_view.window(),array_id)).find(
            "Casilla contada ahora:")==std::string::npos,
            "A non-walkthrough view fabricated a newly read cell");
    }
    check(spec==authored,"Highlighting a counted cell modified authored curriculum");
}
void walkthrough_cell_selection(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    const auto expected=build_simulation_trace(spec);
    int changes=0,layouts=0;
    SimulationCallbacks gestures{};
    gestures.changed=[&]{++changes;};
    gestures.layout_changed=[&]{++layouts;};
    gestures.passive_gesture=[](HWND,UINT message,WPARAM,LPARAM)->std::optional<LRESULT>{
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)return LRESULT{0};
        return std::nullopt;
    };
    SimulationView view(host,spec,"es",gestures);
    resize(view,1050);
    check(view.step_index()==0&&view.cell_bounds_in_canvas(5).has_value()&&
        !view.cell_bounds_in_canvas(6).has_value(),
        "Walkthrough did not expose exact rectangles for its current data");
    const auto initial_key=view.reading_regions()[2].key;
    click_array_cell(view,3);
    check(view.step_index()==5&&expected.frames[5].phase==SimulationPhase::advance&&
        text(view,explanation_id)==expected.frames[5].explanation&&
        text(view,variables_id).find("inicio: 1\nfin (exclusivo): 4\nfrecuencia: 3")!=std::string::npos&&
        text(view,output_id)=="-8:1\n",
        "Index 3 must show its third observation, not a completed future decision");
    check(SendMessageW(GetDlgItem(view.window(),step_selector_id),CB_GETCURSEL,0,0)==5,
        "Click selection and keyboard step selector disagree");
    click_array_cell(view,5);
    check(view.step_index()==8&&expected.frames[8].phase==SimulationPhase::start&&
        text(view,output_id)=="-8:1\n3:4\n",
        "Last cell did not show its own start frame and prior output");
    click_array_cell(view,0,true);click_array_cell(view,0,false,true);
    check(view.step_index()==8,"Drag or Ctrl+click navigated the walkthrough");
    click_array_cell(view,0);
    check(view.step_index()==1&&expected.frames[1].phase==SimulationPhase::start&&
        text(view,output_id).empty(),"First cell retained future output");
    check(view.go_to_step(0)&&view.reading_regions()[2].key==initial_key,
        "Returning to initial frame did not restore its reading identity");
    view.set_text_scale(150);resize(view,360);bounds(view,"wrapped walkthrough selection");
    click_array_cell(view,4);
    check(view.step_index()==6&&text(view,explanation_id)==expected.frames[6].explanation&&
        text(view,variables_id).find("frecuencia: 4")!=std::string::npos&&
        text(view,output_id)=="-8:1\n",
        "Zoomed multirow cell selected another element or revealed future output");
    check(changes>=4&&changes==layouts&&spec==authored,
        "Cell exploration failed to update narration or mutated the authored model");
    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(scenario));pump();
    check(view.step_index()==0&&view.cell_bounds_in_canvas(3).has_value()&&
        !view.cell_bounds_in_canvas(4).has_value(),
        "A shorter scenario retained stale cell geometry");
    click_array_cell(view,3);
    check(view.step_index()==5&&text(view,output_id)=="4:2\n"&&
        text(view,variables_id).find("frecuencia: 2")!=std::string::npos,
        "New scenario clicks used stale frames from the previous input");
    SendMessageW(scenario,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(scenario));pump();
    check(view.step_count()==1&&!view.cell_bounds_in_canvas(0).has_value(),
        "The empty array kept a clickable stale cell");
    const auto before=view.reading_regions();
    const auto canvas=GetDlgItem(view.window(),array_id);
    SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));
    SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(10,10));pump();
    const auto after=view.reading_regions();
    check(view.step_index()==0&&after.size()==before.size()&&
        std::equal(before.begin(),before.end(),after.begin(),[](const auto& a,const auto& b){
            return a.key==b.key&&a.source_text==b.source_text;}),
        "An empty-array click fabricated a step or changed reading state");
}
void original_order_comparison(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    const auto authored=spec;
    SimulationView view(host,spec,"es");resize(view,820);
    const auto preview=GetDlgItem(view.window(),original_order_id);
    check(preview&&control_shown(preview),"Sorting did not expose the source input");
    const auto initial=text(view,original_order_id);
    check(initial.find("Entrada aplicada antes de ordenar")!=std::string::npos&&
        initial.find("índice 0: 10")!=std::string::npos&&
        initial.find("índice 1: 3")!=std::string::npos&&
        initial.find("índice 2: -8")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 0: -8 (origen índice 2)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 1: 3 (origen índice 1)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 2: 3 (origen índice 3)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 3: 3 (origen índice 4)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 4: 3 (origen índice 5)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 5: 10 (origen índice 0)")!=std::string::npos,
        "Stable sorting failed to preserve source positions for equal values");
    const auto reading_key=[&](const SimulationView& current) {
        for(const auto& region:current.reading_regions())
            if(region.key.find("/original-order")!=std::string::npos)return region.key;
        return std::string{};
    };
    const auto initial_key=reading_key(view);
    check(!initial_key.empty(),"The source comparison is absent from current narration");
    check(view.go_to_step(view.step_count()-1)&&reading_key(view)==initial_key&&
        text(view,original_order_id)==initial,
        "Stepping changed the applied source or reidentified its narration");
    resize(view,360);bounds(view,"narrow source order");
    set_edit_text(GetDlgItem(view.window(),input_id),"1\n2\n3");
    click(view,apply_id);resize(view,360);
    check(!control_shown(preview)&&text(view,original_order_id).empty()&&
        reading_key(view).empty()&&
        name(GetDlgItem(view.window(),array_id)).find("índice 0: 1")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("origen índice")==std::string::npos,
        "Already sorted input retained a redundant source comparison or mapping");
    set_edit_text(GetDlgItem(view.window(),input_id),"3\n1\n3");
    click(view,apply_id);resize(view,820);
    const auto edited=text(view,original_order_id);
    check(control_shown(preview)&&edited.find("índice 0: 3")!=std::string::npos&&
        edited.find("índice 1: 1")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 0: 1 (origen índice 1)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 1: 3 (origen índice 0)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 2: 3 (origen índice 2)")!=std::string::npos&&
        reading_key(view)!=initial_key,
        "Edited source positions or comparison did not update with applied input");
    click(view,restore_id);resize(view,820);
    check(text(view,original_order_id)==initial&&reading_key(view)==initial_key&&spec==authored&&
        name(GetDlgItem(view.window(),array_id)).find("índice 0: -8 (origen índice 2)")!=std::string::npos,
        "Restore did not recover the original comparison or stable source positions");
    const auto scenario=GetDlgItem(view.window(),scenario_id);
    SendMessageW(scenario,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(text(view,original_order_id).find("índice 0: 9")!=std::string::npos&&
        text(view,original_order_id).find("índice 1: 4")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 0: 4 (origen índice 1)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 1: 4 (origen índice 3)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 2: 9 (origen índice 0)")!=std::string::npos&&
        name(GetDlgItem(view.window(),array_id)).find("índice 3: 9 (origen índice 2)")!=std::string::npos,
        "The scenario selector retained a previous stable-sort mapping");
    SendMessageW(scenario,CB_SETCURSEL,2,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(scenario));pump();
    check(!control_shown(preview)&&reading_key(view).empty()&&
        view.step_count()==1&&
        name(GetDlgItem(view.window(),array_id)).find("origen índice")==std::string::npos,
        "Empty data retained an obsolete source comparison or cell mapping");

    auto strings=fixture(SimulationViewMode::summary);
    strings.text_values=true;strings.editable=false;strings.scenarios.clear();
    strings.values={std::string(" "),std::string(""),std::string("patio")};
    SimulationView text_view(host,strings,"es");resize(text_view,650);
    check(control_shown(GetDlgItem(text_view.window(),original_order_id))&&
        text(text_view,original_order_id).find("índice 0: \" \" (1 espacio)")!=std::string::npos&&
        text(text_view,original_order_id).find("índice 1: \"\" (cadena vacía)")!=std::string::npos&&
        name(GetDlgItem(text_view.window(),array_id)).find("índice 0: \"\" (cadena vacía) (origen índice 1)")!=std::string::npos&&
        name(GetDlgItem(text_view.window(),array_id)).find("índice 1: \" \" (1 espacio) (origen índice 0)")!=std::string::npos,
        "Text sorting lost original positions for empty or whitespace-only values");

    auto no_sort=fixture(SimulationViewMode::array);no_sort.sort=false;
    no_sort.values=numbers({-8,3,3,10});no_sort.scenarios.clear();
    SimulationView unchanged(host,no_sort,"es");resize(unchanged,650);
    check(!control_shown(GetDlgItem(unchanged.window(),original_order_id))&&
        text(unchanged,original_order_id).empty()&&
        name(GetDlgItem(unchanged.window(),array_id)).find("origen índice")==std::string::npos,
        "Unsorted mode fabricated an ordering comparison or source mapping");

    auto many=fixture(SimulationViewMode::summary);many.scenarios.clear();
    many.values.clear();for(std::int64_t value=63;value>=0;--value)many.values.emplace_back(value);
    SimulationView long_view(host,many,"es");resize(long_view,360);bounds(long_view,"long original comparison");
    const auto long_preview=GetDlgItem(long_view.window(),original_order_id);
    check(control_shown(long_preview)&&
        (GetWindowLongPtrW(long_preview,GWL_STYLE)&WS_TABSTOP)!=0&&
        text(long_view,original_order_id).find("índice 63: 0")!=std::string::npos&&
        name(GetDlgItem(long_view.window(),array_id)).find("índice 0: 0 (origen índice 63)")!=std::string::npos&&
        name(GetDlgItem(long_view.window(),array_id)).find("índice 63: 63 (origen índice 0)")!=std::string::npos,
        "Long source comparison or its per-cell source mapping is incomplete");
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
            click_array_cell(view,5);
            check(view.step_index()==2&&SendMessageW(GetDlgItem(view.window(),group_first_id+2),BM_GETCHECK,0,0)==BST_CHECKED&&
                text(view,variables_id).find("frecuencia: 1")!=std::string::npos,
                "Clicking a rendered cell did not select its group after the parent consumed mouse events");
            click_array_cell(view,0,true);
            check(view.step_index()==2,"Dragging from a cell was incorrectly treated as a click");
            click_array_cell(view,0,false,true);
            check(view.step_index()==2,"Ctrl+click on an array cell changed the selected group");
            click_array_cell(view,0);
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
            click_array_cell(view,2);
            check(view.step_index()==1&&text(view,output_id)=="-8:1\n3:4\n"&&
                SendMessageW(navigation_selector,CB_GETCURSEL,0,0)==1,
                "Clicking a member of the middle group did not select its completed candidate frame");
        }else{
            click_array_cell(view,0);
            check(view.step_index()==0,"Static array or summary reacted to a cell click");
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
void editable_input_keyboard_focus(HWND host) {
    // Use the real dialog message pump: a multiline RichEdit must not trap
    // Tab, insert whitespace, or replace a student's pending data.
    for(const bool texts:{false,true}) {
        auto spec=fixture(SimulationViewMode::walkthrough);
        spec.text_values=texts;
        if(texts)spec.values={std::string(""),std::string("patio"),std::string("patio")};
        spec.scenarios.clear();
        SimulationView view(host,spec,"es");
        resize(view,820);
        const auto editor=GetDlgItem(view.window(),input_id);
        const auto apply=GetDlgItem(view.window(),apply_id);
        check(editor&&apply&&control_shown(editor)&&control_shown(apply)&&
            IsWindowEnabled(editor)&&IsWindowEnabled(apply),
            "The editable simulation lacks a tab-navigable input or apply button");
        const auto before=edit_text(editor);
        const auto step=view.step_index();
        SetFocus(editor);
        check(GetFocus()==editor,"Cannot focus the native simulation input");
        key(host,VK_TAB);
        check(GetFocus()==apply&&edit_text(editor)==before&&view.step_index()==step,
            "Tab inserted text or failed to reach Apply from the multiline editor");
        key(host,VK_TAB,false,true);
        check(GetFocus()==editor&&edit_text(editor)==before&&view.step_index()==step,
            "Shift+Tab did not return to the editor without changing its data");
        SetFocus(nullptr);
    }
}
void scale_limits_and_local_control_wheel(HWND host) {
    auto spec=fixture(SimulationViewMode::summary);SimulationView first(host,spec,"es"),second(host,spec,"es");
    resize(first,800);resize(second,800);
    const auto first_contents=rich_edit_contents(first),second_contents=rich_edit_contents(second);
    first.set_text_scale(900);resize(first,800);bounds(first,"maximum simulation zoom");
    (void)rich_edit_fonts(first,150);(void)rich_edit_fonts(second,100);
    first.set_text_scale(100);resize(first,800);
    // Exact native zoom factors must not depend on an accidental 150/100
    // fraction: the RichEdit API bounds both numerator and denominator to 64.
    (void)rich_edit_fonts(first,100);
    first.set_text_scale(125);resize(first,800);
    (void)rich_edit_fonts(first,125);
    // 101/100 cannot be sent directly to RichEdit: each fraction component
    // is bounded to 64. The nearest legal ratio must still be accepted.
    first.set_text_scale(101);resize(first,800);
    for(auto child=GetWindow(first.window(),GW_CHILD);child;
            child=GetWindow(child,GW_HWNDNEXT)){
        wchar_t kind[48]{};
        GetClassNameW(child,kind,48);
        if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)||
            std::wstring_view(kind)!=MSFTEDIT_CLASS)continue;
        int numerator=0,denominator=0;
        check(SendMessageW(child,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),
            reinterpret_cast<LPARAM>(&denominator))!=FALSE&&
            numerator>=1&&numerator<=64&&denominator>=1&&denominator<=64,
            "A non-round reader scale produced an invalid RichEdit fraction");
        check(std::abs(MulDiv(numerator,100,denominator)-101)<=1,
            "The closest native zoom ratio does not represent a 101 percent reader scale");
    }
    first.set_text_scale(100);resize(first,800);
    const auto input=GetDlgItem(first.window(),input_id);
    // Fail the last RichEdit after earlier controls and the editable input
    // accepted their next zoom. Restore the input's independent 120% zoom,
    // not merely the global reader scale of 100%.
    control_wheel(host,input,WHEEL_DELTA);
    control_wheel(host,input,WHEEL_DELTA);
    check(rich_edit_scale(input)==120,
        "Cannot prepare the editor's independent scale before rollback");
    // Seed a real native Redo history without changing the final input.
    // A font/zoom transaction must never clear this history, selection
    // or caret focus when a later RichEdit rejects its scale.
    const auto draft_before=edit_text(input);
    SendMessageW(input,EM_SETSEL,0,0);
    SendMessageW(input,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"x"));
    check(SendMessageW(input,EM_UNDO,0,0)!=FALSE&&
        SendMessageW(input,EM_CANREDO,0,0)!=FALSE&&
        edit_text(input)==draft_before,
        "Cannot prepare real native undo/redo evidence for the zoom transaction");
    CHARRANGE selected{1,3},selection_after{};
    SendMessageW(input,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    SendMessageW(input,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection_after));
    const auto undo_before=SendMessageW(input,EM_CANUNDO,0,0);
    const auto redo_before=SendMessageW(input,EM_CANREDO,0,0);
    const auto modified_before=SendMessageW(input,EM_GETMODIFY,0,0);
    POINT scroll_before{};
    SendMessageW(input,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll_before));
    const auto focus_before=GetFocus();
    const auto rejected_panel=GetDlgItem(first.window(),progress_label_id);
    check(rejected_panel,"Missing late RichEdit for a transactional scale test");
    const auto fail_zoom=[](HWND child,UINT message,WPARAM wp,LPARAM lp,
            UINT_PTR,DWORD_PTR)->LRESULT{
        if(message==EM_SETZOOM&&wp==5&&lp==4)return FALSE;
        return DefSubclassProc(child,message,wp,lp);
    };
    check(SetWindowSubclass(rejected_panel,fail_zoom,75,0)!=FALSE,
        "Cannot intercept a late native simulation zoom failure");
    bool rejected_reader_scale=false;
    try {first.set_text_scale(125);}
    catch(const std::runtime_error&){rejected_reader_scale=true;}
    RemoveWindowSubclass(rejected_panel,fail_zoom,75);
    check(rejected_reader_scale&&rich_edit_scale(input)==120,
        "Rejected reader zoom was committed or erased the editor-local scale");
    CHARRANGE selection_restored{};
    POINT scroll_restored{};
    SendMessageW(input,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection_restored));
    SendMessageW(input,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll_restored));
    check(GetFocus()==focus_before&&
        selection_restored.cpMin==selection_after.cpMin&&
        selection_restored.cpMax==selection_after.cpMax&&
        undo_before==SendMessageW(input,EM_CANUNDO,0,0)&&
        redo_before==SendMessageW(input,EM_CANREDO,0,0)&&
        modified_before==SendMessageW(input,EM_GETMODIFY,0,0)&&
        scroll_before.x==scroll_restored.x&&scroll_before.y==scroll_restored.y,
        "Rejected reader zoom changed editor caret, selection, Undo/Redo, modification or scroll");
    for(auto child=GetWindow(first.window(),GW_CHILD);child;
            child=GetWindow(child,GW_HWNDNEXT)){
        wchar_t kind[48]{};GetClassNameW(child,kind,48);
        if(std::wstring_view(kind)!=MSFTEDIT_CLASS||child==input)continue;
        check(rich_edit_scale(child)==100,
            "A rejected reader zoom left one RichEdit at a mixed scale");
    }
    check(rich_edit_contents(first)==first_contents&&
        rich_edit_contents(second)==second_contents,
        "Rolling back native zoom modified simulation text or another view");
    control_wheel(host,input,-WHEEL_DELTA);
    control_wheel(host,input,-WHEEL_DELTA);
    check(rich_edit_scale(input)==100,
        "The editor could not resume ordinary Ctrl+wheel after a rejected zoom");
    first.set_text_scale(125);resize(first,800);
    (void)rich_edit_fonts(first,125);
    first.set_text_scale(100);resize(first,800);
    const auto original_step=first.step_index();const auto original_array=name(GetDlgItem(first.window(),array_id));
    for(int step=0;step<40&&rich_edit_scale(input)<150;++step){
        const auto before=rich_edit_scale(input);control_wheel(host,input,WHEEL_DELTA);const auto after=rich_edit_scale(input);
        check(after>before&&after<=150,"Ctrl+wheel did not enlarge the local data editor within its 150 percent limit");
    }
    check(rich_edit_scale(input)==150,"Ctrl+wheel did not reach the native 150 percent limit");
    control_wheel(host,input,WHEEL_DELTA);check(rich_edit_scale(input)==150,"An extra Ctrl+wheel event exceeded 150 percent");
    control_wheel(host,input,-WHEEL_DELTA);check(rich_edit_scale(input)==140,
        "Ctrl+wheel did not reduce the local data editor's scale by one step");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==140,
        "Half a high-resolution wheel notch prematurely changed the editor zoom");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==150,
        "Two high-resolution half-notches did not produce one zoom step");
    control_wheel(host,input,-WHEEL_DELTA);
    check(rich_edit_scale(input)==140,
        "Ctrl+wheel failed to resume below the maximum after reaching it");
    control_wheel(host,input,WHEEL_DELTA/2);
    SendMessageW(input,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==140,
        "Ordinary scrolling improperly changed zoom or carried a stale Ctrl+wheel fraction");
    SendMessageW(input,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);
    for(int step=0;step<20;++step)control_wheel(host,input,-WHEEL_DELTA);
    check(rich_edit_scale(input)==50,
        "Ctrl+wheel passed below the simulation data editor's minimum scale");
    control_wheel(host,input,-WHEEL_DELTA);
    check(rich_edit_scale(input)==50,
        "Ctrl+wheel did not stop at the 50 percent minimum");
    const auto short_explanation=GetDlgItem(first.window(),explanation_id);
    const auto short_output=GetDlgItem(first.window(),output_id);
    control_wheel(host,short_explanation,WHEEL_DELTA);
    control_wheel(host,short_output,-WHEEL_DELTA);
    check(rich_edit_scale(short_explanation)==100&&rich_edit_scale(short_output)==100,
        "A read-only simulation text control changed its own zoom after Ctrl+wheel");
    (void)rich_edit_fonts(second,100);
    check(rich_edit_contents(first)==first_contents&&rich_edit_contents(second)==second_contents&&
        first.step_index()==original_step&&name(GetDlgItem(first.window(),array_id))==original_array,
        "Ctrl+wheel changed simulation data, replay position or another view");
}
void input_wheel_session_boundaries(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    SimulationView view(host,spec,"es");
    resize(view,820);
    const auto input=GetDlgItem(view.window(),input_id);
    const auto apply=GetDlgItem(view.window(),apply_id);
    const auto selector=GetDlgItem(view.window(),scenario_id);
    check(input&&apply&&selector,"Missing editor controls for fractional wheel checks");
    const auto initial_text=edit_text(input);
    const auto initial_step=view.step_index();
    const auto initial_array=name(GetDlgItem(view.window(),array_id));
    SetFocus(input);
    check(GetFocus()==input,"Cannot focus the data editor before wheel test");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==100,"Half a wheel notch changed the initial zoom");
    SetFocus(apply);
    check(GetFocus()==apply,"Could not leave the data editor with a partial wheel delta");
    SetFocus(input);
    check(GetFocus()==input,"Could not restore focus to the data editor");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==100,"A partial delta survived a real focus change");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==110,"A fresh pair of high-resolution deltas was discarded");

    // Applying a new array must not carry half of an old gesture to its new editor.
    control_wheel(host,input,WHEEL_DELTA/2);
    set_edit_text(input,"4\n4\n2");
    click(view,apply_id);
    check(view.step_index()==0&&text(view,error_id).empty()&&
        rich_edit_scale(input)==110,"Applying valid data lost the editor zoom or model reset");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==110,"Applying new data retained an earlier half-notch");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==120,"The updated editor stopped accepting complete wheel notches");

    control_wheel(host,input,WHEEL_DELTA/2);
    SendMessageW(selector,CB_SETCURSEL,1,0);
    SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(selector));pump();
    check(view.step_index()==0&&rich_edit_scale(input)==120&&
        edit_text(input)=="9\n4\n9\n4",
        "Selecting another scenario did not preserve zoom and replace the edit data");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==120,"Scenario selection reused a previous wheel fraction");
    control_wheel(host,input,WHEEL_DELTA/2);
    check(rich_edit_scale(input)==130,"A new scenario cannot accumulate a fresh wheel notch");
    check(initial_text=="10\n3\n-8\n3\n3\n3"&&initial_step==0&&
        initial_array.find("índice")!=std::string::npos,
        "A wheel gesture altered the initial simulation fixture");
    SetFocus(nullptr);
}
void reader_scale_wheel_session_boundaries(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    SimulationView view(host,spec,"es");
    SimulationView untouched(host,spec,"es");
    resize(view,820);resize(untouched,820);
    const auto editor=GetDlgItem(view.window(),input_id);
    const auto explanation=GetDlgItem(view.window(),explanation_id);
    const auto draft=edit_text(editor);
    const auto position=view.step_index();
    const auto other_contents=rich_edit_contents(untouched);
    SetFocus(editor);
    check(GetFocus()==editor,"Cannot focus the editor for reader-scale wheel transition");

    // Half a high-resolution wheel turn at 100% must not be inherited by
    // a new global reader scale. Only two NEW halves form one new notch.
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==100,"First partial wheel delta already changed the editor");
    view.set_text_scale(125);resize(view,820);
    check(rich_edit_scale(editor)==125&&rich_edit_scale(explanation)==125,
        "Applying the reader scale did not update both native text controls");
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==125,
        "Reader zoom reused an earlier partial Ctrl+wheel delta");
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==135,
        "New reader scale cannot accumulate a fresh high-resolution wheel notch");

    // A second change of global scale must also discard the half-notch
    // left by the preceding editor session.
    control_wheel(host,editor,WHEEL_DELTA/2);
    view.set_text_scale(100);resize(view,820);
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==100&&rich_edit_scale(explanation)==100,
        "Returning to the base reader scale inherited an obsolete wheel fraction");
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==110,
        "Returning to the base reader scale disabled new wheel accumulation");
    check(view.step_index()==position&&edit_text(editor)==draft&&
        rich_edit_contents(untouched)==other_contents&&
        rich_edit_scale(GetDlgItem(untouched.window(),input_id))==100,
        "Reader-scale transitions changed the active model, draft or another simulation");
    SetFocus(nullptr);
}
void dpi_rebase_preserves_local_editor_zoom(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    SimulationView view(host,spec,"es"),other(host,spec,"es");
    resize(view,820);resize(other,820);
    const auto editor=GetDlgItem(view.window(),input_id);
    const auto explanation=GetDlgItem(view.window(),explanation_id);
    check(editor&&explanation,"DPI rebase fixture lacks the editor or reader text");
    const auto original=rich_edit_contents(view),other_original=rich_edit_contents(other);
    const auto step=view.step_index();
    SetFocus(editor);
    check(GetFocus()==editor,"DPI rebase fixture cannot focus its data editor");
    control_wheel(host,editor,2*WHEEL_DELTA);
    check(rich_edit_scale(editor)==120&&rich_edit_scale(explanation)==100,
        "Preparing a local editor scale changed the surrounding reader");
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==120,
        "A partial local wheel delta changed zoom before the DPI rebase");

    // Send only to the isolated child, as Windows does after its parent DPI
    // transition. No physical monitor or desktop window is modified by QA.
    SendMessageW(view.window(),WM_DPICHANGED_AFTERPARENT,0,0);
    pump(host);
    check(rich_edit_scale(editor)==120&&rich_edit_scale(explanation)==100,
        "A DPI font rebase erased independent editor zoom or changed reader scale");
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==120,
        "DPI rebase reused half a wheel notch from the former DPI session");
    control_wheel(host,editor,WHEEL_DELTA/2);
    check(rich_edit_scale(editor)==130,
        "DPI rebase disabled the new high-resolution wheel session");
    check(view.step_index()==step&&rich_edit_contents(view)==original&&
        rich_edit_contents(other)==other_original&&
        rich_edit_scale(GetDlgItem(other.window(),input_id))==100,
        "DPI rebase changed the simulation model, draft, narration or another view");

    // An explicit reader scale change is different: it deliberately sets
    // both controls to the requested scale and starts a fresh wheel session.
    view.set_text_scale(150);resize(view,820);
    check(rich_edit_scale(editor)==150&&rich_edit_scale(explanation)==150&&
        rich_edit_contents(view)==original&&view.step_index()==step,
        "An explicit reader-scale change incorrectly retained local DPI zoom");
    SetFocus(nullptr);
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
struct WheelEdgeProbe {int parent_messages=0;};
LRESULT CALLBACK wheel_edge_probe(HWND window,UINT message,WPARAM wp,LPARAM lp,
        UINT_PTR,DWORD_PTR context) {
    if(message==WM_MOUSEWHEEL) {
        ++reinterpret_cast<WheelEdgeProbe*>(context)->parent_messages;
        return 0;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,wheel_edge_probe,31);
    return DefSubclassProc(window,message,wp,lp);
}
void wheel_edges_resume_reader(HWND host) {
    // The parent is a hidden QA window, not the user's real study viewport.
    // Count only real Win32 wheel messages forwarded by the child.
    auto spec=fixture(SimulationViewMode::summary);
    spec.values.clear();
    for(std::int64_t value=0;value<64;++value)spec.values.emplace_back(value);
    SimulationView view(host,spec,"es");
    resize(view,720);
    const auto output=GetDlgItem(view.window(),output_id);
    const auto chart=GetDlgItem(view.window(),frequencies_id);
    check(output&&chart,"An overflow child of the simulation is missing");
    WheelEdgeProbe probe;
    check(SetWindowSubclass(view.window(),wheel_edge_probe,31,
        reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,
        "Cannot observe the simulated reader wheel messages");
    struct Unhook {
        HWND hwnd;
        ~Unhook() {if(IsWindow(hwnd))RemoveWindowSubclass(hwnd,wheel_edge_probe,31);}
    } unhook{view.window()};
    const auto scroll=[&](HWND item){
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};
        check(GetScrollInfo(item,SB_VERT,&info)!=FALSE,
            "Cannot inspect a native simulation scrollbar");
        return info;
    };
    const auto wheel=[&](HWND item,int delta){
        SendMessageW(item,WM_MOUSEWHEEL,
            MAKEWPARAM(0,static_cast<WORD>(delta)),0);
    };
    for(const auto item:{output,chart}) {
        auto info=scroll(item);
        check(info.nPage>0&&
            info.nMax-info.nMin+1>static_cast<int>(info.nPage),
            "The long read-only output or histogram has no actual overflow");
        SendMessageW(item,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
        info=scroll(item);
        check(info.nPos==info.nMin,
            "Could not position an overflow child at the beginning");
        probe.parent_messages=0;
        wheel(item,WHEEL_DELTA);
        check(probe.parent_messages==1,
            "Scrolling upward at the first line trapped the lesson in its child");
        wheel(item,-WHEEL_DELTA);
        const auto moved=scroll(item);
        check(moved.nPos>info.nPos,
            "Scrolling toward hidden content failed to move the child scrollbar");
        SendMessageW(item,WM_VSCROLL,MAKEWPARAM(SB_BOTTOM,0),0);
        const auto bottom=scroll(item);
        const int last=bottom.nMax-
            std::max(0,static_cast<int>(bottom.nPage)-1);
        check(bottom.nPos==last,
            "Could not position an overflow child at its final scroll limit");
        probe.parent_messages=0;
        wheel(item,-WHEEL_DELTA);
        check(probe.parent_messages==1,
            "Scrolling downward at the last line trapped the lesson in its child");
        check(scroll(item).nPos==last,
            "A wheel movement past the edge corrupted the scrollbar position");
    }
}

void accumulated_output_scroll_follows_new_groups(HWND host) {
    auto spec=fixture(SimulationViewMode::walkthrough);
    spec.scenarios.clear();
    spec.values.clear();
    for(std::int64_t value=0;value<64;++value)spec.values.emplace_back(value);
    SimulationView view(host,spec,"es");
    resize(view,800);
    const auto output=GetDlgItem(view.window(),output_id);
    check(output,"The accumulated output RichEdit is missing");
    int stream_rewrites=0;
    const auto stream_probe=[](HWND edit,UINT message,WPARAM wp,LPARAM lp,
            UINT_PTR,DWORD_PTR raw)->LRESULT {
        if(message==EM_STREAMIN)++*reinterpret_cast<int*>(raw);
        return DefSubclassProc(edit,message,wp,lp);
    };
    check(SetWindowSubclass(output,stream_probe,39,
        reinterpret_cast<DWORD_PTR>(&stream_rewrites))!=FALSE,
        "Cannot observe the real output document streaming");
    struct StreamProbeGuard {
        HWND window;SUBCLASSPROC proc;
        ~StreamProbeGuard(){if(IsWindow(window))RemoveWindowSubclass(window,proc,39);}
    } probe_guard{output,stream_probe};

    // Each distinct value contributes a start frame and a decision. At
    // decision 20 there are already 21 lines, overflowing the bounded
    // output panel. The next start frame does not change the output.
    check(view.go_to_step(42),"Cannot reach the long calculated output");
    const auto long_state=scroll_state(output);
    check(long_state.has_range&&long_state.vertical.nPage>0&&
        long_state.vertical.nMax-long_state.vertical.nMin+1>
            static_cast<int>(long_state.vertical.nPage),
        "The accumulated output fixture has no native vertical overflow");
    check(text(view,output_id).ends_with("20:1\n"),
        "The output fixture did not finish its twentieth group");
    // Ignore initial construction of the long viewport; count only the
    // subsequent start/decision transitions being verified.
    stream_rewrites=0;

    SendMessageW(output,WM_VSCROLL,MAKEWPARAM(SB_BOTTOM,0),0);
    const auto bottom=scroll_state(output);
    check(bottom.position.y>0&&
        bottom.vertical.nPos>=bottom.vertical.nMax-
            std::max(0,static_cast<int>(bottom.vertical.nPage)-1),
        "Could not reach the last visible line of the output");
    const auto unchanged=text(view,output_id);
    check(view.go_to_step(43)&&text(view,output_id)==unchanged,
        "Counting the next element changed output before deciding its group");
    check(stream_rewrites==0,
        "Counting without a new completed group unnecessarily streamed stdout");
    const auto still_bottom=scroll_state(output);
    check(still_bottom.position.y==bottom.position.y,
        "An unchanged output was rewritten or lost its existing scroll");

    // When the learner is already reading the end, the next completed
    // group should be brought into view without changing the raw stdout.
    check(view.go_to_step(44)&&text(view,output_id).ends_with("21:1\n"),
        "The next completed group did not append its expected result");
    check(stream_rewrites==1,
        "Appending a completed group did not stream stdout exactly once");
    const auto extended=scroll_state(output);
    check(extended.position.y>=bottom.position.y&&
        extended.vertical.nPos>=extended.vertical.nMax-
            std::max(0,static_cast<int>(extended.vertical.nPage)-1),
        "A new output line remained hidden despite a bottom-pinned viewport");

    // If the learner scrolls back to an older result, a new group must
    // not yank their focus to the bottom again.
    SendMessageW(output,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
    const auto top=scroll_state(output);
    check(top.first==0&&top.position.y==0,
        "Could not return to the beginning of the output");
    check(view.go_to_step(45)&&view.go_to_step(46)&&
        text(view,output_id).ends_with("22:1\n"),
        "The next completed output group was lost");
    check(stream_rewrites==2,
        "A count/decision pair streamed more output than its one new result");
    const auto retained=scroll_state(output);
    check(retained.first==0&&retained.position.y==top.position.y,
        "Appending output stole the learner's manually selected earlier lines");
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
    // Long read-only text has genuine native overflow. Ctrl+wheel must go
    // through the parent reader rather than zoom its RichEdit in isolation.
    control_wheel(host,explanation,WHEEL_DELTA);
    control_wheel(host,output,-WHEEL_DELTA);
    check(rich_edit_scale(explanation)==100&&rich_edit_scale(output)==100&&
        rich_edit_contents(view)==original_contents,
        "Ctrl+wheel changed independently scaled, scrollable read-only text");
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
    run("native_text_control_contract",[&]{native_text_control_contract(host.value);});
    run("current_frequency_decision",[&]{current_frequency_decision(host.value,captures);});
    run("histogram_card_navigation",[&]{histogram_card_navigation(host.value);});
    run("progressive_frequency_bars",[&]{progressive_frequency_bars(host.value,captures);});
    run("candidate_frequency_filter",[&]{candidate_frequency_filter(host.value);});
    run("neighboring_values_explain_group_boundaries",[&]{neighboring_values_explain_group_boundaries(host.value);});
    run("calculated_step_changes",[&]{calculated_step_changes(host.value);});
    run("completed_scenario_comparison",[&]{completed_scenario_comparison(host.value);});
    run("decision_navigation",[&]{decision_navigation(host.value);});
    run("navigation",[&]{navigation(host.value,captures);});
    run("walkthrough_processed_progress",[&]{walkthrough_processed_progress(host.value,captures);});
    run("counted_cell_emphasis",[&]{counted_cell_emphasis(host.value,captures);});
    run("walkthrough_cell_selection",[&]{walkthrough_cell_selection(host.value);});
    run("original_order_comparison",[&]{original_order_comparison(host.value);});
    run("editable_data",[&]{editable_data(host.value,captures);});
    run("modes",[&]{modes(host.value,captures);});
    run("text_and_independent_views",[&]{text_and_independent_views(host.value,captures);});
    run("empty_text_values",[&]{empty_text_values(host.value,captures);});
    run("editable_input_keyboard_focus",[&]{editable_input_keyboard_focus(host.value);});
    run("scale_limits_and_local_control_wheel",[&]{scale_limits_and_local_control_wheel(host.value);});
    run("input_wheel_session_boundaries",[&]{input_wheel_session_boundaries(host.value);});
    run("reader_scale_wheel_session_boundaries",[&]{reader_scale_wheel_session_boundaries(host.value);});
    run("dpi_rebase_preserves_local_editor_zoom",[&]{dpi_rebase_preserves_local_editor_zoom(host.value);});
    run("parent_width_changes",[&]{parent_width_changes(host.value);});
    run("long_labels_and_literal_values",[&]{long_labels_and_literal_values(host.value,captures);});
    run("wheel_edges_resume_reader",[&]{wheel_edges_resume_reader(host.value);});
    run("accumulated_output_scroll_follows_new_groups",[&]{accumulated_output_scroll_follows_new_groups(host.value);});
    run("scrollable_text_keyboard",[&]{scrollable_text_keyboard(host.value,captures);});
    check(!IsWindowVisible(host.value)&&GetAncestor(GetForegroundWindow(),GA_ROOT)!=host.value,"Simulation QA activated the desktop");
    if(failures){std::cerr<<"FAIL native simulation view: "<<failures<<" independent scenario(s) failed\n";return 1;}
    std::cout<<"PASS native simulation view: actual buttons, keyboard, exact replay, editable/scenario data, groups, candidates, summary, current narration and responsive controls\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native simulation view: "<<error.what()<<'\n';return 1;}
