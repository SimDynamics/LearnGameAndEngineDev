#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/editing_bar.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/narration_preferences.hpp"
#include "native_build_config.hpp"
#include "core/learning_flow.hpp"
#include "core/view_preferences.hpp"
#include "core/attempts.hpp"
#include "presentation/windows/ui_language.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <cstdio>
#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace lgaed::presentation {
namespace {
constexpr wchar_t class_name[]=L"LearnGameAndEngineDev.SettingsView";
constexpr UINT close_message=WM_APP+222;
constexpr UINT refresh_language_message=WM_APP+224;
enum {back=5100,ui_language,content_language,enabled,engine,language,voice,rate,advance,controls,
    auto_lesson,auto_unit,auto_course,swipe_finish,code_wrap,reset,use_global,save_flow,swipe_navigation};
enum {text_scale=5120,text_preview,text_inherit,save_appearance,wrap_overwrite,global_color,programming_color,math_color,
    programming_color_overwrite,math_color_overwrite,global_color_reset,programming_color_reset,math_color_reset,
    reset_wrap_exceptions,reset_programming,reset_math,edit_shortcuts,reset_math_wrap_exceptions};
constexpr int premium_action=5138;
constexpr int audio_programming=5139,audio_mathematics=5140,licenses_action=5141,color_save_notice=5142,size_title=5143,storage_limit=5144,storage_detail=5145;
constexpr int size_separator=5146,narration_hint=5147;
constexpr int storage_maximum=5148;
constexpr int flow_separator_first=5150;
constexpr int size_range=5152;
constexpr int kokoro_separator=5153,kokoro_heading=5154,storage_explanation=5155;
bool separator_id(int id){return id==size_separator||id==kokoro_separator||(id>=flow_separator_first&&id<flow_separator_first+2);}
constexpr std::uint64_t storage_step=1024ull*1024;
constexpr int rgb_slider_first=5700,rgb_input_first=5720,rgb_label_first=5740,rgb_default_first=5760,rgb_default_heading_first=5780;
constexpr int audio_course_first=5600;
std::optional<std::size_t> choose_settings_option(HWND,std::string_view,std::string_view,const std::vector<std::string>&,std::size_t);
constexpr UINT_PTR combo_size_subclass_id=0x4C475348;
constexpr UINT_PTR slider_pointer_subclass_id=0x4C475350;
constexpr UINT_PTR rgb_input_subclass_id=0x4C475352;
LRESULT CALLBACK rgb_input_subclass(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(control,rgb_input_subclass,rgb_input_subclass_id);return DefSubclassProc(control,message,wp,lp);}
    const bool first_click=message==WM_LBUTTONDOWN&&GetFocus()!=control;
    const auto result=DefSubclassProc(control,message,wp,lp);
    // Android's selectAllOnFocus applies to both keyboard focus and the first
    // mouse click. Select after the native click places its caret; later clicks
    // and drags keep ordinary EDIT selection and cursor behavior.
    if((message==WM_SETFOCUS||first_click)&&IsWindow(control))SendMessageW(control,EM_SETSEL,0,-1);
    return result;
}
LRESULT CALLBACK slider_pointer_subclass(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(control,slider_pointer_subclass,slider_pointer_subclass_id);return DefSubclassProc(control,message,wp,lp);}
    if(message==WM_LBUTTONDOWN){
        if(!IsWindowEnabled(control))return 0;
        RECT channel{};SendMessageW(control,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
        const bool vertical=(GetWindowLongPtrW(control,GWL_STYLE)&TBS_VERT)!=0;
        const int start=vertical?channel.top:channel.left,end=vertical?channel.bottom:channel.right;
        const int minimum=static_cast<int>(SendMessageW(control,TBM_GETRANGEMIN,0,0)),maximum=static_cast<int>(SendMessageW(control,TBM_GETRANGEMAX,0,0));
        if(end>start&&maximum>=minimum){
            const int before=static_cast<int>(SendMessageW(control,TBM_GETPOS,0,0)),point=std::clamp(vertical?GET_Y_LPARAM(lp):GET_X_LPARAM(lp),start,end);
            const int selected=minimum+MulDiv(point-start,maximum-minimum,end-start);
            SendMessageW(control,TBM_SETPOS,TRUE,selected);
            // Start the native thumb drag at its new centre. A native channel
            // click would instead move by page size and start a repeat timer.
            RECT thumb{};SendMessageW(control,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
            const auto result=DefSubclassProc(control,message,wp,MAKELPARAM((thumb.left+thumb.right)/2,(thumb.top+thumb.bottom)/2));
            if(IsWindow(control)&&selected!=before)SendMessageW(GetParent(control),vertical?WM_VSCROLL:WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,selected),reinterpret_cast<LPARAM>(control));
            return result;
        }
    }
    return DefSubclassProc(control,message,wp,lp);
}
int measured_text_height(HWND control,int width){
    const auto dc=GetDC(control);if(!dc)return 0;
    const auto font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));const auto previous=font?SelectObject(dc,font):nullptr;
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');text.resize(static_cast<std::size_t>(GetWindowTextW(control,text.data(),static_cast<int>(text.size()))));
    RECT bounds{0,0,std::max(1,width),0};DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    if(previous)SelectObject(dc,previous);ReleaseDC(control,dc);return bounds.bottom-bounds.top;
}
int combo_selection_height(HWND control,int width){const auto dpi=static_cast<int>(GetDpiForWindow(control));
    return std::max(MulDiv(56,dpi,96),measured_text_height(control,width-MulDiv(72,dpi,96))+MulDiv(12,dpi,96));}
void size_combo(HWND control){
    static thread_local bool adjusting=false;if(adjusting)return;adjusting=true;
    const auto dpi=static_cast<int>(GetDpiForWindow(control));
    RECT bounds{};GetClientRect(control,&bounds);
    SendMessageW(control,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),combo_selection_height(control,bounds.right));
    SendMessageW(control,CB_SETITEMHEIGHT,0,MulDiv(36,dpi,96));adjusting=false;
}
LRESULT CALLBACK combo_size_subclass(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(control,combo_size_subclass,combo_size_subclass_id);return DefSubclassProc(control,message,wp,lp);}
    const auto result=DefSubclassProc(control,message,wp,lp);
    if(message==WM_SETFONT||message==WM_SIZE||message==WM_DPICHANGED_AFTERPARENT)size_combo(control);
    return result;
}
void save_accent(state::StateData& data,std::string_view route,COLORREF color){
    auto& prefs=data.preferences["appearance_settings"];const std::string suffix(route);
    prefs[route.empty()?"accent_red":"route_accent_red_"+suffix]=static_cast<std::int64_t>(GetRValue(color));
    prefs[route.empty()?"accent_green":"route_accent_green_"+suffix]=static_cast<std::int64_t>(GetGValue(color));
    prefs[route.empty()?"accent_blue":"route_accent_blue_"+suffix]=static_cast<std::int64_t>(GetBValue(color));
}
void save_default_accent(state::StateData& data,std::string_view route,COLORREF color){
    auto& prefs=data.preferences["appearance_settings"];const auto key=[&](const char* channel){return route.empty()?std::string("default_global_accent_")+channel:std::string("default_route_accent_")+channel+'_'+std::string(route);};
    prefs[key("red")]=static_cast<std::int64_t>(GetRValue(color));prefs[key("green")]=static_cast<std::int64_t>(GetGValue(color));prefs[key("blue")]=static_cast<std::int64_t>(GetBValue(color));
}
int rgb_channel(COLORREF color,int channel){return channel==0?GetRValue(color):channel==1?GetGValue(color):GetBValue(color);}
COLORREF rgb_with_channel(COLORREF color,int channel,int value){return RGB(channel==0?value:GetRValue(color),channel==1?value:GetGValue(color),channel==2?value:GetBValue(color));}
std::optional<int> rgb_number(std::string_view text){if(text.empty()||text.size()>3||!std::ranges::all_of(text,[](char value){return value>='0'&&value<='9';}))return {};int value=0;for(const char digit:text)value=value*10+digit-'0';return value;}
COLORREF default_accent(const state::StateData& data,std::string_view route){
    const state::StateData factory;const auto fallback=reader_theme::appearance_accent(factory,route);
    const auto value=[&](const char* component,int original){const auto key=route.empty()?std::string("default_global_accent_")+component:
        std::string("default_route_accent_")+component+'_'+std::string(route);
        return static_cast<BYTE>(std::clamp<std::int64_t>(state::integer_value(data,"appearance_settings",key,original),0,255));};
    return RGB(value("red",GetRValue(fallback)),value("green",GetGValue(fallback)),value("blue",GetBValue(fallback)));
}
// This is an ordinary Windows dialog with ordinary single-line EDIT controls.
// Shortcut configuration must not run the code editor's automatic pairing.
struct ShortcutDialog {
    static constexpr int viewport_id=5350,defaults_id=5351,help_id=5352,first_input_id=5400;
    HWND window=nullptr,viewport=nullptr;HFONT font=nullptr;int scroll=0;std::string locale;
    std::array<HWND,20> inputs{},labels{};std::vector<std::string> initial;
    std::optional<std::string> result;
    ShortcutDialog(std::vector<std::string> values,std::string language):locale(std::move(language)),initial(std::move(values)){}
    ~ShortcutDialog(){if(font)DeleteObject(font);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    std::string tr(const char* es,const char* en,const char* fr)const{return locale=="fr"?fr:locale=="en"?en:es;}
    HWND control(HWND parent,const wchar_t* cls,std::string_view text,int id,DWORD style){
        const auto child=CreateWindowExW(0,cls,wide(text).c_str(),WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!child)throw std::runtime_error("Cannot create the programming shortcut dialog");
        reader_theme::apply(child,wcscmp(cls,L"EDIT")==0?reader_theme::Surface::field:reader_theme::Surface::screen);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);return child;
    }
    void layout_rows(){
        RECT bounds{};GetClientRect(viewport,&bounds);const int extent=px(static_cast<int>(inputs.size())*76);
        scroll=std::clamp(scroll,0,std::max(0,extent-static_cast<int>(bounds.bottom)));
        for(std::size_t index=0;index<inputs.size();++index){const auto top=px(static_cast<int>(index)*76)-scroll;
            MoveWindow(labels[index],px(6),top,std::max(1L,bounds.right-px(28)),px(24),TRUE);
            MoveWindow(inputs[index],px(6),top+px(26),std::max(1L,bounds.right-px(28)),px(42),TRUE);}
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,extent-1),static_cast<UINT>(bounds.bottom),scroll,0};SetScrollInfo(viewport,SB_VERT,&info,TRUE);reader_theme::refresh_scrollbars(viewport);
        RedrawWindow(viewport,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }
    void layout(){
        if(!viewport)return;RECT bounds{};GetClientRect(window,&bounds);const int margin=px(18),gap=px(12);
        const int width=std::max(px(100),static_cast<int>(bounds.right)-margin*2);
        MoveWindow(GetDlgItem(window,help_id),margin,margin,width,px(78),TRUE);
        MoveWindow(GetDlgItem(window,defaults_id),margin,px(106),width,px(44),TRUE);
        MoveWindow(viewport,margin,px(164),width,std::max(px(60),static_cast<int>(bounds.bottom)-px(250)),TRUE);
        const int button_width=(width-gap)/2;
        MoveWindow(GetDlgItem(window,IDCANCEL),margin,std::max(px(224),static_cast<int>(bounds.bottom)-px(62)),button_width,px(44),TRUE);
        MoveWindow(GetDlgItem(window,IDOK),margin+button_width+gap,std::max(px(224),static_cast<int>(bounds.bottom)-px(62)),button_width,px(44),TRUE);
        layout_rows();
    }
    void reveal(HWND input){
        RECT child{},area{};GetWindowRect(input,&child);MapWindowPoints(nullptr,viewport,reinterpret_cast<POINT*>(&child),2);GetClientRect(viewport,&area);
        if(child.top<0)scroll+=child.top;else if(child.bottom>area.bottom)scroll+=child.bottom-area.bottom;
        layout_rows();
    }
    void restore_defaults(){const auto defaults=parse_state_shortcuts(state::StateData{});
        for(std::size_t index=0;index<inputs.size();++index)SetWindowTextW(inputs[index],wide(index<defaults.size()?defaults[index]:std::string{}).c_str());}
    void save(){
        winrt::Windows::Data::Json::JsonArray shortcuts;
        for(const auto input:inputs){std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(input))+1,L'\0');
            text.resize(static_cast<std::size_t>(GetWindowTextW(input,text.data(),static_cast<int>(text.size()))));
            if(!text.empty())shortcuts.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(text));}
        result=winrt::to_string(shortcuts.Stringify());EndDialog(window,IDOK);
    }
    void initialize(HWND target){
        window=target;SetWindowTextW(window,wide(tr("Atajos de programación","Programming Shortcuts","Raccourcis de programmation")).c_str());
        font=CreateFontW(-MulDiv(16,static_cast<int>(GetDpiForWindow(window)),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(!font)throw std::runtime_error("Cannot create the shortcut dialog font");
        const auto help=tr("TAB permanece fijo. Puedes configurar 20 teclas, compartidas por todos los ejercicios de Programación. Deja una posición vacía para ocultarla.","TAB remains fixed. Configure 20 keys shared by all Programming exercises. Leave a slot empty to hide it.","TAB reste fixe. Configurez 20 touches communes à tous les exercices de Programmation. Laissez une position vide pour la masquer.");
        control(window,L"STATIC",help,help_id,SS_LEFT);
        control(window,L"BUTTON",tr("Restaurar valores predeterminados","Restore defaults","Rétablir les valeurs par défaut"),defaults_id,WS_TABSTOP|BS_PUSHBUTTON|BS_MULTILINE);
        viewport=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(viewport_id)),GetModuleHandleW(nullptr),nullptr);
        if(!viewport||!SetWindowSubclass(viewport,viewport_proc,1,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot create the shortcut dialog scroll area");
        reader_theme::apply(viewport,reader_theme::Surface::screen);
        for(std::size_t index=0;index<inputs.size();++index){labels[index]=control(viewport,L"STATIC",tr("Atajo ","Shortcut ","Raccourci ")+std::to_string(index+1),5500+static_cast<int>(index),SS_LEFT);
            inputs[index]=control(viewport,L"EDIT",index<initial.size()?initial[index]:std::string{},first_input_id+static_cast<int>(index),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL);}
        control(window,L"BUTTON",tr("Cancelar","Cancel","Annuler"),IDCANCEL,WS_TABSTOP|BS_PUSHBUTTON);
        const auto save=control(window,L"BUTTON",tr("Guardar atajos","Save shortcuts","Enregistrer les raccourcis"),IDOK,WS_TABSTOP|BS_DEFPUSHBUTTON);reader_theme::primary_button(save);
        SendMessageW(window,DM_SETDEFID,IDOK,0);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(GetWindow(window,GW_OWNER),MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=std::max(px(260),std::min(px(680),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-px(32)));
        const int height=std::max(px(320),std::min(px(740),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-px(32)));
        SetWindowPos(window,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        layout();SetFocus(inputs.front());
    }
    static LRESULT CALLBACK viewport_proc(HWND target,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
        auto& self=*reinterpret_cast<ShortcutDialog*>(data);LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        switch(message){
        case WM_COMMAND:if(HIWORD(wp)==EN_SETFOCUS)self.reveal(reinterpret_cast<HWND>(lp));return SendMessageW(self.window,message,wp,lp);
        case WM_MOUSEWHEEL:self.scroll-=GET_WHEEL_DELTA_WPARAM(wp)*self.px(48)/WHEEL_DELTA;self.layout_rows();return 0;
        case WM_VSCROLL:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(target,SB_VERT,&info);switch(LOWORD(wp)){
            case SB_LINEUP:self.scroll-=self.px(40);break;case SB_LINEDOWN:self.scroll+=self.px(40);break;
            case SB_PAGEUP:self.scroll-=self.px(240);break;case SB_PAGEDOWN:self.scroll+=self.px(240);break;
            case SB_THUMBTRACK:self.scroll=info.nTrackPos;break;default:break;}self.layout_rows();return 0;}
        case WM_ERASEBKGND:{RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::screen));return 1;}
        case WM_NCDESTROY:RemoveWindowSubclass(target,viewport_proc,1);break;
        default:break;}return DefSubclassProc(target,message,wp,lp);
    }
    static INT_PTR CALLBACK proc(HWND target,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<ShortcutDialog*>(GetWindowLongPtrW(target,DWLP_USER));
        if(message==WM_INITDIALOG){self=reinterpret_cast<ShortcutDialog*>(lp);SetWindowLongPtrW(target,DWLP_USER,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return FALSE;LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        try{switch(message){
        case WM_INITDIALOG:self->initialize(target);return FALSE;
        case WM_SIZE:self->layout();return TRUE;
        case WM_COMMAND:if(LOWORD(wp)==IDCANCEL){EndDialog(target,IDCANCEL);return TRUE;}
            if(LOWORD(wp)==IDOK){self->save();return TRUE;}if(LOWORD(wp)==defaults_id){self->restore_defaults();return TRUE;}break;
        case WM_CLOSE:EndDialog(target,IDCANCEL);return TRUE;
        case WM_DRAWITEM:return reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        case WM_ERASEBKGND:{RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::screen));return TRUE;}
        default:break;}}
        catch(const std::exception& error){MessageBoxW(target,wide(error.what()).c_str(),L"Programming shortcuts",MB_OK|MB_ICONERROR);EndDialog(target,IDCANCEL);return TRUE;}
        return FALSE;
    }
    std::optional<std::string> show(HWND owner){
        struct alignas(DWORD) Template{DLGTEMPLATE dialog{};WORD menu=0,window_class=0,title=0;} layout;
        static_assert(offsetof(Template,menu)==sizeof(DLGTEMPLATE));
        layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN|DS_MODALFRAME|DS_CENTER;
        layout.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;layout.dialog.cx=340;layout.dialog.cy=370;
        const auto code=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,GetAncestor(owner,GA_ROOT),proc,reinterpret_cast<LPARAM>(this));
        if(code==-1)throw std::runtime_error("Cannot open the programming shortcut dialog");return result;
    }
};
struct Row{HWND window;int y,height;bool dropdown=false;int actual_top=0,actual_height=0,actual_width=0;bool suppressed=false;int font_size=16;bool font_bold=false;int columns=1,column=0,actual_left=0;int minimum_gap=8;};
struct Settings {
    SettingsAccess access; HWND window=nullptr,eyebrow_label=nullptr;std::vector<Row> rows;std::vector<HFONT> fonts;
    std::string locale="es";int scroll=0,extent=0,measured_width=-1,measured_dpi=0,content_height=0;bool binding=false;POINT drag{};bool dragging=false;
    std::vector<std::string> voices;
    std::vector<std::string> voice_names;
    curriculum::LearningFlow flow_draft;
    curriculum::LearningFlow original_flow;
    bool flow_uses_global=true;
    int scale_draft=100,original_scale=100;bool inherits_scale=true,original_inherits_scale=true;
    bool wrap_draft=false,original_wrap=false,wrap_allowed=true,original_wrap_allowed=true;
    std::array<COLORREF,3> colors{},original_colors{},default_colors{},original_default_colors{};
    std::array<std::array<std::string,3>,3> color_inputs{},default_inputs{};
    std::array<bool,2> color_overwrite{true,true},original_color_overwrite{true,true};
    HFONT preview_font=nullptr;
    std::vector<RECT> selector_cards;
    std::vector<std::pair<std::size_t,std::size_t>> card_ranges;
    std::vector<std::pair<std::size_t,std::size_t>> default_ranges;
    std::vector<RECT> default_cards;
    std::size_t card_begin=0;
    bool laying_out=false;
    windows::NarrationStorageSnapshot storage;
    std::size_t kokoro_rows_begin=0,kokoro_rows_end=0,selected_voice_index=0;
    explicit Settings(SettingsAccess value):access(std::move(value)){}
    ~Settings(){if(preview_font)DeleteObject(preview_font);for(auto font:fonts)DeleteObject(font);}
    int px(int v)const{return MulDiv(v,static_cast<int>(GetDpiForWindow(window)),96);}
    std::string tr(const char* es,const char* en,const char* fr)const{return locale=="en"?en:locale=="fr"?fr:es;}
    HWND item(const wchar_t* cls,std::string text,int id,int y,int height,DWORD style=0,int size=16,bool bold=false){
        const auto instance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));
        if(wcscmp(cls,L"BUTTON")==0&&((style&BS_TYPEMASK)==BS_PUSHBUTTON||(style&BS_TYPEMASK)==BS_DEFPUSHBUTTON))style|=BS_MULTILINE;
        const auto control=CreateWindowExW(0,cls,wide(text).c_str(),WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        if(!control)throw std::runtime_error("Cannot create a native settings control");
        const bool dropdown=wcscmp(cls,L"COMBOBOX")==0;
        reader_theme::apply(control,dropdown?reader_theme::Surface::catalog_card:reader_theme::Surface::screen);
        if(wcscmp(cls,TRACKBAR_CLASSW)==0&&!SetWindowSubclass(control,slider_pointer_subclass,slider_pointer_subclass_id,0))throw std::runtime_error("Cannot preserve Settings slider pointer behaviour");
        if(wcscmp(cls,L"STATIC")==0&&bold)reader_theme::set_text_color(control,RGB(255,255,255));
        else if(dropdown)reader_theme::set_text_color(control,RGB(210,214,220));
        else if(wcscmp(cls,L"BUTTON")==0&&(style&BS_TYPEMASK)==BS_AUTOCHECKBOX)reader_theme::set_text_color(control,RGB(255,255,255));
        const auto font=CreateFontW(-MulDiv(size,static_cast<int>(GetDpiForWindow(window)),96),0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        fonts.push_back(font);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);rows.push_back({control,y,height,dropdown});rows.back().font_size=size;rows.back().font_bold=bold;return control;
    }
    HWND label(std::string text,int y,int size=16,bool bold=false,int height=34){
        const auto control=item(L"STATIC",std::move(text),0,y,height,SS_LEFT,size,bold);
        if(!bold)reader_theme::set_text_color(control,size==18?RGB(174,180,190):RGB(143,149,159));
        return control;
    }
    void begin_card(int& y){card_begin=rows.size();y+=18;}
    void end_card(int& y){
        if(rows.size()>card_begin){card_ranges.emplace_back(card_begin,rows.size());
            for(std::size_t index=card_begin;index<rows.size();++index)reader_theme::apply(rows[index].window,in_defaults(index)?reader_theme::Surface::settings_defaults:reader_theme::Surface::catalog_card);}
        y+=18;
    }
    bool in_card(std::size_t index)const{return std::ranges::any_of(card_ranges,[index](const auto& range){return index>=range.first&&index<range.second;});}
    bool in_defaults(std::size_t index)const{return std::ranges::any_of(default_ranges,[index](const auto& range){return index>=range.first&&index<range.second;});}
    void combo(int id,std::initializer_list<const char*> values,int y,int selection){
        const auto control=item(L"COMBOBOX","",id,y,64,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,18);
        for(auto text:values)SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(text).c_str()));
        SendMessageW(control,CB_SETCURSEL,selection,0);
        if(!SetWindowSubclass(control,combo_size_subclass,combo_size_subclass_id,0))throw std::runtime_error("Cannot preserve native selector dimensions");
        size_combo(control);
    }
    void checkbox(int id,std::string text,int y,bool checked,int size=18){const auto control=item(L"BUTTON",std::move(text),id,y,40,WS_TABSTOP|BS_AUTOCHECKBOX|BS_MULTILINE|BS_RIGHTBUTTON,size);
        reader_theme::switch_button(control);
        SendMessageW(control,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);}
    void progress(int& y){
        if(!access.reset_scope)return;
        label(tr("Progreso","Progress","Progression"),y,21,true);y+=50;
        const auto text=access.reset_label.empty()?tr("Reiniciar este contenido…","Reset this content…","Réinitialiser ce contenu…"):access.reset_label;
        const auto button=item(L"BUTTON",text,reset,y,54,WS_TABSTOP|BS_PUSHBUTTON,18,true);reader_theme::primary_button(button);y+=74;
        const auto details=access.scope==SettingsScope::course?
            tr("Elimina el progreso y las respuestas de todas las unidades y lecciones de este curso.","Deletes progress and answers from all units and lessons in this course.","Supprime la progression et les réponses de toutes les unités et leçons de ce cours."):
            access.scope==SettingsScope::unit?tr("Elimina el progreso y las respuestas de todas las lecciones pertenecientes a esta unidad.","Deletes progress and answers from all lessons in this unit.","Supprime la progression et les réponses de toutes les leçons de cette unité."):
            tr("Elimina las respuestas, ejercicios completados, página guardada y estado de finalización de esta lección.","Deletes answers, completed exercises, the saved page, and completion status for this lesson.","Supprime les réponses, les exercices terminés, la page enregistrée et l’état d’achèvement de cette leçon.");
        label(details,y,access.scope==SettingsScope::course?16:14,false,60);y+=86;
    }
    void flow_controls(int& y){
        checkbox(auto_lesson,tr("Avanzar automáticamente entre lecciones","Automatically Advance Between Lessons","Passer automatiquement à la leçon suivante"),y,flow_draft.between_lessons);y+=40;
        checkbox(auto_unit,tr("Avanzar automáticamente entre unidades","Automatically Advance Between Units","Passer automatiquement à l’unité suivante"),y,flow_draft.between_units);rows.back().minimum_gap=0;y+=40;
        checkbox(auto_course,tr("Avanzar automáticamente entre cursos","Automatically Advance Between Courses","Passer automatiquement au cours suivant"),y,flow_draft.between_courses);rows.back().minimum_gap=0;y+=40;
        checkbox(swipe_finish,tr("Permitir terminar una lección deslizando","Allow Swiping to Finish a Lesson","Autoriser le glissement pour terminer une leçon"),y,flow_draft.swipe_finish);rows.back().minimum_gap=0;y+=58;
        if(access.scope==SettingsScope::course)for(const auto id:{auto_lesson,auto_unit,auto_course,swipe_finish})EnableWindow(GetDlgItem(window,id),!flow_uses_global);
    }
    static bool boolean(const state::StateData& data,const char* domain,const char* key,bool fallback){const auto value=state::find(data,domain,key);return value&&std::holds_alternative<bool>(*value)?std::get<bool>(*value):fallback;}
    void load_appearance(const state::StateData& data){
        const bool course=access.scope==SettingsScope::lesson&&!access.course_id.empty();
        scale_draft=original_scale=course?appearance::course_text_scale(data,access.course_id):appearance::global_text_scale(data);
        inherits_scale=original_inherits_scale=course?appearance::inherits_text_scale(data,access.course_id):true;
        wrap_draft=original_wrap=appearance::code_wrap(data,course?access.course_id:std::string_view{});
        wrap_allowed=original_wrap_allowed=course?appearance::allows_global_wrap(data,access.course_id):true;
        static constexpr std::string_view routes[]{"","programming","math_physics"};
        for(std::size_t index=0;index<colors.size();++index){colors[index]=original_colors[index]=reader_theme::appearance_accent(data,routes[index]);default_colors[index]=original_default_colors[index]=default_accent(data,routes[index]);
            for(int channel=0;channel<3;++channel){color_inputs[index][channel]=std::to_string(rgb_channel(colors[index],channel));default_inputs[index][channel]=std::to_string(rgb_channel(default_colors[index],channel));}}
        color_overwrite[0]=original_color_overwrite[0]=boolean(data,"appearance_settings","route_allow_global_accent_overwrite_programming",true);
        color_overwrite[1]=original_color_overwrite[1]=boolean(data,"appearance_settings","route_allow_global_accent_overwrite_math_physics",true);
    }
    bool rgb_complete()const{for(const auto* groups:{&color_inputs,&default_inputs})for(const auto& group:*groups)for(const auto& text:group)if(const auto value=rgb_number(text);!value||*value>255)return false;return true;}
    bool appearance_dirty()const{return access.scope==SettingsScope::global&&(colors!=original_colors||default_colors!=original_default_colors||color_overwrite!=original_color_overwrite||!rgb_complete());}
    static std::string control_text(HWND control){if(!control)return {};std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');text.resize(GetWindowTextW(control,text.data(),static_cast<int>(text.size())));return winrt::to_string(text);}
    void set_caption(int id,const std::string& value){if(const auto control=GetDlgItem(window,id);control&&control_text(control)!=value)SetWindowTextW(control,wide(value).c_str());}
    void refresh_rgb(){const auto was_binding=binding;binding=true;
        const std::array<std::string,3> labels{tr("Rojo","Red","Rouge"),tr("Verde","Green","Vert"),tr("Azul","Blue","Bleu")};
        for(std::size_t index=0;index<colors.size();++index)for(int channel=0;channel<3;++channel){const int offset=static_cast<int>(index)*3+channel;
            set_caption(rgb_label_first+offset,labels[channel]+" · "+std::to_string(rgb_channel(colors[index],channel)));
            if(const auto slider=GetDlgItem(window,rgb_slider_first+offset))SendMessageW(slider,TBM_SETPOS,TRUE,rgb_channel(colors[index],channel));
            set_caption(rgb_input_first+offset,color_inputs[index][channel]);set_caption(rgb_default_first+offset,default_inputs[index][channel]);}
        set_caption(color_save_notice,rgb_complete()?tr("Estos cambios de color requieren Guardar cambios.","These colour changes require Save changes.","Ces modifications de couleur nécessitent Enregistrer les modifications."):
            tr("Escribe tres valores RGB de 0 a 255. Estos cambios requieren Guardar cambios.","Enter three RGB values from 0 to 255. These changes require Save changes.","Saisissez trois valeurs RVB de 0 à 255. Ces modifications nécessitent Enregistrer les modifications."));binding=was_binding;
    }
    void edit_rgb(int id){const bool defaults=id>=rgb_default_first&&id<rgb_default_first+9;const int offset=id-(defaults?rgb_default_first:rgb_input_first);
        if(offset<0||offset>=9)return;const auto index=static_cast<std::size_t>(offset/3);const int channel=offset%3;auto& text=(defaults?default_inputs:color_inputs)[index][channel];text=control_text(GetDlgItem(window,id));
        if(const auto value=rgb_number(text)){const int limited=std::min(*value,255);if(*value!=limited)text=std::to_string(limited);
            auto& color=(defaults?default_colors:colors)[index];color=rgb_with_channel(color,channel,limited);}
        refresh_appearance();
    }
    void refresh_appearance(){
        const auto slider=GetDlgItem(window,text_scale),preview=GetDlgItem(window,text_preview);
        if(slider){EnableWindow(slider,access.scope!=SettingsScope::lesson||!inherits_scale);
            const auto effective=access.scope==SettingsScope::lesson&&inherits_scale?appearance::global_text_scale(access.read()):scale_draft;
            SendMessageW(slider,TBM_SETPOS,TRUE,effective);
            const auto font=CreateFontW(-MulDiv((access.scope==SettingsScope::lesson?17:18)*effective,static_cast<int>(GetDpiForWindow(window)),9600),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
            if(font){SendMessageW(preview,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(preview_font)DeleteObject(preview_font);preview_font=font;}
            const auto size_label=(access.scope==SettingsScope::lesson?tr("Tamaño del curso","Course Content Size","Taille du contenu du cours"):tr("Tamaño del contenido","Content Size","Taille du contenu"))+" · "+std::to_string(effective)+"%"+
                (access.scope==SettingsScope::lesson&&inherits_scale?tr(" · Global"," · Global"," · Globale"):std::string{});
            SetWindowTextW(GetDlgItem(window,size_title),wide(size_label).c_str());
            const auto sample=tr("Vista previa de lectura","Reading Preview","Aperçu de lecture");
            SetWindowTextW(preview,wide(sample).c_str());
        }
        static constexpr int buttons[]{global_color,programming_color,math_color};
        const std::array<std::string,3> labels{tr("Color principal","Primary Color","Couleur principale"),tr("Color de Programación","Programming Color","Couleur de la programmation"),tr("Color de Matemáticas y Física","Mathematics and Physics Color","Couleur des mathématiques et de la physique")};
        for(std::size_t index=0;index<colors.size();++index)if(const auto control=GetDlgItem(window,buttons[index])){
            SetWindowTextW(control,wide(labels[index]).c_str());InvalidateRect(control,nullptr,FALSE);}
        refresh_rgb();
        if(const auto save=GetDlgItem(window,save_appearance)){EnableWindow(save,appearance_dirty()&&rgb_complete());ShowWindow(save,appearance_dirty()?SW_SHOW:SW_HIDE);}
        if(!binding)layout();
    }
    void text_controls(int& y){
        const bool lesson=access.scope==SettingsScope::lesson;
        if(lesson){label(tr("Tamaño del contenido","Content Size","Taille du contenu"),y,21,true);y+=40;begin_card(y);
            checkbox(text_inherit,tr("Usar tamaño global","Use Global Size","Utiliser la taille globale"),y,inherits_scale,17);y+=58;
            label(tr("Cuando está activado, este curso utiliza el tamaño configurado en las opciones generales.","When enabled, this course uses the content size configured in the general settings.","Lorsque cette option est activée, ce cours utilise la taille du contenu définie dans les paramètres généraux."),y,14,false,44);y+=60;
            item(L"STATIC","",size_separator,y,1,SS_OWNERDRAW);y+=17;
        }
        item(L"STATIC",tr("Tamaño del contenido","Content Size","Taille du contenu"),size_title,y,34,SS_LEFT,lesson?17:19,true);y+=38;
        if(access.scope==SettingsScope::global)description(y,tr("Se aplica a todos los cursos que tengan activada la opción «Usar tamaño global».","Applies to all courses with the «Use Global Size» option enabled.","S’applique à tous les cours pour lesquels l’option « Utiliser la taille globale » est activée."));
        const auto slider=item(TRACKBAR_CLASSW,"",text_scale,y,48,WS_TABSTOP|TBS_HORZ|TBS_NOTICKS);y+=56;
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(75,150));SendMessageW(slider,TBM_SETLINESIZE,0,1);SendMessageW(slider,TBM_SETPAGESIZE,0,5);SendMessageW(slider,TBM_SETPOS,TRUE,scale_draft);
        reader_theme::set_text_color(item(L"STATIC","",text_preview,y,80,SS_CENTER,lesson?17:18),RGB(255,255,255));y+=88;
        const auto range=lesson?(locale=="en"?"75%                                  150%":"75 %                                  150 %"):"75 % · 150 %";
        reader_theme::set_text_color(item(L"STATIC",range,size_range,y,34,SS_CENTER,lesson?12:16),RGB(143,149,159));y+=48;
        if(lesson)end_card(y);
    }
    void description(int& y,std::string text){label(std::move(text),y,16,false,48);y+=58;}
    void section(int& y,std::string title,std::string text){label(std::move(title),y,23,true);y+=44;
        reader_theme::set_text_color(label(std::move(text),y,16,false,48),RGB(174,180,190));y+=58;}
    void rgb_fields(std::size_t index,int& y,bool defaults){
        for(int channel=0;channel<3;++channel){const auto control=item(L"EDIT","",(defaults?rgb_default_first:rgb_input_first)+static_cast<int>(index)*3+channel,y,52,WS_TABSTOP|WS_BORDER|ES_CENTER|ES_AUTOHSCROLL|ES_NUMBER,18);
            SendMessageW(control,EM_SETLIMITTEXT,3,0);reader_theme::set_text_color(control,RGB(255,255,255));rows.back().columns=3;rows.back().column=channel;
            const auto hint=channel==0?L"R":channel==1?(locale=="fr"?L"V":L"G"):L"B";
            SendMessageW(control,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(hint));
            if(!SetWindowSubclass(control,rgb_input_subclass,rgb_input_subclass_id,0))throw std::runtime_error("Cannot preserve RGB input focus selection");}
        y+=64;
    }
    void rgb_controls(std::size_t index,int& y){
        for(int channel=0;channel<3;++channel){item(L"STATIC","",rgb_label_first+static_cast<int>(index)*3+channel,y,28,SS_LEFT,16,true);y+=34;
            const auto slider=item(TRACKBAR_CLASSW,"",rgb_slider_first+static_cast<int>(index)*3+channel,y,42,WS_TABSTOP|TBS_HORZ|TBS_NOTICKS);y+=52;
            SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,255));SendMessageW(slider,TBM_SETLINESIZE,0,1);SendMessageW(slider,TBM_SETPAGESIZE,0,8);}
        label(tr("Entrada manual","Manual Input","Saisie manuelle"),y,16,true);y+=34;rgb_fields(index,y,false);
        item(L"STATIC","",global_color+static_cast<int>(index),y,76,SS_OWNERDRAW,18,true);y+=92;
        const auto load=item(L"BUTTON",tr("Cargar color predeterminado","Load Default Color","Charger la couleur par défaut"),global_color_reset+static_cast<int>(index),y,54,WS_TABSTOP|BS_PUSHBUTTON,16);reader_theme::primary_button(load);y+=78;
        const auto start=rows.size();item(L"STATIC",tr("Valores predeterminados","Default Values","Valeurs par défaut"),rgb_default_heading_first+static_cast<int>(index),y,28,SS_LEFT,16,true);y+=34;
        description(y,tr("El botón «Cargar color predeterminado» copia estos valores al selector. Pulsa «Guardar cambios» para aplicar el color.","The «Load Default Color» button copies these values into the color picker. Select «Save Changes» to apply the color.","Le bouton « Charger la couleur par défaut » copie ces valeurs dans le sélecteur. Appuyez sur « Enregistrer les modifications » pour appliquer la couleur."));
        rgb_fields(index,y,true);default_ranges.emplace_back(start,rows.size());y+=14;
    }
    void color_controls(int& y){
        const auto notice=item(L"STATIC",tr("Estos cambios de color requieren Guardar cambios.","These colour changes require Save changes.","Ces modifications de couleur nécessitent Enregistrer les modifications."),color_save_notice,y,48,SS_LEFT,16,true);
        reader_theme::set_text_color(notice,RGB(244,91,104));y+=60;
        label(tr("Color de acento global","Global Accent Color","Couleur d’accent globale"),y,19,true);y+=34;
        description(y,tr("Elige el color principal mediante sus valores RGB. El tono secundario para textos se calculará automáticamente.","Choose the primary color using RGB values. The secondary text color will be calculated automatically.","Choisissez la couleur principale à l’aide des valeurs RVB. La couleur secondaire des textes sera calculée automatiquement."));
        begin_card(y);
        rgb_controls(0,y);end_card(y);y+=22;
        section(y,tr("Color por ruta","Learning-Path Colors","Couleur par parcours"),tr("Cada ruta puede utilizar un color distinto del color global de la aplicación.","Each learning path can use a different color from the app’s global color.","Chaque parcours peut utiliser une couleur différente de la couleur générale de l’application."));
        for(std::size_t index=1;index<colors.size();++index){begin_card(y);
            label(index==1?tr("Programación","Programming","Programmation"):tr("Matemáticas y Física","Mathematics and Physics","Mathématiques et physique"),y,19,true);y+=34;
            description(y,index==1?tr("Este color se utiliza dentro de la ruta de Programación. El menú principal de rutas continúa utilizando el color global.","This color is used within the Programming learning path. The main learning-path menu continues to use the global color.","Cette couleur est utilisée dans le parcours Programmation. Le menu principal des parcours continue d’utiliser la couleur générale."):tr("Este color se utiliza dentro de la ruta de Matemáticas y Física. El menú principal de rutas continúa utilizando el color global.","This color is used within the Mathematics and Physics learning path. The main learning-path menu continues to use the global color.","Cette couleur est utilisée dans le parcours Mathématiques et physique. Le menu principal des parcours continue d’utiliser la couleur générale."));
            checkbox(programming_color_overwrite+static_cast<int>(index)-1,tr("Permitir sobrescribir el color de acento","Allow global accent color to override","Autoriser le remplacement de la couleur d’accent"),y,color_overwrite[index-1]);y+=58;
            description(y,index==1?tr("Si está activado, al guardar un nuevo color de acento global también cambia el de Programación, salvo que guardes otro color de esta ruta al mismo tiempo. Si lo desactivas, Programación conserva su color.","When enabled, saving a new global accent color also changes the Programming color, unless you save a separate color for this path at the same time. Disable it to preserve the Programming color.","Si cette option est activée, enregistrer une nouvelle couleur d’accent globale change aussi celle du parcours Programmation, sauf si vous enregistrez une couleur propre au parcours en même temps. Désactivez-la pour préserver sa couleur."):tr("Si está activado, al guardar un nuevo color de acento global también cambia el de Matemáticas y Física, salvo que guardes otro color de esta ruta al mismo tiempo. Si lo desactivas, Matemáticas y Física conserva su color.","When enabled, saving a new global accent color also changes the Mathematics and Physics color, unless you save a separate color for this path at the same time. Disable it to preserve the Mathematics and Physics color.","Si cette option est activée, enregistrer une nouvelle couleur d’accent globale change aussi celle du parcours Mathématiques et physique, sauf si vous enregistrez une couleur propre au parcours en même temps. Désactivez-la pour préserver sa couleur."));
            rgb_controls(index,y);end_card(y);y+=20;
        }
    }
    void appearance_save_button(int& y){const auto save=item(L"BUTTON",tr("Guardar cambios","Save Changes","Enregistrer les modifications"),save_appearance,y,54,WS_TABSTOP|BS_PUSHBUTTON,18,true);reader_theme::primary_button(save);EnableWindow(save,FALSE);y+=80;}
    void save_appearance_settings(){
        if(!appearance_dirty())return;
        if(!rgb_complete()){show_settings_notice(window,tr("Escribe tres valores RGB de 0 a 255.","Enter three RGB values from 0 to 255.","Saisissez trois valeurs RVB de 0 à 255."));return;}
        access.update([&](state::StateData& data){
            if(access.scope==SettingsScope::global){
                static constexpr std::string_view routes[]{"programming","math_physics"};
                // Android saves the new protections before propagating the global
                // accent; an explicit route edit in the same Save wins afterwards.
                for(std::size_t index=0;index<color_overwrite.size();++index)
                    if(color_overwrite[index]!=original_color_overwrite[index])data.preferences["appearance_settings"]["route_allow_global_accent_overwrite_"+std::string(routes[index])]=color_overwrite[index];
                if(colors[0]!=original_colors[0]){save_accent(data,"",colors[0]);for(std::size_t index=0;index<color_overwrite.size();++index)if(color_overwrite[index])save_accent(data,routes[index],colors[0]);}
                for(std::size_t index=1;index<colors.size();++index)if(colors[index]!=original_colors[index])save_accent(data,routes[index-1],colors[index]);
                static constexpr std::string_view default_routes[]{"","programming","math_physics"};
                for(std::size_t index=0;index<default_colors.size();++index)if(default_colors[index]!=original_default_colors[index])save_default_accent(data,default_routes[index],default_colors[index]);
            }else if(access.scope==SettingsScope::lesson&&!access.course_id.empty()){
                if(scale_draft!=original_scale||inherits_scale!=original_inherits_scale)appearance::save_course_text_scale(data,access.course_id,inherits_scale,scale_draft);
                if(access.programming&&wrap_allowed!=original_wrap_allowed)appearance::save_wrap_overwrite(data,access.course_id,wrap_allowed);
            }
        });
        load_appearance(access.read());original_flow=flow_draft;
        reader_theme::set_accent(window,reader_theme::appearance_accent(access.read(),access.route_id));
        refresh_appearance();if(access.changed)access.changed();
    }
    void show_shortcuts(){
        ShortcutDialog editor(parse_state_shortcuts(access.read()),locale);
        if(const auto value=editor.show(window)){
            access.update([&](state::StateData& data){data.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"]=*value;});
            if(access.changed)access.changed();
        }
    }
    void fill_voices(const state::StateData& data){
        std::string voice_error;
        const auto tag=windows::selected_narration_language(data,interface_language(data));
        const auto index=tag=="en-US"?1:tag=="en-GB"?2:tag=="fr"?3:0;
        static constexpr const char* tags[]{"es","en-US","en-GB","fr"};
        const auto saved_engine=state::string_value(data,"lesson_narration_settings","engine","KOKORO");
        const bool system=saved_engine=="WINDOWS"||saved_engine=="ANDROID";
        if(system){voices.clear();voice_names.clear();for(const auto& value:windows::system_voices())if(windows::system_voice_matches(value.language,tags[std::clamp(index,0,3)])){voices.push_back(value.id);voice_names.push_back(value.name);}}
        else {
        voices.clear();
        try{
            for(const auto& entry:windows::kokoro_voice_catalog(std::filesystem::path(build::source_root)/"out/narration/runtime.json"))
                if(entry.language==tags[std::clamp(index,0,3)])voices.push_back(entry.id);
        }catch(...){voices.clear();voice_error=tr("No se pudo cargar el catálogo de voces de Kokoro.","The Kokoro voice catalogue could not be loaded.","Impossible de charger le catalogue de voix Kokoro.");}
        const std::string preferred=index==0?"ef_dora":index==1?"af_heart":index==2?"bf_emma":"ff_siwis";
        if(const auto found=std::ranges::find(voices,preferred);found!=voices.end())std::rotate(voices.begin(),found,found+1);
        voice_names.clear();for(const auto& id:voices){auto label=id.substr(id.find('_')+1);
            if(!label.empty()&&label.front()>='a'&&label.front()<='z')label.front()=static_cast<char>(label.front()-'a'+'A');
            voice_names.push_back(std::move(label));}}
        std::string selected;
        if(system)selected=state::string_value(data,"lesson_narration_settings",std::string("windows_voice:")+tags[std::clamp(index,0,3)],voices.empty()?"":voices.front());
        else if(!voices.empty()){
            try{selected=windows::selected_kokoro_voice(data,tags[std::clamp(index,0,3)],std::filesystem::path(build::source_root)/"out/narration/runtime.json");}
            catch(...){voices.clear();voice_names.clear();voice_error=tr("No se pudo cargar el catálogo de voces de Kokoro.","The Kokoro voice catalogue could not be loaded.","Impossible de charger le catalogue de voix Kokoro.");}
        }
        const auto found=std::ranges::find(voices,selected);selected_voice_index=found==voices.end()?0:static_cast<std::size_t>(found-voices.begin());
        refresh_narration_buttons(voice_error);
    }
    std::vector<std::string> narration_language_labels()const{return {tr("Español","Spanish","Espagnol"),tr("Inglés estadounidense","American English","Anglais américain"),tr("Inglés británico","British English","Anglais britannique"),tr("Francés","French","Français")};}
    void refresh_narration_buttons(const std::string& voice_error={}){
        const auto data=access.read();const auto tag=windows::selected_narration_language(data,interface_language(data));const auto index=tag=="en-US"?1:tag=="en-GB"?2:tag=="fr"?3:0;
        SetWindowTextW(GetDlgItem(window,language),wide(tr("Idioma: ","Language: ","Langue : ")+narration_language_labels()[index]).c_str());
        const auto selected_engine=state::string_value(data,"lesson_narration_settings","engine","KOKORO");const bool system=selected_engine=="WINDOWS"||selected_engine=="ANDROID";
        SetWindowTextW(GetDlgItem(window,engine),wide(system?tr("Motor: Windows TTS","Engine: Windows TTS","Moteur : Windows TTS"):
            selected_engine=="AUTO"?tr("Motor: automático","Engine: automatic","Moteur : automatique"):tr("Motor: Kokoro","Engine: Kokoro","Moteur : Kokoro")).c_str());
        const auto current=voices.empty()?(voice_error.empty()?tr("No hay voces disponibles para este idioma","No available voices for this language","Aucune voix disponible pour cette langue"):voice_error):
            system?voice_names[selected_voice_index]:voices[selected_voice_index];
        SetWindowTextW(GetDlgItem(window,voice),wide((system?tr("Voz Windows: ","Windows voice: ","Voix Windows : "):tr("Voz Kokoro: ","Kokoro voice: ","Voix Kokoro : "))+current).c_str());
        SetWindowTextW(GetDlgItem(window,narration_hint),wide(tr("Configura el motor de lectura y sus voces en las opciones globales.","Configure the speech engine and its voices in Global Settings.","Configurez le moteur de lecture et ses voix dans les paramètres généraux.")).c_str());
        const bool active=boolean(data,"lesson_narration_settings","enabled",true);EnableWindow(GetDlgItem(window,engine),active);EnableWindow(GetDlgItem(window,language),active);EnableWindow(GetDlgItem(window,voice),active&&!voices.empty());
    }
    void choose_narration(int id){
        const auto before=access.read();if(!boolean(before,"lesson_narration_settings","enabled",true))return;
        static constexpr const char* tags[]{"es","en-US","en-GB","fr"};const auto tag=windows::selected_narration_language(before,interface_language(before));
        const auto candidate_voices=voices;const auto previous_engine=state::string_value(before,"lesson_narration_settings","engine","KOKORO");
        const bool system=previous_engine=="WINDOWS"||previous_engine=="ANDROID";
        const auto current=id==engine?static_cast<std::size_t>(system?1:0):id==language?static_cast<std::size_t>(tag=="en-US"?1:tag=="en-GB"?2:tag=="fr"?3:0):selected_voice_index;
        auto labels=id==engine?std::vector<std::string>{tr("Kokoro neuronal","Neural Kokoro","Kokoro neuronal"),"Windows TTS"}:id==language?narration_language_labels():voice_names;
        if(id==voice&&!system&&access.scope!=SettingsScope::lesson)for(std::size_t index=0;index<labels.size();++index)labels[index]+=" · "+candidate_voices[index];
        if(labels.empty())return;
        const auto title=id==engine?tr("Motor de voz","Speech engine","Moteur vocal"):id==language?tr("Idioma de lectura","Read aloud language","Langue de lecture"):
            access.scope==SettingsScope::lesson?tr("Voz instalada","Installed voice","Voix installée"):system?tr("Voces de Windows","Windows voices","Voix Windows"):tr("Voces Kokoro","Kokoro voices","Voix Kokoro");
        const auto chosen=choose_settings_option(window,locale,title,labels,current);
        if(!chosen||*chosen==current)return;
        const auto now=access.read();if(!boolean(now,"lesson_narration_settings","enabled",true)||windows::selected_narration_language(now,interface_language(now))!=tag||state::string_value(now,"lesson_narration_settings","engine","KOKORO")!=previous_engine){fill_voices(now);return;}
        access.update([&](auto& data){auto& settings=data.preferences["lesson_narration_settings"];
            if(id==engine)settings["engine"]=std::string(*chosen==1?"WINDOWS":"KOKORO");
            else if(id==language)settings["language"]=std::string(tags[*chosen]);
            else{settings[std::string(system?"windows_voice:":"kokoro_voice:")+tag]=candidate_voices[*chosen];if(!system)settings["kokoro_voice"]=candidate_voices[*chosen];}});
        fill_voices(access.read());refresh_narration_visibility();layout();if(access.changed)access.changed();
    }
    void global_controls(int& y,const state::StateData& data){
        section(y,tr("Idioma de la aplicación","App Language","Langue de l’application"),tr("Selecciona el idioma de la aplicación, el contenido y la narración.","Choose the language of the application, content and narration.","Choisissez la langue de l’application, du contenu et de la narration."));
        const auto selected=state::string_value(data,"native_windows","ui_language","auto");
        const auto automatic=tr("Automático (idioma del dispositivo)","Automatic (device language)","Automatique (langue de l’appareil)");
        begin_card(y);combo(ui_language,{automatic.c_str(),"Español","English","Français"},y,selected=="en"?2:selected=="fr"?3:selected=="es"?1:0);y+=84;end_card(y);y+=32;
        label("Premium",y,23,true);y+=44;begin_card(y);
        label(tr("Aprende sin límites","Learn Without Limits","Apprenez sans limites"),y,19,true);y+=34;
        description(y,tr("Comprobaciones ilimitadas, sin consumir corazones.","Unlimited answer checks without using hearts.","Vérifications illimitées des réponses, sans consommer de cœurs."));
        const bool active=attempts::premium(data);
        const auto premium=item(L"BUTTON",active?tr("Usuario Premium","Premium User","Utilisateur Premium"):tr("Obtener Premium","Get Premium","Obtenir Premium"),premium_action,y,58,WS_TABSTOP|BS_PUSHBUTTON,18,true);
        if(active){reader_theme::premium_button(premium,true);EnableWindow(premium,FALSE);}else reader_theme::primary_button(premium);y+=72;
        end_card(y);y+=32;
        section(y,tr("Apariencia general","General Appearance","Apparence générale"),tr("Configura el tamaño global del contenido y el color de acento de la aplicación.","Set the global content size and the app’s accent color.","Réglez la taille globale du contenu et la couleur d’accent de l’application."));
        begin_card(y);text_controls(y);end_card(y);y+=24;color_controls(y);appearance_save_button(y);y+=20;
        narration_controls(y,data);
        section(y,tr("Flujo de aprendizaje","Learning Flow","Progression de l’apprentissage"),tr("Controla cómo continúa la aplicación cuando terminas contenido.","Control how the app continues when you complete learning content.","Contrôlez la manière dont l’application poursuit le parcours après avoir terminé du contenu."));
        begin_card(y);label(tr("Continuidad automática","Automatic Progression","Progression automatique"),y,19,true);y+=34;
        description(y,tr("Decide hasta dónde puede continuar automáticamente el estudio.","Choose how far your learning can progress automatically.","Choisissez jusqu’où l’apprentissage peut progresser automatiquement."));
        for(int index=0;index<3;++index){
            const std::array<std::string,3> names{tr("Avanzar entre lecciones","Advance Between Lessons","Passer à la leçon suivante"),tr("Avanzar entre unidades","Advance Between Units","Passer à l’unité suivante"),tr("Avanzar entre cursos","Advance Between Courses","Passer au cours suivant")};
            const std::array<std::string,3> descriptions{tr("Al terminar una lección, abre directamente la siguiente de la misma unidad.","After completing a lesson, automatically open the next lesson in the same unit.","Après avoir terminé une leçon, ouvrez directement la suivante dans la même unité."),tr("Al terminar una unidad, abre la primera lección de la siguiente.","After completing a unit, open the first lesson of the next unit.","Après avoir terminé une unité, ouvrez la première leçon de l’unité suivante."),tr("Al terminar un curso, abre la primera lección del siguiente curso disponible.","After completing a course, open the first lesson of the next available course.","Après avoir terminé un cours, ouvrez la première leçon du prochain cours disponible.")};
            const std::array<bool,3> values{flow_draft.between_lessons,flow_draft.between_units,flow_draft.between_courses};
            checkbox(auto_lesson+index,names[index],y,values[index]);y+=58;description(y,descriptions[index]);
            if(index<2){y+=4;item(L"STATIC","",flow_separator_first+index,y,1,SS_OWNERDRAW);y+=5;}
        }end_card(y);y+=32;
        section(y,tr("Gestos de navegación","Navigation Gestures","Gestes de navigation"),tr("Configura cómo responden las lecciones a los gestos.","Configure how lessons respond to gestures.","Configurez la manière dont les leçons réagissent aux gestes."));
        begin_card(y);checkbox(swipe_finish,tr("Terminar una lección deslizando","Finish a Lesson by Swiping","Terminer une leçon par glissement"),y,flow_draft.swipe_finish);y+=58;
        description(y,tr("Permite que un deslizamiento hacia adelante en la última página tenga el mismo efecto que «Terminar lección».","Allows a forward swipe on the last page to have the same effect as selecting «Finish Lesson».","Permet à un glissement vers l’avant sur la dernière page d’avoir le même effet que « Terminer la leçon »."));end_card(y);y+=32;
        section(y,tr("Código","Code","Code"),tr("Configura el ajuste de línea predeterminado de los bloques de código.","Configure the default line wrapping for code blocks.","Configurez le retour à la ligne par défaut des blocs de code."));
        begin_card(y);checkbox(code_wrap,tr("Ajuste de línea predeterminado","Default Line Wrapping","Retour à la ligne par défaut"),y,wrap_draft);y+=58;
        description(y,tr("Al cambiar este ajuste, el nuevo valor se aplica a los cursos que permiten sobrescribir su ajuste de línea desde las opciones globales.","Changes apply to courses that allow their line wrapping to be overridden by global options.","Les modifications s’appliquent aux cours qui autorisent les options globales à remplacer leur retour à la ligne."));end_card(y);y+=24;
        section(y,tr("Excepciones de ajuste de línea","Line Wrapping Exceptions","Exceptions de retour à la ligne"),tr("Restablece las excepciones individuales de una ruta y vuelve a permitir que sus cursos reciban el ajuste de línea predeterminado.","Reset individual exceptions for a learning path and allow its courses to use the default line wrapping setting again.","Réinitialisez les exceptions individuelles d’un parcours pour permettre à ses cours d’utiliser à nouveau le paramètre de retour à la ligne par défaut."));
        for(int index=0;index<2;++index){begin_card(y);label(index==0?tr("Programación","Programming","Programmation"):tr("Matemáticas y Física","Mathematics and Physics","Mathématiques et physique"),y,19,true);y+=34;
            description(y,index==0?tr("Los cursos de esta ruta volverán a permitir la sobrescritura global y recibirán el ajuste de línea predeterminado actual.","Courses in this path will allow global overrides again and receive the current default line wrapping.","Les cours de ce parcours autoriseront à nouveau le réglage global et recevront le retour à la ligne par défaut actuel."):
                tr("Esta ruta aún no tiene cursos ni excepciones de ajuste de línea. El botón solo muestra un aviso; no restablece ninguna configuración.","This learning path has no courses or line wrapping exceptions yet. The button only shows a notice; it does not reset any settings.","Ce parcours n’a pas encore de cours ni d’exceptions de retour à la ligne. Le bouton affiche seulement un message ; aucun réglage n’est réinitialisé."));
            const auto button=item(L"BUTTON",index==0?tr("Restablecer excepciones de Programación","Reset Programming Exceptions","Réinitialiser les exceptions de programmation"):tr("Consultar disponibilidad de excepciones","Check exception availability","Vérifier la disponibilité des exceptions"),index==0?reset_wrap_exceptions:reset_math_wrap_exceptions,y,58,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::primary_button(button);y+=72;end_card(y);y+=20;
        }
        if(access.reset_route){section(y,tr("Datos y progreso","Data and Progress","Données et progression"),tr("Reinicia el progreso guardado de cada ruta sin cambiar sus ajustes.","Reset each learning path’s saved progress without changing its settings.","Réinitialisez la progression enregistrée de chaque parcours sans modifier ses réglages."));
            for(int index=0;index<2;++index){begin_card(y);label(index==0?tr("Ruta de Programación","Programming Learning Path","Parcours de programmation"):tr("Ruta de Matemáticas y Física","Mathematics and Physics path","Parcours Mathématiques et Physique"),y,19,true);y+=34;
                description(y,tr("Elimina el progreso, las respuestas y las páginas guardadas de todos los cursos de esta ruta.","Remove progress, answers and saved pages for all courses in this learning path.","Supprimez la progression, les réponses et les pages enregistrées de tous les cours de ce parcours."));
                const auto button=item(L"BUTTON",index==0?tr("Reiniciar progreso de Programación","Reset Programming Progress","Réinitialiser la progression en programmation"):tr("Reiniciar progreso de Matemáticas y Física","Reset Mathematics and Physics progress","Réinitialiser la progression de Mathématiques et Physique"),index==0?reset_programming:reset_math,y,58,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::primary_button(button);y+=72;
                description(y,tr("Los ajustes generales y las configuraciones particulares de los cursos se conservarán.","General settings and individual course configurations will be preserved.","Les paramètres généraux et les configurations particulières des cours seront conservés."));end_card(y);y+=20;}
        }
    }
    void refresh_narration_visibility(){
        if(!GetDlgItem(window,engine))return;
        const auto data=access.read();
        const bool active=boolean(data,"lesson_narration_settings","enabled",true);
        for(const auto id:{engine,language,voice})EnableWindow(GetDlgItem(window,id),active&&(id!=voice||!voices.empty()));
        // Android keeps the existing Kokoro recordings and preparation controls
        // available when the playback engine changes. Only the master switch
        // disables this section; selecting system speech does not remove it.
        for(std::size_t index=kokoro_rows_begin;index<kokoro_rows_end&&index<rows.size();++index){rows[index].suppressed=false;ShowWindow(rows[index].window,SW_SHOW);EnableWindow(rows[index].window,active);}
        EnableWindow(GetDlgItem(window,licenses_action),active);
    }
    std::string storage_size(std::uint64_t bytes)const{char value[40]{};std::snprintf(value,sizeof(value),"%.2f",static_cast<double>(bytes)/(1024.*1024.*1024.));std::string number(value);if(locale!="en")std::replace(number.begin(),number.end(),'.',',');return number+tr(" GB"," GB"," Go");}
    void refresh_storage_detail(std::uint64_t selected){
        const auto limit=selected?storage_size(selected):tr("0 GB (pregeneración pausada)","0 GB (pre-rendering paused)","0 Go (prégénération en pause)");
        auto detail=tr("Límite: ","Limit: ","Limite : ")+limit+tr(" · Ya guardado: "," · Already saved: "," · Déjà enregistré : ")+storage_size(storage.stored_bytes);
        if(storage.stored_bytes>=selected)detail+=" "+tr("No se generarán nuevos audios automáticos hasta tener espacio disponible.","No new background audio will be generated until there is room.","Aucun nouvel audio automatique tant que l’espace reste insuffisant.");
        SetWindowTextW(GetDlgItem(window,storage_detail),wide(detail).c_str());
        SetWindowTextW(GetDlgItem(window,storage_maximum),wide(tr("Máximo disponible: ","Available maximum: ","Maximum disponible : ")+storage_size(storage.maximum_bytes)+tr(" (con 1 GB de reserva)"," (with 1 GB reserved)"," (1 Go réservé)")).c_str());
    }
    void narration_controls(int& y,const state::StateData& data){
        section(y,tr("Lectura en voz alta","Read aloud","Lecture à voix haute"),tr("Configura la lectura de las lecciones, el motor, las voces y sus recursos.","Configure lesson narration, the speech engine, voices, and their resources.","Configurez la lecture des leçons, le moteur vocal, les voix et leurs ressources."));
        begin_card(y);label(tr("Ajustes generales de narración","General narration settings","Paramètres généraux de narration"),y,18,true);y+=42;
        checkbox(enabled,tr("Activar narración de las lecciones","Enable lesson narration","Activer la narration des leçons"),y,boolean(data,"lesson_narration_settings","enabled",true));y+=52;
        for(const auto id:{engine,language,voice}){reader_theme::primary_button(item(L"BUTTON","",id,y,56,WS_TABSTOP|BS_PUSHBUTTON,17));y+=72;}
        fill_voices(data);
        kokoro_rows_begin=rows.size();
        y+=14;item(L"STATIC","",kokoro_separator,y,1,SS_OWNERDRAW);y+=13;
        item(L"STATIC",tr("Voces y recursos Kokoro","Kokoro voices and resources","Voix et ressources Kokoro"),kokoro_heading,y,34,SS_LEFT,18,true);y+=46;
        if(access.storage_snapshot){storage=access.storage_snapshot();
            label(tr("Almacenamiento para audios Kokoro","Kokoro audio storage","Stockage audio Kokoro"),y,17,true);y+=38;
            item(L"STATIC","",storage_detail,y,46,SS_LEFT,15);y+=54;
            const auto slider=item(TRACKBAR_CLASSW,"",storage_limit,y,48,WS_TABSTOP|TBS_HORZ|TBS_NOTICKS);y+=56;
            const auto steps=static_cast<int>(std::min<std::uint64_t>(std::numeric_limits<int>::max(),storage.maximum_bytes/storage_step));
            SendMessageW(slider,TBM_SETRANGEMIN,FALSE,0);SendMessageW(slider,TBM_SETRANGEMAX,TRUE,steps);
            SendMessageW(slider,TBM_SETPOS,TRUE,static_cast<int>(std::min<std::uint64_t>(steps,storage.effective_bytes/storage_step)));
            item(L"STATIC","",storage_maximum,y,30,SS_LEFT,14);y+=40;refresh_storage_detail(storage.effective_bytes);
            item(L"STATIC",tr("Límite para la pregeneración automática. Se reserva al menos 1 GB libre; los audios terminados nunca se borran automáticamente.","Limit for automatic pre-rendering. At least 1 GB stays free; finished audio is never deleted automatically.","Limite de prégénération automatique. Au moins 1 Go reste libre ; les audios terminés ne sont jamais supprimés automatiquement."),storage_explanation,y,60,SS_LEFT,14);y+=70;
        }
        checkbox(advance,tr("Generar anticipadamente los audios de las siguientes lecciones y cursos","Generate audio for upcoming lessons and courses in advance","Générer à l’avance l’audio des prochaines leçons et cours"),y,boolean(data,"lesson_narration_settings","kokoro_advance_generation",true));y+=72;
        if(access.open_audio){label(tr("Audios Kokoro por curso","Kokoro audio by course","Audios Kokoro par cours"),y,17,true);y+=38;
            for(int index=0;index<2;++index){const auto button=item(L"BUTTON",index==0?tr("Audios de Programación","Programming audio","Audios de programmation"):tr("Audios de Matemáticas y Física","Mathematics and Physics audio","Audios de mathématiques et physique"),index==0?audio_programming:audio_mathematics,y,54,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::primary_button(button);y+=66;}}
        kokoro_rows_end=rows.size();
        for(std::size_t index=card_begin;index<rows.size();++index){wchar_t kind[16]{};GetClassNameW(rows[index].window,kind,16);
            const auto font=reinterpret_cast<HFONT>(SendMessageW(rows[index].window,WM_GETFONT,0,0));LOGFONTW details{};
            if(_wcsicmp(kind,L"STATIC")==0&&GetObjectW(font,sizeof(details),&details)&&details.lfWeight<FW_BOLD)
                reader_theme::set_text_color(rows[index].window,RGB(210,214,220));}
        end_card(y);y+=24;
        refresh_narration_visibility();
        if(access.open_licenses){label(tr("Licencias","Licenses","Licences"),y,19,true);y+=38;begin_card(y);const auto button=item(L"BUTTON",tr("Licencias y atribuciones de lectura","Read aloud licenses and credits","Licences et crédits de lecture"),licenses_action,y,54,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::primary_button(button);EnableWindow(button,boolean(data,"lesson_narration_settings","enabled",true));y+=66;end_card(y);y+=24;}
    }
    void build(){
        binding=true;struct BindingGuard{bool& value;~BindingGuard(){value=false;}} binding_guard{binding};
        const auto data=access.read();locale=interface_language(data);load_appearance(data);
        const auto route=access.route_id.empty()?(access.programming?"programming":access.scope==SettingsScope::course||access.scope==SettingsScope::unit||access.scope==SettingsScope::lesson?"math_physics":""):access.route_id;
        reader_theme::set_accent(window,reader_theme::appearance_accent(data,route));
        auto button=item(L"BUTTON",tr("Volver","Back","Retour"),back,20,56,WS_TABSTOP|BS_OWNERDRAW);reader_theme::primary_button(button);
        const bool scoped=!access.eyebrow.empty();
        if(scoped)eyebrow_label=item(L"STATIC",access.eyebrow,0,94,30,SS_LEFT,16,true);
        const bool lesson_scope=access.scope==SettingsScope::lesson;
        label(lesson_scope?tr("Ajustes de la lección","Lesson Settings","Paramètres de la leçon"):access.title.empty()?tr("Opciones","Settings","Paramètres"):access.title,scoped?132:96,lesson_scope?30:(access.scope==SettingsScope::course||access.scope==SettingsScope::unit)?28:32,true,68);
        label(lesson_scope?(access.programming?tr("Configura la experiencia de esta lección y las herramientas de programación.","Configure your experience in this lesson and the programming tools.","Configurez votre expérience dans cette leçon et les outils de programmation."):tr("Configura el tamaño del contenido y el progreso de esta lección.","Configure content size and progress for this lesson.","Configurez la taille du contenu et la progression de cette leçon.")):
            access.subtitle.empty()?tr("Configuración general de LearnGameAndEngineDev","General LearnGameAndEngineDev settings","Configuration générale de LearnGameAndEngineDev"):access.subtitle,scoped?210:150,lesson_scope?16:18,false,44);
        int y=scoped?298:222;
        if(access.scope==SettingsScope::unit){progress(y);extent=y+20;binding=false;layout();return;}
        flow_draft=original_flow=curriculum::learning_flow(data,access.scope==SettingsScope::course?access.course_id:std::string{});
        if(access.scope==SettingsScope::course){
            flow_uses_global=!curriculum::has_course_flow(data,access.course_id);
            checkbox(use_global,tr("Usar configuración global","Use Global Settings","Utiliser les paramètres généraux"),y,flow_uses_global);y+=62;
            description(y,tr("Si está activado, este curso utiliza automáticamente los ajustes generales de la aplicación.","When enabled, this course automatically uses the general app settings.","Lorsque cette option est activée, ce cours utilise automatiquement les paramètres généraux de l’application."));
            flow_controls(y);
            y+=32;progress(y);
            extent=y+20;binding=false;layout();return;
        }
        if(lesson_scope){
            label(tr("Narración","Narration","Narration"),y,21,true);y+=40;begin_card(y);
            reader_theme::primary_button(item(L"BUTTON","",language,y,56,WS_TABSTOP|BS_PUSHBUTTON,17));y+=72;
            reader_theme::primary_button(item(L"BUTTON","",voice,y,56,WS_TABSTOP|BS_PUSHBUTTON,17));y+=72;
            reader_theme::set_text_color(item(L"STATIC","",narration_hint,y,44,SS_LEFT,14),RGB(183,187,196));y+=56;
            fill_voices(data);end_card(y);y+=32;
            label(tr("Navegación de páginas","Page navigation","Navigation entre les pages"),y,21,true);y+=40;begin_card(y);
            checkbox(swipe_navigation,tr("Deslizar para cambiar de página","Swipe to change pages","Balayer pour changer de page"),y,boolean(data,"app_settings","swipeNavigationEnabled",true),17);y+=58;
            label(tr("Permite pasar entre páginas de la lección deslizando horizontalmente.","Move between lesson pages by dragging horizontally.","Passez d’une page de la leçon à l’autre en balayant horizontalement."),y,14,false,44);y+=52;end_card(y);y+=40;
            if(!access.course_id.empty()){text_controls(y);y+=32;}
            if(access.programming){
                label(tr("Ajuste de línea","Line Wrapping","Retour à la ligne"),y,21,true);y+=40;begin_card(y);
                checkbox(wrap_overwrite,tr("Permitir que el ajuste global sobrescriba este curso","Allow global settings to overwrite this course","Autoriser le réglage global à remplacer celui de ce cours"),y,wrap_allowed,17);y+=62;
                label(tr("Activado por defecto. Permite que los cambios realizados en el ajuste de línea predeterminado sobrescriban este curso. Desactivarlo no bloquea el botón de ajuste de línea de las consolas.","Enabled by default. Allows changes to the default line wrapping setting to override this course. Disabling it does not disable the line wrapping button in code consoles.","Activé par défaut. Permet aux modifications du retour à la ligne par défaut de remplacer le réglage de ce cours. Désactiver cette option ne bloque pas le bouton de retour à la ligne des consoles de code."),y,14,false,78);y+=88;end_card(y);y+=16;
            }
            progress(y);extent=y+24;refresh_appearance();binding=false;layout();return;
        }
        if(access.scope==SettingsScope::audio&&!access.route_id.empty()){
            description(y,tr("Activa o pausa la preparación anticipada de los audios de cada curso. Los audios ya guardados y la lectura de la página actual se conservan.","Enable or pause advance audio preparation for each course. Saved audio and current-page reading are preserved.","Activez ou suspendez la préparation audio de chaque cours. Les audios enregistrés et la lecture de la page actuelle sont conservés."));
            for(std::size_t index=0;index<access.audio_courses.size();++index){begin_card(y);const auto& course=access.audio_courses[index];
                const auto key="kokoro_paused_course:"+access.route_id+":"+course.first;const auto paused=boolean(data,"lesson_narration_settings",key.c_str(),false);
                checkbox(audio_course_first+static_cast<int>(index),course.second,y,!paused);y+=62;end_card(y);y+=20;}
            extent=y+20;binding=false;layout();return;
        }
        if(access.scope==SettingsScope::global){global_controls(y,data);extent=y+20;refresh_appearance();binding=false;layout();return;}
        narration_controls(y,data);
        extent=y+20;refresh_appearance();binding=false;layout();
    }
    void paint_background(HDC dc)const{
        if(!dc)return;
        RECT area{};GetClientRect(window,&area);FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::screen));
        for(const auto& bounds:selector_cards)FillRect(dc,&bounds,reader_theme::brush(reader_theme::Surface::catalog_card));
        for(const auto& bounds:default_cards)FillRect(dc,&bounds,reader_theme::brush(reader_theme::Surface::settings_defaults));
    }
    void refresh_language(){
        if(locale==interface_language(access.read()))return;
        // Rebuild only the settings controls. Buffered course/global edits are
        // retained separately from persisted state, exactly as before Save.
        const auto pending=std::tuple{flow_draft,original_flow,flow_uses_global,scale_draft,original_scale,inherits_scale,original_inherits_scale,
            wrap_draft,original_wrap,wrap_allowed,original_wrap_allowed,colors,original_colors,default_colors,original_default_colors,color_inputs,default_inputs,color_overwrite,original_color_overwrite};
        const auto previous_scroll=scroll;const auto focus=GetFocus();const auto focused_id=focus&&IsChild(window,focus)?GetDlgCtrlID(focus):0;
        SendMessageW(window,WM_SETREDRAW,FALSE,0);binding=true;
        for(const auto& row:rows)DestroyWindow(row.window);rows.clear();card_ranges.clear();selector_cards.clear();default_ranges.clear();default_cards.clear();eyebrow_label=nullptr;
        if(preview_font){DeleteObject(preview_font);preview_font=nullptr;}for(const auto font:fonts)DeleteObject(font);fonts.clear();
        build();binding=true;
        std::tie(flow_draft,original_flow,flow_uses_global,scale_draft,original_scale,inherits_scale,original_inherits_scale,
            wrap_draft,original_wrap,wrap_allowed,original_wrap_allowed,colors,original_colors,default_colors,original_default_colors,color_inputs,default_inputs,color_overwrite,original_color_overwrite)=pending;
        const auto set_check=[&](int id,bool value){if(const auto control=GetDlgItem(window,id))SendMessageW(control,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);};
        set_check(auto_lesson,flow_draft.between_lessons);set_check(auto_unit,flow_draft.between_units);set_check(auto_course,flow_draft.between_courses);set_check(swipe_finish,flow_draft.swipe_finish);
        set_check(use_global,flow_uses_global);set_check(code_wrap,wrap_draft);set_check(text_inherit,inherits_scale);set_check(wrap_overwrite,wrap_allowed);
        set_check(programming_color_overwrite,color_overwrite[0]);set_check(math_color_overwrite,color_overwrite[1]);
        if(const auto slider=GetDlgItem(window,text_scale))SendMessageW(slider,TBM_SETPOS,TRUE,scale_draft);
        if(access.scope==SettingsScope::course)for(const auto id:{auto_lesson,auto_unit,auto_course,swipe_finish})EnableWindow(GetDlgItem(window,id),!flow_uses_global);
        scroll=previous_scroll;binding=false;refresh_appearance();
        if(focused_id)if(const auto control=GetDlgItem(window,focused_id))SetFocus(control);
        const auto parent=GetParent(window);wchar_t name[80]{};GetClassNameW(parent,name,80);
        if(std::wstring_view(name)==L"LearnGameAndEngineDev.SettingsWindow"&&access.title.empty())SetWindowTextW(parent,wide(tr("Opciones","Settings","Paramètres")).c_str());
        SendMessageW(window,WM_SETREDRAW,TRUE,0);RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void refresh_fonts(){
        std::vector<HFONT> replacements;replacements.reserve(rows.size());
        for(const auto& row:rows){const auto font=CreateFontW(-px(row.font_size),0,0,0,row.font_bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
            if(!font){for(const auto value:replacements)DeleteObject(value);throw std::runtime_error("Cannot resize the settings fonts for this display");}replacements.push_back(font);}
        SendMessageW(window,WM_SETREDRAW,FALSE,0);const auto previous_binding=binding;binding=true;
        for(std::size_t index=0;index<rows.size();++index)SendMessageW(rows[index].window,WM_SETFONT,reinterpret_cast<WPARAM>(replacements[index]),FALSE);
        for(const auto font:fonts)DeleteObject(font);fonts=std::move(replacements);
        refresh_appearance();binding=previous_binding;SendMessageW(window,WM_SETREDRAW,TRUE,0);layout();
    }
    void layout(bool measure=true){
        if(laying_out||!window)return;laying_out=true;struct LayoutGuard{bool& value;~LayoutGuard(){value=false;}} guard{laying_out};
        RECT area{};GetClientRect(window,&area);const auto margin=std::min(px(24),std::max(0,static_cast<int>(area.right)/4));
        const auto width=std::max(1,static_cast<int>(area.right)-2*margin),padding=px(18);
        measure=measure||measured_width!=area.right||measured_dpi!=static_cast<int>(GetDpiForWindow(window));
        int cursor=0,authored_end=0;
        if(measure)for(auto& row:rows){
            const auto row_index=static_cast<std::size_t>(&row-rows.data());const auto inner=in_card(row_index);
            const auto id=GetDlgCtrlID(row.window);if(row.suppressed||(id==save_appearance&&!appearance_dirty())){row.actual_height=0;row.actual_top=cursor;row.actual_width=width;authored_end=row.y+row.height;continue;}const int gap=row.y-authored_end;
            const auto nested=in_defaults(row_index);const int inset=(inner?padding:0)+(nested?px(14):0),usable=std::max(1,width-2*inset);
            row.actual_top=row.column>0?rows[row_index-1].actual_top:rows.data()==&row?px(row.y):cursor+px(std::max(row.minimum_gap,gap));
            row.actual_width=id==back?px(56):row.columns>1?std::max(1,(usable-px(8)*(row.columns-1))/row.columns):usable;
            row.actual_left=margin+inset+row.column*(row.actual_width+px(8));
            if(row.dropdown){
                const auto inner_width=std::max(1,width-2*padding);RECT native{};GetWindowRect(row.window,&native);
                const auto selection=static_cast<int>(SendMessageW(row.window,CB_GETITEMHEIGHT,static_cast<WPARAM>(-1),0));
                const auto frame=std::max(0,static_cast<int>(native.bottom-native.top)-selection);
                row.actual_height=combo_selection_height(row.window,inner_width)+frame+(inner?0:2*padding);
            }else{
                wchar_t name[32]{};GetClassNameW(row.window,name,32);
                if(id==back)row.actual_height=px(56);
                else if(separator_id(id))row.actual_height=px(1);
                else if(id>=global_color&&id<=math_color)row.actual_height=px(76);
                else if(_wcsicmp(name,L"BUTTON")==0){
                    const auto type=GetWindowLongPtrW(row.window,GWL_STYLE)&BS_TYPEMASK;
                    const bool check=type==BS_AUTOCHECKBOX||type==BS_CHECKBOX;
                    const auto text_inset=check?px(40):(id>=global_color&&id<=math_color?px(76):px(28));
                    row.actual_height=std::max(px(check?58:62),measured_text_height(row.window,std::max(1,row.actual_width-text_inset))+px(check?12:16));
                }else if(_wcsicmp(name,L"STATIC")==0)row.actual_height=std::max(px(24),measured_text_height(row.window,row.actual_width)+px(4));
                else row.actual_height=px(row.height);
            }
            cursor=std::max(cursor,row.actual_top+row.actual_height);authored_end=row.y+row.height;
        }
        if(measure){content_height=cursor+px(24);measured_width=area.right;measured_dpi=static_cast<int>(GetDpiForWindow(window));}
        scroll=std::clamp(scroll,0,std::max(0,content_height-static_cast<int>(area.bottom)));
        struct Placement{HWND control;int left,top,width,height;};std::vector<Placement> placements;placements.reserve(rows.size());
        SendMessageW(window,WM_SETREDRAW,FALSE,0);selector_cards.clear();default_cards.clear();
        for(const auto& range:card_ranges)selector_cards.push_back({margin,rows[range.first].actual_top-scroll-padding,margin+width,rows[range.second-1].actual_top+rows[range.second-1].actual_height-scroll+padding});
        for(const auto& range:default_ranges)default_cards.push_back({margin+padding,rows[range.first].actual_top-scroll-px(14),margin+width-padding,rows[range.second-1].actual_top+rows[range.second-1].actual_height-scroll+px(14)});
        for(const auto& row:rows){if(row.actual_height==0)continue;const auto top=row.actual_top-scroll;const auto inner=in_card(static_cast<std::size_t>(&row-rows.data()));
            if(row.dropdown){
                if(!inner)selector_cards.push_back({margin,top,margin+width,top+row.actual_height});
                const auto inner_width=std::max(1,width-2*padding),field=combo_selection_height(row.window,inner_width);
                placements.push_back({row.window,margin+padding,top+(inner?0:padding),inner_width,field+px(210)});
            }else placements.push_back({row.window,row.actual_left,top,row.actual_width,row.actual_height});
        }
        // All controls keep their own HWND, order and exact authored geometry.
        // Submit their moves together instead of recomputing the native sibling
        // regions separately for every offscreen control on every wheel step.
        auto batch=BeginDeferWindowPos(static_cast<int>(placements.size()));bool positioned=false;
        if(batch){for(const auto& placement:placements){batch=DeferWindowPos(batch,placement.control,nullptr,placement.left,placement.top,placement.width,placement.height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|(measure?0:SWP_NOSIZE));if(!batch)break;}
            if(batch)positioned=EndDeferWindowPos(batch)!=FALSE;}
        // A failed sequence owns no usable HDWP. Apply the complete set again,
        // including any earlier entries, rather than leaving a partial layout.
        if(!positioned)for(const auto& placement:placements)MoveWindow(placement.control,placement.left,placement.top,placement.width,placement.height,FALSE);
        if(measure)for(const auto& row:rows)if(row.dropdown&&row.actual_height>0)size_combo(row.window);
        SendMessageW(window,WM_SETREDRAW,TRUE,0);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_height-1),static_cast<UINT>(area.bottom),scroll,0};SetScrollInfo(window,SB_VERT,&info,TRUE);reader_theme::refresh_scrollbars(window);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        RECT adjusted{};GetClientRect(window,&adjusted);if(adjusted.right!=area.right)PostMessageW(window,WM_SIZE,0,MAKELPARAM(adjusted.right,adjusted.bottom));
    }
    void change(int id,int notification){if(id==back){PostMessageW(window,close_message,0,0);return;}if(binding)return;if(id==reset){if(access.reset_scope)access.reset_scope();return;}
        if(notification==EN_CHANGE&&((id>=rgb_input_first&&id<rgb_input_first+9)||(id>=rgb_default_first&&id<rgb_default_first+9))){edit_rgb(id);return;}
        if(id==engine||id==language||id==voice){if(notification==BN_CLICKED)choose_narration(id);return;}
        if((id==reset_programming||id==reset_math)&&access.reset_route){access.reset_route(id==reset_programming?"programming":"math_physics");return;}
        if(notification!=CBN_SELCHANGE&&notification!=BN_CLICKED)return;
        if(id==premium_action){MessageBoxW(window,wide(tr("Esta versión es para uso personal. Premium solo se activa desde el código.","This version is for personal use. Premium is enabled only in code.","Cette version est réservée à un usage personnel. Premium s’active uniquement dans le code.")).c_str(),L"Premium",MB_OK|MB_ICONINFORMATION);return;}
        if(id==audio_programming||id==audio_mathematics){if(boolean(access.read(),"lesson_narration_settings","enabled",true)&&access.open_audio)access.open_audio(id==audio_programming?"programming":"math_physics");return;}
        if(id==licenses_action){if(boolean(access.read(),"lesson_narration_settings","enabled",true)&&access.open_licenses)access.open_licenses();return;}
        const auto selection=static_cast<int>(SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0));
        const bool checked=SendDlgItemMessageW(window,id,BM_GETCHECK,0,0)==BST_CHECKED;
        if(id==save_appearance){save_appearance_settings();return;}
        if(id==edit_shortcuts){show_shortcuts();return;}
        if(id>=global_color&&id<=math_color)return;
        if(id>=global_color_reset&&id<=math_color_reset){const auto index=static_cast<std::size_t>(id-global_color_reset);
            if(std::ranges::any_of(default_inputs[index],[](const auto& text){const auto value=rgb_number(text);return !value||*value>255;})){show_settings_notice(window,tr("Escribe tres valores RGB de 0 a 255.","Enter three RGB values from 0 to 255.","Saisissez trois valeurs RVB de 0 à 255."));return;}
            colors[index]=default_colors[index];for(int channel=0;channel<3;++channel)color_inputs[index][channel]=std::to_string(rgb_channel(colors[index],channel));refresh_appearance();return;}
        if(id==text_inherit){inherits_scale=original_inherits_scale=checked;access.update([&](auto& data){appearance::save_course_text_scale(data,access.course_id,checked,scale_draft);});refresh_appearance();if(access.changed)access.changed();return;}
        if(id==wrap_overwrite){wrap_allowed=original_wrap_allowed=checked;access.update([&](auto& data){appearance::save_wrap_overwrite(data,access.course_id,checked);});refresh_appearance();if(access.changed)access.changed();return;}
        if(id==programming_color_overwrite||id==math_color_overwrite){color_overwrite[id-programming_color_overwrite]=checked;refresh_appearance();return;}
        if(id==reset_math_wrap_exceptions){
            const auto message=tr("Todavía no hay cursos disponibles en esta ruta.\n\nCuando se agreguen cursos, este botón permitirá restablecer sus excepciones de ajuste de línea.","There are no courses available in this learning path yet.\n\nWhen courses are added, this button will allow you to reset their line wrapping exceptions.","Aucun cours n’est encore disponible dans ce parcours.\n\nLorsque des cours seront ajoutés, ce bouton permettra de réinitialiser leurs exceptions de retour à la ligne.");
            MessageBoxW(window,wide(message).c_str(),wide(tr("Matemáticas y Física","Mathematics and Physics","Mathématiques et physique")).c_str(),MB_OK|MB_ICONINFORMATION);return;
        }
        if(id==reset_wrap_exceptions){
            const auto& courses=access.programming_course_ids;
            const auto title=tr("Restablecer excepciones de Programación","Reset Programming Exceptions","Réinitialiser les exceptions de programmation");
            const auto description=tr("Todos los cursos de Programación volverán a permitir que las Opciones globales sobrescriban su ajuste de línea.\n\nAdemás, todos recibirán inmediatamente el ajuste de línea predeterminado actualmente guardado.","All Programming courses will once again allow global settings to override their line wrapping preferences.\n\nThey will also immediately receive the currently saved default line wrapping setting.","Tous les cours de Programmation autoriseront à nouveau les paramètres généraux à remplacer leur réglage de retour à la ligne.\n\nIls recevront également immédiatement le réglage de retour à la ligne par défaut actuellement enregistré.");
            if(MessageBoxW(window,wide(description).c_str(),wide(title).c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return;
            access.update([&](state::StateData& data){appearance::reset_wrap_exceptions(data,courses);});
            show_settings_notice(window,tr("Excepciones de Programación restablecidas","Programming Exceptions Reset","Exceptions de programmation réinitialisées"));
            if(access.changed)access.changed();return;
        }
        if(access.scope==SettingsScope::global&&(id==auto_lesson||id==auto_unit||id==auto_course||id==swipe_finish||id==code_wrap)){
            if(id==auto_lesson)flow_draft.between_lessons=checked;if(id==auto_unit)flow_draft.between_units=checked;
            if(id==auto_course)flow_draft.between_courses=checked;if(id==swipe_finish)flow_draft.swipe_finish=checked;
            if(id==code_wrap)wrap_draft=original_wrap=checked;access.update([&](auto& data){if(id==code_wrap)appearance::save_global_wrap(data,checked,access.programming_course_ids);else curriculum::save_global_flow(data,flow_draft);});original_flow=flow_draft;refresh_appearance();if(access.changed)access.changed();return;
        }
        if(access.scope==SettingsScope::course&&(id==use_global||id==auto_lesson||id==auto_unit||id==auto_course||id==swipe_finish)){
            if(id==use_global){flow_uses_global=checked;
                if(checked){flow_draft=curriculum::learning_flow(access.read());const std::array<bool,4> values{flow_draft.between_lessons,flow_draft.between_units,flow_draft.between_courses,flow_draft.swipe_finish};
                    for(std::size_t index=0;index<values.size();++index)SendDlgItemMessageW(window,auto_lesson+static_cast<int>(index),BM_SETCHECK,values[index]?BST_CHECKED:BST_UNCHECKED,0);}
                for(const auto control:{auto_lesson,auto_unit,auto_course,swipe_finish})EnableWindow(GetDlgItem(window,control),!checked);}
            if(id==auto_lesson)flow_draft.between_lessons=checked;if(id==auto_unit)flow_draft.between_units=checked;
            if(id==auto_course)flow_draft.between_courses=checked;if(id==swipe_finish)flow_draft.swipe_finish=checked;
            access.update([&](auto& data){if(flow_uses_global)curriculum::clear_course_flow(data,access.course_id);else curriculum::save_course_flow(data,access.course_id,flow_draft);});original_flow=flow_draft;if(access.changed)access.changed();return;
        }
        if(id>=audio_course_first&&static_cast<std::size_t>(id-audio_course_first)<access.audio_courses.size()){
            const auto key="kokoro_paused_course:"+access.route_id+":"+access.audio_courses[static_cast<std::size_t>(id-audio_course_first)].first;
            access.update([&](auto& data){data.preferences["lesson_narration_settings"][key]=!checked;});if(access.changed)access.changed();return;
        }
        access.update([&](state::StateData& data){auto& speech=data.preferences["lesson_narration_settings"];
            static constexpr const char* ui_tags[]{"es","en","fr"};
            switch(id){
            case ui_language:apply_application_language(data,selection<=0?"auto":ui_tags[std::clamp(selection-1,0,2)]);break;
            case enabled:speech["enabled"]=checked;break;
            case rate:speech["rate"]=std::array{.5,1.,1.5,2.}[std::clamp(selection,0,3)];break;
            case controls:speech["controls_visible"]=checked;break;
            case advance:speech["kokoro_advance_generation"]=checked;break;
            case auto_lesson:flow_draft.between_lessons=checked;curriculum::save_global_flow(data,flow_draft);break;
            case auto_unit:flow_draft.between_units=checked;curriculum::save_global_flow(data,flow_draft);break;
            case auto_course:flow_draft.between_courses=checked;curriculum::save_global_flow(data,flow_draft);break;
            case swipe_finish:flow_draft.swipe_finish=checked;curriculum::save_global_flow(data,flow_draft);break;
            case swipe_navigation:data.preferences["app_settings"]["swipeNavigationEnabled"]=checked;break;
            case code_wrap:if(!access.course_id.empty())appearance::save_course_wrap(data,access.course_id,checked);else appearance::save_global_wrap(data,checked,access.programming_course_ids);wrap_draft=original_wrap=checked;break;
            default:break;
            }});
        if(id==enabled)refresh_narration_visibility();
        layout();
        if(access.changed)access.changed();
        if(id==ui_language)PostMessageW(window,refresh_language_message,0,0);
    }
};
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<Settings*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Settings*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);
    if(message==WM_CTLCOLORSTATIC&&reinterpret_cast<HWND>(lp)==self->eyebrow_label&&self->eyebrow_label){SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);SetTextColor(reinterpret_cast<HDC>(wp),reader_theme::accent_ink(window,reader_theme::background(window)));return reinterpret_cast<LRESULT>(reader_theme::brush(reader_theme::Surface::screen));}
    LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
    try{switch(message){
    case WM_CREATE:reader_theme::apply(window,reader_theme::Surface::screen);self->build();return 0;
    case WM_SIZE:self->layout();return 0;
    case WM_DPICHANGED_AFTERPARENT:self->refresh_fonts();return 0;
    case WM_ERASEBKGND:self->paint_background(reinterpret_cast<HDC>(wp));return 1;
    case WM_PRINTCLIENT:self->paint_background(reinterpret_cast<HDC>(wp));return 0;
    case WM_COMMAND:self->change(LOWORD(wp),HIWORD(wp));return 0;
    case close_message:if(self->appearance_dirty()&&self->access.unsaved_discarded)self->access.unsaved_discarded(self->tr("Saliste sin guardar los cambios de color.","You left without saving the colour changes.","Vous avez quitté sans enregistrer les modifications de couleur."));if(self->access.close)self->access.close();return 0;
    case refresh_language_message:self->refresh_language();return 0;
    case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(separator_id(static_cast<int>(item.CtlID))){const auto color=reader_theme::palette(window);const auto fill=CreateSolidBrush(color.high_contrast?color.outline:item.CtlID==kokoro_separator?RGB(76,79,87):RGB(48,50,58));FillRect(item.hDC,&item.rcItem,fill);DeleteObject(fill);return TRUE;}
        if(item.CtlID>=global_color&&item.CtlID<=math_color){
            const auto palette=reader_theme::palette(window);FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::catalog_card));
            const int index=item.CtlID-global_color;const auto color=self->colors[index];const auto fill=CreateSolidBrush(palette.high_contrast?palette.card:color);
            const auto old_brush=SelectObject(item.hDC,fill),old_pen=SelectObject(item.hDC,GetStockObject(NULL_PEN));RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,self->px(28),self->px(28));
            SelectObject(item.hDC,old_pen);SelectObject(item.hDC,old_brush);DeleteObject(fill);
            auto text=item.rcItem;text.left+=self->px(12);text.right-=self->px(12);text.top+=self->px(12);text.bottom=text.top+self->px(28);
            const auto previous=SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));SetTextColor(item.hDC,palette.high_contrast?palette.text:RGB(255,255,255));SetBkMode(item.hDC,TRANSPARENT);
            wchar_t caption[180]{};GetWindowTextW(item.hwndItem,caption,180);DrawTextW(item.hDC,caption,-1,&text,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            text.top=text.bottom+self->px(4);text.bottom=item.rcItem.bottom-self->px(10);
            const auto secondary_font=reinterpret_cast<HFONT>(SendDlgItemMessageW(window,rgb_default_heading_first+index,WM_GETFONT,0,0));if(secondary_font)SelectObject(item.hDC,secondary_font);
            SetTextColor(item.hDC,palette.high_contrast?palette.text:RGB(MulDiv(GetRValue(color),96,100),MulDiv(GetGValue(color),96,100),MulDiv(GetBValue(color),96,100)));
            DrawTextW(item.hDC,wide(self->tr("Color secundario","Secondary Color","Couleur secondaire")).c_str(),-1,&text,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(item.hDC,previous);
            return TRUE;
        }
        if((item.CtlID==back||item.CtlID==reset)&&!reader_theme::palette(window).high_contrast){
            const auto brush=CreateSolidBrush(reader_theme::palette(window).accent);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
            auto bounds=item.rcItem;
            if(item.CtlID==back){InflateRect(&bounds,-self->px(18),-self->px(8));draw_icon(item.hDC,bounds,Icon::back,reader_theme::accent_foreground(window));}
            else{wchar_t text[200]{};GetWindowTextW(item.hwndItem,text,200);const auto old=SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));SetTextColor(item.hDC,reader_theme::accent_foreground(window));SetBkMode(item.hDC,TRANSPARENT);
                InflateRect(&bounds,-self->px(14),-self->px(8));auto measured=bounds;
                DrawTextW(item.hDC,text,-1,&measured,DT_CENTER|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
                bounds.top+=std::max<LONG>(0,(bounds.bottom-bounds.top-(measured.bottom-measured.top))/2);
                DrawTextW(item.hDC,text,-1,&bounds,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);SelectObject(item.hDC,old);}
            return TRUE;
        }
        if(item.CtlID==back){FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::catalog_card));auto bounds=item.rcItem;InflateRect(&bounds,-self->px(18),-self->px(8));draw_icon(item.hDC,bounds,Icon::back,reader_theme::accent_ink(window,reader_theme::palette(window).card));return TRUE;}return reader_theme::draw_button(item);}
    case WM_MOUSEWHEEL:self->scroll-=GET_WHEEL_DELTA_WPARAM(wp)*self->px(48)/WHEEL_DELTA;self->layout(false);return 0;
    case WM_HSCROLL:if(lp&&!IsWindowEnabled(reinterpret_cast<HWND>(lp)))return 0;
    if(lp&&!self->binding){const auto slider=reinterpret_cast<HWND>(lp);const int id=GetDlgCtrlID(slider);
        if(id>=rgb_slider_first&&id<rgb_slider_first+9){const int offset=id-rgb_slider_first,index=offset/3,channel=offset%3,value=static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0));
            self->colors[index]=rgb_with_channel(self->colors[index],channel,value);self->color_inputs[index][channel]=std::to_string(value);self->refresh_appearance();return 0;}}
    if(lp&&!self->binding&&reinterpret_cast<HWND>(lp)==GetDlgItem(window,storage_limit)){
        const auto bytes=static_cast<std::uint64_t>(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0))*storage_step;
        self->access.update([&](auto& data){data.preferences["lesson_narration_settings"]["offline_audio_budget_bytes"]=static_cast<std::int64_t>(bytes);});
        if(self->access.storage_snapshot)self->storage=self->access.storage_snapshot();
        self->refresh_storage_detail(bytes);self->layout();
        if(self->access.changed)self->access.changed();return 0;}
    if(lp&&!self->binding&&reinterpret_cast<HWND>(lp)==GetDlgItem(window,text_scale)){
        self->scale_draft=self->original_scale=std::clamp(static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0)),75,150);self->access.update([&](auto& data){if(self->access.scope==SettingsScope::lesson)appearance::save_course_text_scale(data,self->access.course_id,self->inherits_scale,self->scale_draft);else appearance::save_global_text_scale(data,self->scale_draft);});self->refresh_appearance();if(self->access.changed)self->access.changed();return 0;}break;
    case WM_VSCROLL:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(window,SB_VERT,&info);switch(LOWORD(wp)){case SB_LINEUP:self->scroll-=self->px(32);break;case SB_LINEDOWN:self->scroll+=self->px(32);break;case SB_PAGEUP:self->scroll-=self->px(240);break;case SB_PAGEDOWN:self->scroll+=self->px(240);break;case SB_THUMBTRACK:self->scroll=info.nTrackPos;break;default:break;}self->layout(false);return 0;}
    case WM_LBUTTONDOWN:self->drag={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};self->dragging=true;SetCapture(window);return 0;
    case WM_MOUSEMOVE:if(self->dragging&&(wp&MK_LBUTTON)){self->scroll+=self->drag.y-GET_Y_LPARAM(lp);self->drag.y=GET_Y_LPARAM(lp);self->layout(false);}return 0;
    case WM_LBUTTONUP:case WM_CAPTURECHANGED:self->dragging=false;if(GetCapture()==window)ReleaseCapture();return 0;
    case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);delete self;break;
    default:break;}}catch(const std::exception& error){MessageBoxW(window,wide(error.what()).c_str(),L"Opciones",MB_OK|MB_ICONERROR);}
    return DefWindowProcW(window,message,wp,lp);
}
}
HWND create_settings_view(HWND parent,SettingsAccess access){
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};
    if(!InitCommonControlsEx(&common))throw std::runtime_error("Cannot initialize native settings controls");
    const auto instance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent,GWLP_HINSTANCE));
    WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=class_name;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    auto settings=std::make_unique<Settings>(std::move(access));const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,class_name,L"Opciones",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,0,0,parent,nullptr,instance,settings.get());
    if(!window)throw std::runtime_error("No se pudo abrir las opciones.");settings.release();return window;
}

namespace {
constexpr wchar_t frame_class[]=L"LearnGameAndEngineDev.SettingsWindow";
constexpr wchar_t attempts_class[]=L"LearnGameAndEngineDev.AttemptDetails";
constexpr wchar_t attempts_property[]=L"LGAED.AttemptDetails";
struct LanguageDialog {
    HWND window=nullptr,viewport=nullptr;HFONT font=nullptr;std::string locale,selected,title;std::optional<std::string> result;std::exception_ptr error;
    std::vector<std::string> labels,tags;std::vector<HWND> choices;int scroll=0;
    ~LanguageDialog(){if(font)DeleteObject(font);}
    std::string tr(const char* es,const char* en,const char* fr)const{return locale=="fr"?fr:locale=="en"?en:es;}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
    void layout_choices(){RECT area{};GetClientRect(viewport,&area);const int extent=px(56)*static_cast<int>(choices.size());scroll=std::clamp(scroll,0,std::max(0,extent-static_cast<int>(area.bottom)));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,extent-1),static_cast<UINT>(area.bottom),scroll};SetScrollInfo(viewport,SB_VERT,&info,TRUE);
        GetClientRect(viewport,&area);for(std::size_t index=0;index<choices.size();++index)MoveWindow(choices[index],0,px(56)*static_cast<int>(index)-scroll,area.right,px(56),TRUE);
        RedrawWindow(viewport,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);}
    static LRESULT CALLBACK viewport_proc(HWND target,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
        auto* self=reinterpret_cast<LanguageDialog*>(raw);LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        if(message==WM_COMMAND){const auto index=static_cast<int>(LOWORD(wp))-8350;
            if(HIWORD(wp)==BN_SETFOCUS&&index>=0&&static_cast<std::size_t>(index)<self->choices.size()){RECT area{};GetClientRect(target,&area);const int top=self->px(56)*index;
                if(top<self->scroll)self->scroll=top;else if(top+self->px(56)>self->scroll+area.bottom)self->scroll=top+self->px(56)-area.bottom;self->layout_choices();}
            return SendMessageW(self->window,message,wp,lp);}
        if(message==WM_MOUSEWHEEL){self->scroll-=GET_WHEEL_DELTA_WPARAM(wp)*self->px(56)/WHEEL_DELTA;self->layout_choices();return 0;}
        if(message==WM_VSCROLL){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(target,SB_VERT,&info);switch(LOWORD(wp)){
            case SB_LINEUP:self->scroll-=self->px(28);break;case SB_LINEDOWN:self->scroll+=self->px(28);break;
            case SB_PAGEUP:self->scroll-=self->px(224);break;case SB_PAGEDOWN:self->scroll+=self->px(224);break;case SB_THUMBTRACK:self->scroll=info.nTrackPos;break;default:break;}self->layout_choices();return 0;}
        if(message==WM_ERASEBKGND){RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::catalog_card));return 1;}
        if(message==WM_DRAWITEM)return reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        if(message==WM_NCDESTROY)RemoveWindowSubclass(target,viewport_proc,0x4C474F50);
        return DefSubclassProc(target,message,wp,lp);
    }
    void initialize(HWND target){
        window=target;reader_theme::apply(window,reader_theme::Surface::catalog_card);
        if(labels.empty()){labels={tr("Automático (idioma del dispositivo)","Automatic (device language)","Automatique (langue de l’appareil)"),"Español","English","Français"};tags={"auto","es","en","fr"};}
        if(labels.size()!=tags.size()||labels.empty())throw std::invalid_argument("Invalid settings choices");
        SetWindowTextW(window,wide(title.empty()?tr("Idioma de la aplicación","App Language","Langue de l’application"):title).c_str());
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(GetWindow(window,GW_OWNER),MONITOR_DEFAULTTONEAREST),&monitor);
        const int visible_height=std::min(px(56)*static_cast<int>(labels.size()),std::max(px(56),std::min(px(420),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-px(160))));
        RECT bounds{0,0,std::min(px(440),std::max(px(200),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-px(32))),visible_height+px(90)};const auto style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),ex=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE));
        AdjustWindowRectExForDpi(&bounds,style,FALSE,ex,GetDpiForWindow(window));RECT parent{};GetWindowRect(GetWindow(window,GW_OWNER),&parent);
        SetWindowPos(window,nullptr,(parent.left+parent.right-(bounds.right-bounds.left))/2,(parent.top+parent.bottom-(bounds.bottom-bounds.top))/2,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE);
        font=CreateFontW(-px(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(!font)throw std::runtime_error("Cannot create the application language chooser font");
        RECT area{};GetClientRect(window,&area);
        viewport=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,px(24),px(16),area.right-px(48),visible_height,window,reinterpret_cast<HMENU>(8349),GetModuleHandleW(nullptr),nullptr);
        if(!viewport||!SetWindowSubclass(viewport,viewport_proc,0x4C474F50,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot create scrollable settings choices");reader_theme::apply(viewport,reader_theme::Surface::catalog_card);
        for(std::size_t index=0;index<labels.size();++index){
            const auto radio=CreateWindowExW(0,L"BUTTON",wide(labels[index]).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON|BS_MULTILINE|BS_NOTIFY|(index==0?WS_GROUP:0),0,px(static_cast<int>(index)*56),area.right-px(48),px(56),viewport,reinterpret_cast<HMENU>(8350+index),GetModuleHandleW(nullptr),nullptr);
            if(!radio)throw std::runtime_error("Cannot create an application language choice");
            // Focusing a native radio can otherwise emit BN_CLICKED and close
            // this immediate-choice dialog without any actual selection.
            SendMessageW(radio,BM_SETDONTCLICK,TRUE,0);
            reader_theme::apply(radio,reader_theme::Surface::catalog_card);SendMessageW(radio,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);SendMessageW(radio,BM_SETCHECK,selected==tags[index]?BST_CHECKED:BST_UNCHECKED,0);
            choices.push_back(radio);
        }
        layout_choices();
        const auto cancel=CreateWindowExW(0,L"BUTTON",wide(tr("Cancelar","Cancel","Annuler")).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_GROUP|BS_PUSHBUTTON,area.right-px(160),visible_height+px(32),px(136),px(48),window,reinterpret_cast<HMENU>(IDCANCEL),GetModuleHandleW(nullptr),nullptr);
        if(!cancel)throw std::runtime_error("Cannot create the language chooser Cancel action");
        reader_theme::apply(cancel,reader_theme::Surface::catalog_card);SendMessageW(cancel,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    }
    static INT_PTR CALLBACK proc(HWND target,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<LanguageDialog*>(GetWindowLongPtrW(target,DWLP_USER));
        if(message==WM_INITDIALOG){self=reinterpret_cast<LanguageDialog*>(lp);SetWindowLongPtrW(target,DWLP_USER,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return FALSE;LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        try{switch(message){
        case WM_INITDIALOG:self->initialize(target);return TRUE;
        case WM_COMMAND:if(LOWORD(wp)==IDCANCEL){EndDialog(target,IDCANCEL);return TRUE;}
            if(LOWORD(wp)>=8350&&static_cast<std::size_t>(LOWORD(wp)-8350)<self->tags.size()&&HIWORD(wp)==BN_CLICKED){self->result=self->tags[LOWORD(wp)-8350];EndDialog(target,IDOK);return TRUE;}break;
        case WM_CLOSE:EndDialog(target,IDCANCEL);return TRUE;
        case WM_DRAWITEM:return reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        case WM_ERASEBKGND:{RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::catalog_card));return TRUE;}
        default:break;}}catch(...){self->error=std::current_exception();EndDialog(target,IDCANCEL);}return FALSE;
    }
    std::optional<std::string> show(HWND owner){
        struct alignas(DWORD) Template{DLGTEMPLATE dialog{};WORD menu=0,window_class=0,title=0;} layout;
        static_assert(offsetof(Template,menu)==sizeof(DLGTEMPLATE));
        layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN|DS_MODALFRAME|DS_CENTER;
        layout.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;layout.dialog.cx=310;layout.dialog.cy=220;
        const auto code=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,GetAncestor(owner,GA_ROOT),proc,reinterpret_cast<LPARAM>(this));
        if(error)std::rethrow_exception(error);if(code==-1)throw std::runtime_error("Cannot open the application language chooser");return result;
    }
};
std::optional<std::size_t> choose_settings_option(HWND owner,std::string_view locale,std::string_view title,const std::vector<std::string>& labels,std::size_t selected){
    LanguageDialog dialog;dialog.locale=locale;dialog.title=title;dialog.labels=labels;dialog.selected=std::to_string(selected);
    for(std::size_t index=0;index<labels.size();++index)dialog.tags.push_back(std::to_string(index));
    if(const auto choice=dialog.show(owner))return static_cast<std::size_t>(std::stoul(*choice));return {};
}
struct SettingsFrame {HWND child=nullptr;SettingsAccess access;bool owns=false;};
LRESULT CALLBACK frame_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* frame=reinterpret_cast<SettingsFrame*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){frame=static_cast<SettingsFrame*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(frame));}
    if(!frame)return DefWindowProcW(window,message,wp,lp);
    switch(message){
    case WM_CREATE:try{frame->child=create_settings_view(window,std::move(frame->access));return 0;}catch(...){return -1;}
    case WM_SIZE:{RECT bounds{};GetClientRect(window,&bounds);if(frame->child)MoveWindow(frame->child,0,0,bounds.right,bounds.bottom,TRUE);return 0;}
    case WM_SETFOCUS:if(frame->child)SetFocus(frame->child);return 0;
    case WM_COMMAND:if(LOWORD(wp)==IDCANCEL){SendMessageW(window,WM_CLOSE,0,0);return 0;}break;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);return 0;}break;
    case WM_CLOSE:if(frame->child)SendMessageW(frame->child,close_message,0,0);return 0;
    case WM_DPICHANGED:{const auto bounds=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    case WM_GETMINMAXINFO:{const auto dpi=GetDpiForWindow(window);reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={MulDiv(360,dpi,96),MulDiv(320,dpi,96)};return 0;}
    case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);if(frame->owns)delete frame;break;
    default:break;
    }
    return DefWindowProcW(window,message,wp,lp);
}
std::int64_t now_millis(){FILETIME time{};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER ticks{};ticks.LowPart=time.dwLowDateTime;ticks.HighPart=time.dwHighDateTime;return static_cast<std::int64_t>(ticks.QuadPart/10000-11644473600000ULL);}
struct AttemptDetails {HWND lesson=nullptr;std::function<state::StateData()> read;std::string route;int offset=0;POINT drag{};bool dragging=false;};
void position_attempt_details(HWND window,HWND lesson){
    RECT bounds{};GetClientRect(lesson,&bounds);RECT anchor{};const auto header_end=GetDlgItem(lesson,106);
    GetWindowRect(header_end?header_end:lesson,&anchor);MapWindowPoints(nullptr,lesson,reinterpret_cast<POINT*>(&anchor),2);
    const auto dpi=GetDpiForWindow(lesson);SetWindowPos(window,HWND_TOP,0,header_end?anchor.bottom:MulDiv(80,dpi,96),bounds.right,MulDiv(43,dpi,96),SWP_NOACTIVATE);
}
LRESULT CALLBACK attempt_owner_subclass(HWND lesson,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    const auto popup=static_cast<HWND>(GetPropW(lesson,attempts_property));
    if(message==WM_SIZE&&popup)position_attempt_details(popup,lesson);
    if(message==WM_NCDESTROY){RemoveWindowSubclass(lesson,attempt_owner_subclass,id);RemovePropW(lesson,attempts_property);}
    return DefSubclassProc(lesson,message,wp,lp);
}
LRESULT CALLBACK attempt_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* details=reinterpret_cast<AttemptDetails*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){details=static_cast<AttemptDetails*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(details));}
    if(!details)return DefWindowProcW(window,message,wp,lp);
    if(message==WM_CREATE){reader_theme::apply(window,reader_theme::Surface::inset);SetTimer(window,1,1000,nullptr);SetTimer(window,2,5000,nullptr);return 0;}
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_TIMER){if(wp==2){DestroyWindow(window);return 0;}InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_LBUTTONDOWN){SetTimer(window,2,5000,nullptr);details->drag={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};details->dragging=true;SetCapture(window);return 0;}
    if(message==WM_MOUSEMOVE&&details->dragging&&(wp&MK_LBUTTON)){details->offset+=details->drag.x-GET_X_LPARAM(lp);details->drag.x=GET_X_LPARAM(lp);InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_LBUTTONUP||message==WM_CAPTURECHANGED){details->dragging=false;if(GetCapture()==window)ReleaseCapture();return 0;}
    if(message==WM_MOUSEWHEEL){details->offset-=GET_WHEEL_DELTA_WPARAM(wp)/3;InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){
        PAINTSTRUCT paint{};const auto dc=BeginPaint(window,&paint);RECT area{};GetClientRect(window,&area);
        const auto memory=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,area.right),std::max(1L,area.bottom));const auto old=SelectObject(memory,bitmap);
        FillRect(memory,&area,reader_theme::brush(reader_theme::Surface::inset));
        const auto data=details->read();const auto current=attempts::balance(data,details->route,now_millis());
        const auto width=MulDiv(43,GetDpiForWindow(window),96);details->offset=std::clamp(details->offset,0,std::max(0,current.maximum*width-static_cast<int>(area.right)));
        const auto start=std::max(0L,(area.right-current.maximum*width)/2)-details->offset;
        const auto tint=reader_theme::appearance_accent(data,details->route);
        const auto icon=details->route=="math_physics"?Icon::attempt_math:Icon::terminal;
        for(int index=0;index<current.maximum;++index){RECT slot{start+index*width,0,start+(index+1)*width,area.bottom};
            const auto inset=std::max(1,width/12);InflateRect(&slot,-inset,-inset);draw_icon(memory,slot,icon,RGB(255,255,255));
            const auto saved=SaveDC(memory);const auto progress=std::clamp(current.reserve-index,0.,1.);
            IntersectClipRect(memory,slot.left,slot.top,slot.left+static_cast<int>((slot.right-slot.left)*progress),slot.bottom);draw_icon(memory,slot,icon,tint);RestoreDC(memory,saved);
        }
        BitBlt(dc,0,0,area.right,area.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(window,&paint);return 0;
    }
    if(message==WM_NCDESTROY){KillTimer(window,1);KillTimer(window,2);if(IsWindow(details->lesson))RemovePropW(details->lesson,attempts_property);SetWindowLongPtrW(window,GWLP_USERDATA,0);delete details;}
    return DefWindowProcW(window,message,wp,lp);
}
}
SettingsResetText settings_reset_text(SettingsScope scope,std::string_view locale,std::string_view route){
    const auto tr=[&](const char* es,const char* en,const char* fr){return std::string(locale=="fr"?fr:locale=="en"?en:es);};
    if(scope==SettingsScope::global){
        if(route=="programming")return {
            tr("Reiniciar progreso de Programación","Reset Programming Progress","Réinitialiser la progression en programmation"),
            tr("Se eliminarán el progreso, las respuestas y las páginas guardadas de todos los cursos de la ruta de Programación.\n\nLos ajustes generales y las configuraciones particulares de los cursos se conservarán.\n\nEsta acción no se puede deshacer.","Saved progress, answers, and pages from all courses in the Programming learning path will be deleted.\n\nGeneral settings and individual course configurations will be preserved.\n\nThis action cannot be undone.","La progression, les réponses et les pages enregistrées de tous les cours du parcours Programmation seront supprimées.\n\nLes paramètres généraux et les configurations particulières des cours seront conservés.\n\nCette action est irréversible."),
            tr("Progreso de Programación reiniciado","Programming Progress Reset","Progression en programmation réinitialisée")};
        if(route=="math_physics")return {
            tr("Reiniciar progreso de Matemáticas y Física","Reset Mathematics and Physics progress","Réinitialiser la progression en mathématiques et en physique"),
            tr("Se eliminarán el progreso, las respuestas y las páginas guardadas de todos los cursos de la ruta de Matemáticas y Física.\n\nLos ajustes generales y las configuraciones particulares de los cursos se conservarán.\n\nEsta acción no se puede deshacer.","Progress, answers, and saved pages for all courses in the Mathematics and Physics route will be deleted.\n\nGeneral settings and course-specific configurations will be preserved.\n\nThis action cannot be undone.","La progression, les réponses et les pages enregistrées de tous les cours du parcours de mathématiques et de physique seront supprimées.\n\nLes réglages généraux et les configurations propres aux cours seront conservés.\n\nCette action est irréversible."),
            tr("Progreso de Matemáticas y Física reiniciado","Mathematics and Physics progress reset","Progression en mathématiques et en physique réinitialisée")};
    }
    if(scope==SettingsScope::course)return {tr("Reiniciar curso","Reset course","Réinitialiser le cours"),tr("Se eliminarán el progreso y las respuestas de todas las unidades y lecciones de este curso.\n\nEsta acción no se puede deshacer.","Progress and answers from all units and lessons in this course will be deleted.\n\nThis action cannot be undone.","La progression et les réponses de toutes les unités et leçons de ce cours seront supprimées.\n\nCette action est irréversible."),tr("Curso reiniciado","Course Reset","Cours réinitialisé")};
    if(scope==SettingsScope::unit)return {tr("Reiniciar unidad","Reset unit","Réinitialiser l’unité"),tr("Se eliminarán el progreso y las respuestas de todas las lecciones de esta unidad.\n\nEsta acción no se puede deshacer.","Progress and answers from all lessons in this unit will be deleted.\n\nThis action cannot be undone.","La progression et les réponses de toutes les leçons de cette unité seront supprimées.\n\nCette action est irréversible."),tr("Unidad reiniciada","Unit Reset","Unité réinitialisée")};
    if(scope==SettingsScope::lesson)return {tr("Reiniciar lección","Reset lesson","Réinitialiser la leçon"),tr("Se eliminarán todas las respuestas, los ejercicios completados, la página guardada y el progreso de esta lección.\n\nEsta acción no se puede deshacer.","All answers, completed exercises, the saved page, and progress for this lesson will be deleted.\n\nThis action cannot be undone.","Toutes les réponses, les exercices terminés, la page enregistrée et la progression de cette leçon seront supprimés.\n\nCette action est irréversible."),tr("Lección reiniciada","Lesson Reset","Leçon réinitialisée")};
    throw std::invalid_argument("Unknown progress reset scope");
}
HWND create_settings_window(HWND owner,SettingsAccess access){
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=frame_proc;cls.hInstance=instance;cls.lpszClassName=frame_class;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    auto frame=std::make_unique<SettingsFrame>();const auto locale=interface_language(access.read());
    const auto caption=access.title.empty()?(locale=="es"?L"Opciones":locale=="fr"?L"Paramètres":L"Settings"):wide(access.title);
    frame->access=std::move(access);RECT owner_bounds{},work{};GetWindowRect(owner,&owner_bounds);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);work=monitor.rcWork;
    const auto dpi=GetDpiForWindow(owner);const auto width=std::min<LONG>(MulDiv(920,dpi,96),work.right-work.left),height=std::min<LONG>(MulDiv(900,dpi,96),work.bottom-work.top);
    const auto left=std::clamp<LONG>((owner_bounds.left+owner_bounds.right-width)/2,work.left,work.right-width),top=std::clamp<LONG>((owner_bounds.top+owner_bounds.bottom-height)/2,work.top,work.bottom-height);
    const auto result=CreateWindowExW(WS_EX_CONTROLPARENT,frame_class,caption.c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,left,top,width,height,owner,nullptr,instance,frame.get());
    if(!result)throw std::runtime_error("No se pudo abrir la ventana de opciones.");frame->owns=true;frame.release();ShowWindow(result,SW_SHOW);SetForegroundWindow(result);return result;
}
void refresh_settings_language(HWND settings_window){
    const auto view=FindWindowExW(settings_window,nullptr,class_name,nullptr);
    PostMessageW(view?view:settings_window,refresh_language_message,0,0);
}
void toggle_attempt_details(HWND lesson,std::function<state::StateData()> read,std::string_view route){
    if(const auto popup=static_cast<HWND>(GetPropW(lesson,attempts_property))){DestroyWindow(popup);return;}
    if(attempts::premium(read()))return;
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=attempt_proc;cls.hInstance=instance;cls.lpszClassName=attempts_class;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    auto details=std::make_unique<AttemptDetails>();details->lesson=lesson;details->read=std::move(read);details->route=route;
    const auto popup=CreateWindowExW(WS_EX_NOACTIVATE,attempts_class,L"",WS_CHILD|WS_VISIBLE,0,0,0,0,lesson,nullptr,instance,details.get());
    if(!popup)throw std::runtime_error("Cannot open the attempt reserve details");details.release();SetPropW(lesson,attempts_property,popup);SetWindowSubclass(lesson,attempt_owner_subclass,0x4C474154,0);position_attempt_details(popup,lesson);
}
std::optional<std::string> choose_interface_language(HWND owner,const state::StateData& data){
    LanguageDialog dialog;dialog.locale=interface_language(data);dialog.selected=state::string_value(data,"native_windows","ui_language","auto");
    if(dialog.selected!="es"&&dialog.selected!="en"&&dialog.selected!="fr")dialog.selected="auto";
    return dialog.show(owner);
}
void show_settings_notice(HWND owner,std::string_view text){
    if(!IsWindow(owner)||text.empty())return;
    const auto dpi=GetDpiForWindow(owner);RECT parent{};GetWindowRect(owner,&parent);
    const int width=std::min<int>(MulDiv(600,dpi,96),std::max(160L,parent.right-parent.left-MulDiv(32,dpi,96)));
    const int height=MulDiv(78,dpi,96);
    const auto popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",wide(text).c_str(),WS_POPUP|SS_CENTER|SS_CENTERIMAGE,
        parent.left+(parent.right-parent.left-width)/2,parent.bottom-height-MulDiv(32,dpi,96),width,height,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!popup)return;reader_theme::apply(popup,reader_theme::Surface::catalog_card);
    const auto font=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
    if(!font){DestroyWindow(popup);return;}
    if(!SetWindowSubclass(popup,[](HWND target,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw)->LRESULT{
        if(message==WM_TIMER){DestroyWindow(target);return 0;}
        if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(message==WM_NCDESTROY){KillTimer(target,1);DeleteObject(reinterpret_cast<HFONT>(raw));}
        return DefSubclassProc(target,message,wp,lp);
    },0x4C47534E,reinterpret_cast<DWORD_PTR>(font))){DeleteObject(font);DestroyWindow(popup);return;}
    SendMessageW(popup,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    SetTimer(popup,1,3000,nullptr);
    SetWindowPos(popup,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
}
