#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/editing_bar.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include "core/learning_flow.hpp"
#include "core/view_preferences.hpp"
#include "presentation/windows/ui_language.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <commctrl.h>
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <cstdio>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace lgaed::presentation {
namespace {
constexpr wchar_t class_name[]=L"LearnGameAndEngineDev.SettingsView";
constexpr UINT close_message=WM_APP+222;
enum {back=5100,ui_language,content_language,enabled,engine,language,voice,rate,advance,controls,
    auto_lesson,auto_unit,auto_course,swipe_finish,code_wrap,reset,use_global,save_flow,swipe_navigation};
enum {text_scale=5120,text_preview,text_inherit,save_appearance,wrap_overwrite,global_color,programming_color,math_color,
    programming_color_overwrite,math_color_overwrite,global_color_reset,programming_color_reset,math_color_reset,
    reset_wrap_exceptions,reset_programming,reset_math,edit_shortcuts,reset_math_wrap_exceptions};
constexpr UINT_PTR combo_size_subclass_id=0x4C475348;
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
        reader_theme::apply(child,wcscmp(cls,L"EDIT")==0?reader_theme::Surface::field:reader_theme::Surface::canvas);
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
        window=target;SetWindowTextW(window,wide(tr("Atajos de programación","Programming shortcuts","Raccourcis de programmation")).c_str());
        font=CreateFontW(-MulDiv(12,static_cast<int>(GetDpiForWindow(window)),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!font)throw std::runtime_error("Cannot create the shortcut dialog font");
        const auto help=tr("TAB permanece fijo. Puedes configurar 20 teclas, compartidas por todos los ejercicios de Programación. Deja una posición vacía para ocultarla.","TAB remains fixed. Configure 20 keys shared by all Programming exercises. Leave a slot empty to hide it.","TAB reste fixe. Configurez 20 touches communes à tous les exercices de Programmation. Laissez une position vide pour la masquer.");
        control(window,L"STATIC",help,help_id,SS_LEFT);
        control(window,L"BUTTON",tr("Restaurar valores predeterminados","Restore defaults","Rétablir les valeurs par défaut"),defaults_id,WS_TABSTOP|BS_PUSHBUTTON|BS_MULTILINE);
        viewport=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(viewport_id)),GetModuleHandleW(nullptr),nullptr);
        if(!viewport||!SetWindowSubclass(viewport,viewport_proc,1,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot create the shortcut dialog scroll area");
        reader_theme::apply(viewport,reader_theme::Surface::canvas);
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
        case WM_ERASEBKGND:{RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::canvas));return 1;}
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
        case WM_ERASEBKGND:{RECT bounds{};GetClientRect(target,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::canvas));return TRUE;}
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
struct Row{HWND window;int y,height;bool dropdown=false;int actual_top=0,actual_height=0,actual_width=0;};
struct Settings {
    SettingsAccess access; HWND window=nullptr,eyebrow_label=nullptr;std::vector<Row> rows;std::vector<HFONT> fonts;
    std::string locale="es";int scroll=0,extent=0;bool binding=false;POINT drag{};bool dragging=false;
    std::vector<std::string> voices;
    std::vector<std::string> voice_names;
    curriculum::LearningFlow flow_draft;
    curriculum::LearningFlow original_flow;
    bool flow_uses_global=true;
    int scale_draft=100,original_scale=100;bool inherits_scale=true,original_inherits_scale=true;
    bool wrap_draft=false,original_wrap=false,wrap_allowed=true,original_wrap_allowed=true;
    std::array<COLORREF,3> colors{},original_colors{},default_colors{};
    std::array<bool,2> color_overwrite{true,true},original_color_overwrite{true,true};
    std::array<COLORREF,16> custom_colors{};
    HFONT preview_font=nullptr;
    std::vector<RECT> selector_cards;
    bool laying_out=false;
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
        reader_theme::apply(control,dropdown?reader_theme::Surface::card:reader_theme::Surface::canvas);
        const auto font=CreateFontW(-MulDiv(size,static_cast<int>(GetDpiForWindow(window)),72),0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        fonts.push_back(font);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);rows.push_back({control,y,height,dropdown});return control;
    }
    void label(std::string text,int y,int size=16,bool bold=false,int height=34){item(L"STATIC",std::move(text),0,y,height,SS_LEFT,size,bold);}
    void combo(int id,std::initializer_list<const char*> values,int y,int selection){
        const auto control=item(L"COMBOBOX","",id,y,64,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,18);
        for(auto text:values)SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(text).c_str()));
        SendMessageW(control,CB_SETCURSEL,selection,0);
        if(!SetWindowSubclass(control,combo_size_subclass,combo_size_subclass_id,0))throw std::runtime_error("Cannot preserve native selector dimensions");
        size_combo(control);
    }
    void checkbox(int id,std::string text,int y,bool checked){const auto control=item(L"BUTTON",std::move(text),id,y,40,WS_TABSTOP|BS_AUTOCHECKBOX|BS_MULTILINE,18);
        SendMessageW(control,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);}
    void progress(int& y){
        if(!access.reset_scope)return;
        label(tr("Progreso","Progress","Progression"),y,23,true);y+=50;
        const auto text=access.reset_label.empty()?tr("Reiniciar este contenido…","Reset this content…","Réinitialiser ce contenu…"):access.reset_label;
        const auto button=item(L"BUTTON",text,reset,y,54,WS_TABSTOP|BS_PUSHBUTTON,18,true);reader_theme::primary_button(button);y+=74;
        label(tr("Elimina únicamente el progreso y las respuestas del contenido seleccionado.","Only progress and answers for the selected content will be removed.","Supprime uniquement la progression et les réponses du contenu sélectionné."),y,16,false,60);y+=86;
    }
    void flow_controls(int& y){
        checkbox(auto_lesson,tr("Continuar entre lecciones","Continue between lessons","Continuer entre les leçons"),y,flow_draft.between_lessons);y+=48;
        checkbox(auto_unit,tr("Continuar entre unidades","Continue between units","Continuer entre les unités"),y,flow_draft.between_units);y+=48;
        checkbox(auto_course,tr("Continuar entre cursos","Continue between courses","Continuer entre les cours"),y,flow_draft.between_courses);y+=48;
        checkbox(swipe_finish,tr("Permitir finalizar una lección mediante arrastre","Allow swiping to finish a lesson","Autoriser le balayage pour terminer une leçon"),y,flow_draft.swipe_finish);y+=58;
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
        for(std::size_t index=0;index<colors.size();++index){colors[index]=original_colors[index]=reader_theme::appearance_accent(data,routes[index]);default_colors[index]=default_accent(data,routes[index]);}
        color_overwrite[0]=original_color_overwrite[0]=boolean(data,"appearance_settings","route_allow_global_accent_overwrite_programming",true);
        color_overwrite[1]=original_color_overwrite[1]=boolean(data,"appearance_settings","route_allow_global_accent_overwrite_math_physics",true);
        custom_colors.fill(reader_theme::appearance_accent(data));
    }
    bool appearance_dirty()const{
        if(access.scope==SettingsScope::global)return scale_draft!=original_scale||flow_draft!=original_flow||wrap_draft!=original_wrap||colors!=original_colors||color_overwrite!=original_color_overwrite;
        return access.scope==SettingsScope::lesson&&(scale_draft!=original_scale||inherits_scale!=original_inherits_scale||wrap_allowed!=original_wrap_allowed);
    }
    void refresh_appearance(){
        const auto slider=GetDlgItem(window,text_scale),preview=GetDlgItem(window,text_preview);
        if(slider){EnableWindow(slider,access.scope!=SettingsScope::lesson||!inherits_scale);
            const auto effective=access.scope==SettingsScope::lesson&&inherits_scale?appearance::global_text_scale(access.read()):scale_draft;
            const auto font=CreateFontW(-MulDiv(18*effective,static_cast<int>(GetDpiForWindow(window)),7200),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            if(font){SendMessageW(preview,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(preview_font)DeleteObject(preview_font);preview_font=font;}
            const auto sample=std::to_string(effective)+"% — "+tr("Así se verá el texto de las lecciones.","This is how lesson text will look.","Voici l’apparence du texte des leçons.");
            SetWindowTextW(preview,wide(sample).c_str());
        }
        static constexpr int buttons[]{global_color,programming_color,math_color};
        const std::array<std::string,3> labels{tr("Color global","Global accent","Couleur globale"),tr("Color de Programación","Programming accent","Couleur de Programmation"),tr("Color de Matemáticas y Física","Mathematics and Physics accent","Couleur de Mathématiques et Physique")};
        for(std::size_t index=0;index<colors.size();++index)if(const auto control=GetDlgItem(window,buttons[index])){
            char hex[12]{};std::snprintf(hex,sizeof(hex),"#%02X%02X%02X",GetRValue(colors[index]),GetGValue(colors[index]),GetBValue(colors[index]));
            SetWindowTextW(control,wide(labels[index]+" · "+hex).c_str());InvalidateRect(control,nullptr,FALSE);}
        if(const auto save=GetDlgItem(window,save_appearance))EnableWindow(save,appearance_dirty());
        if(!binding)layout();
    }
    void text_controls(int& y){
        label(tr("Tamaño del texto","Text size","Taille du texte"),y,23,true);y+=46;
        if(access.scope==SettingsScope::lesson){
            label(tr("Se aplica a todas las lecciones de este curso.","Applies to every lesson in this course.","S’applique à toutes les leçons de ce cours."),y,16,false,44);y+=52;
            checkbox(text_inherit,tr("Usar el tamaño global","Use the global text size","Utiliser la taille globale"),y,inherits_scale);y+=50;
        }
        const auto slider=item(TRACKBAR_CLASSW,"",text_scale,y,48,WS_TABSTOP|TBS_HORZ|TBS_NOTICKS);y+=56;
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(75,150));SendMessageW(slider,TBM_SETLINESIZE,0,1);SendMessageW(slider,TBM_SETPAGESIZE,0,5);SendMessageW(slider,TBM_SETPOS,TRUE,scale_draft);
        item(L"STATIC","",text_preview,y,80,SS_LEFT,18);y+=100;
    }
    void color_controls(int& y){
        label(tr("Colores de la aplicación","Application accents","Couleurs de l’application"),y,23,true);y+=48;
        label(tr("Los colores se aplican al guardar; cada ruta puede proteger su elección.","Colours apply when saved; each route can protect its selection.","Les couleurs sont appliquées après enregistrement ; chaque parcours peut protéger son choix."),y,16,false,64);y+=74;
        for(std::size_t index=0;index<colors.size();++index){
            item(L"BUTTON","",global_color+static_cast<int>(index),y,64,WS_TABSTOP|BS_PUSHBUTTON);y+=74;
            item(L"BUTTON",tr("Restablecer color predeterminado","Restore default accent","Rétablir la couleur par défaut"),global_color_reset+static_cast<int>(index),y,48,WS_TABSTOP|BS_PUSHBUTTON);y+=60;
            if(index){checkbox(programming_color_overwrite+static_cast<int>(index)-1,tr("Permitir que un cambio global sobrescriba esta ruta","Allow global changes to overwrite this route","Autoriser les changements globaux à remplacer cette couleur"),y,color_overwrite[index-1]);y+=62;}
        }
    }
    void appearance_save_button(int& y){const auto save=item(L"BUTTON",tr("Guardar ajustes","Save settings","Enregistrer les réglages"),save_appearance,y,54,WS_TABSTOP|BS_PUSHBUTTON,18,true);reader_theme::primary_button(save);EnableWindow(save,FALSE);y+=80;}
    void save_appearance_settings(){
        if(!appearance_dirty())return;
        access.update([&](state::StateData& data){
            if(access.scope==SettingsScope::global){
                if(scale_draft!=original_scale)appearance::save_global_text_scale(data,scale_draft);
                if(flow_draft!=original_flow)curriculum::save_global_flow(data,flow_draft);
                if(wrap_draft!=original_wrap)appearance::save_global_wrap(data,wrap_draft,access.programming_course_ids);
                static constexpr std::string_view routes[]{"programming","math_physics"};
                // Android saves the new protections before propagating the global
                // accent; an explicit route edit in the same Save wins afterwards.
                for(std::size_t index=0;index<color_overwrite.size();++index)
                    if(color_overwrite[index]!=original_color_overwrite[index])data.preferences["appearance_settings"]["route_allow_global_accent_overwrite_"+std::string(routes[index])]=color_overwrite[index];
                if(colors[0]!=original_colors[0]){save_accent(data,"",colors[0]);for(std::size_t index=0;index<color_overwrite.size();++index)if(color_overwrite[index])save_accent(data,routes[index],colors[0]);}
                for(std::size_t index=1;index<colors.size();++index)if(colors[index]!=original_colors[index])save_accent(data,routes[index-1],colors[index]);
            }else if(access.scope==SettingsScope::lesson&&!access.course_id.empty()){
                if(scale_draft!=original_scale||inherits_scale!=original_inherits_scale)appearance::save_course_text_scale(data,access.course_id,inherits_scale,scale_draft);
                if(access.programming&&wrap_allowed!=original_wrap_allowed)appearance::save_wrap_overwrite(data,access.course_id,wrap_allowed);
            }
        });
        load_appearance(access.read());original_flow=flow_draft;
        reader_theme::set_accent(window,reader_theme::appearance_accent(access.read(),access.route_id));
        refresh_appearance();if(access.changed)access.changed();
    }
    void choose_color(std::size_t index){
        CHOOSECOLORW picker{sizeof(picker)};picker.hwndOwner=window;picker.rgbResult=colors[index];picker.lpCustColors=custom_colors.data();picker.Flags=CC_RGBINIT|CC_FULLOPEN;
        if(ChooseColorW(&picker)){colors[index]=picker.rgbResult;refresh_appearance();}
        else if(CommDlgExtendedError())throw std::runtime_error("The Windows colour dialog could not be opened");
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
        const auto index=static_cast<int>(SendDlgItemMessageW(window,language,CB_GETCURSEL,0,0));
        static constexpr const char* tags[]{"es","en-US","en-GB","fr"};
        const bool system=SendDlgItemMessageW(window,engine,CB_GETCURSEL,0,0)==1;
        if(system){voices.clear();voice_names.clear();for(const auto& value:windows::system_voices())if(windows::system_voice_matches(value.language,tags[std::clamp(index,0,3)])){voices.push_back(value.id);voice_names.push_back(value.name);}}
        else {
        voices.clear();
        try{
            const auto bytes=state::read_bytes(std::filesystem::path(build::source_root)/"out/narration/runtime.json",1024*1024);
            const auto config=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
            for(const auto& entry:config.GetNamedArray(L"voices")){
                const auto voice=entry.GetObject();if(voice.GetNamedString(L"language")==winrt::to_hstring(tags[std::clamp(index,0,3)]))voices.push_back(winrt::to_string(voice.GetNamedString(L"id")));
            }
        }catch(...){voices.clear();voice_error=tr("No se pudo cargar el catálogo de voces de Kokoro.","The Kokoro voice catalogue could not be loaded.","Impossible de charger le catalogue de voix Kokoro.");}
        const std::string preferred=index==0?"ef_dora":index==1?"af_heart":index==2?"bf_emma":"ff_siwis";
        if(const auto found=std::ranges::find(voices,preferred);found!=voices.end())std::rotate(voices.begin(),found,found+1);
        voice_names.clear();for(const auto& id:voices){auto label=id.substr(id.find('_')+1);
            if(!label.empty()&&label.front()>='a'&&label.front()<='z')label.front()=static_cast<char>(label.front()-'a'+'A');
            voice_names.push_back(std::move(label));}}
        const auto selected=state::string_value(data,"lesson_narration_settings",std::string(system?"windows_voice:":"kokoro_voice:")+tags[std::clamp(index,0,3)],voices.empty()?"":voices.front());
        const auto control=GetDlgItem(window,voice);SendMessageW(control,CB_RESETCONTENT,0,0);
        for(const auto& name:voice_names)SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(name).c_str()));
        if(voices.empty())SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(voice_error.empty()?tr("No hay voces instaladas para este idioma","No installed voices for this language","Aucune voix installée pour cette langue"):voice_error).c_str()));
        const auto found=std::ranges::find(voices,selected);SendMessageW(control,CB_SETCURSEL,found==voices.end()?0:found-voices.begin(),0);
    }
    void build(){
        binding=true;struct BindingGuard{bool& value;~BindingGuard(){value=false;}} binding_guard{binding};
        const auto data=access.read();locale=interface_language(data);load_appearance(data);
        const auto route=access.route_id.empty()?(access.programming?"programming":access.scope==SettingsScope::course||access.scope==SettingsScope::unit||access.scope==SettingsScope::lesson?"math_physics":""):access.route_id;
        reader_theme::set_accent(window,reader_theme::appearance_accent(data,route));
        const auto code=state::string_value(data,"native_windows","content_language","es");
        auto button=item(L"BUTTON",tr("Volver","Back","Retour"),back,20,44,WS_TABSTOP|BS_OWNERDRAW);reader_theme::primary_button(button);
        const bool scoped=!access.eyebrow.empty();
        if(scoped)eyebrow_label=item(L"STATIC",access.eyebrow,0,94,30,SS_LEFT,16,true);
        label(access.title.empty()?tr("Opciones","Options","Options"):access.title,scoped?132:94,32,true,68);
        label(access.subtitle.empty()?tr("Configuración general de LearnGameAndEngineDev","General LearnGameAndEngineDev settings","Configuration générale de LearnGameAndEngineDev"):access.subtitle,scoped?210:150,18,false,44);
        int y=scoped?298:222;
        if(access.scope==SettingsScope::unit){progress(y);extent=y+20;binding=false;layout();return;}
        flow_draft=original_flow=curriculum::learning_flow(data,access.scope==SettingsScope::course?access.course_id:std::string{});
        if(access.scope==SettingsScope::course){
            flow_uses_global=!curriculum::has_course_flow(data,access.course_id);
            checkbox(use_global,tr("Usar la configuración global","Use global settings","Utiliser la configuration globale"),y,flow_uses_global);y+=62;
            flow_controls(y);
            const auto save=item(L"BUTTON",tr("Guardar ajustes","Save settings","Enregistrer les réglages"),save_flow,y,54,WS_TABSTOP|BS_PUSHBUTTON,18,true);
            reader_theme::primary_button(save);EnableWindow(save,FALSE);y+=100;progress(y);
            extent=y+20;binding=false;layout();return;
        }
        if(access.scope==SettingsScope::global){
        label(tr("Idioma de la aplicación","Application language","Langue de l’application"),y,23,true);y+=44;
        label(tr("Los menús, el contenido y la narración se configuran de forma independiente.","Menus, lesson content and narration can be configured independently.","Les menus, le contenu et la narration se configurent indépendamment."),y,16,false,48);y+=58;
        const auto selected_ui=state::string_value(data,"native_windows","ui_language","auto");
        const auto automatic=tr("Automático (Windows)","Automatic (Windows)","Automatique (Windows)");
        combo(ui_language,{"Español","English","Français",automatic.c_str()},y,selected_ui=="en"?1:selected_ui=="fr"?2:selected_ui=="es"?0:3);y+=100;
        label(tr("Idioma del contenido","Lesson content language","Langue du contenu"),y,23,true);y+=44;
        combo(content_language,{"Español","English","Français"},y,code=="en"?1:code=="fr"?2:0);y+=100;
        text_controls(y);
        }
        label(tr("Lectura en voz alta","Read aloud","Lecture à voix haute"),y,23,true);y+=48;
        checkbox(enabled,tr("Activar la lectura de las lecciones","Enable lesson narration","Activer la lecture des leçons"),y,boolean(data,"lesson_narration_settings","enabled",true));y+=52;
        label(tr("Motor de voz","Speech engine","Moteur vocal"),y);y+=34;
        const auto chosen_engine=state::string_value(data,"lesson_narration_settings","engine","KOKORO");
        combo(engine,{"Kokoro", "Windows TTS"},y,chosen_engine=="WINDOWS"||chosen_engine=="ANDROID"?1:0);y+=86;
        label(tr("Idioma de narración","Narration language","Langue de narration"),y);y+=34;
        const auto lang=state::string_value(data,"lesson_narration_settings","language",code=="en"?"en-US":code);
        combo(language,{"Español","English · US","English · GB","Français"},y,lang=="en-US"?1:lang=="en-GB"?2:lang=="fr"?3:0);y+=86;
        label(tr("Voz","Voice","Voix"),y);y+=34;combo(voice,{},y,0);fill_voices(data);y+=86;
        label(tr("Velocidad de lectura","Playback speed","Vitesse de lecture"),y);y+=34;
        const auto stored=state::find(data,"lesson_narration_settings","rate");const auto value=stored&&std::holds_alternative<double>(*stored)?std::get<double>(*stored):1.0;
        combo(rate,{"0.5×","1×","1.5×","2×"},y,value<.75?0:value<1.25?1:value<1.75?2:3);y+=88;
        checkbox(controls,tr("Mostrar la barra flotante del lector","Show floating reading controls","Afficher les commandes de lecture flottantes"),y,boolean(data,"lesson_narration_settings","controls_visible",true));y+=50;
        checkbox(advance,tr("Preparar audio anticipadamente","Prepare upcoming audio","Préparer l’audio à l’avance"),y,boolean(data,"lesson_narration_settings","kokoro_advance_generation",true));y+=94;
        if(access.scope!=SettingsScope::audio){
        if(access.scope==SettingsScope::lesson&&!access.course_id.empty())text_controls(y);
        label(tr("Navegación y código","Navigation and code","Navigation et code"),y,23,true);y+=52;
        if(access.scope==SettingsScope::global)flow_controls(y);
        checkbox(swipe_navigation,tr("Navegar entre páginas mediante arrastre","Swipe between pages","Changer de page par balayage"),y,boolean(data,"app_settings","swipeNavigationEnabled",true));y+=48;
        if(access.scope==SettingsScope::global||access.programming){
            checkbox(code_wrap,tr("Ajustar las líneas de código al ancho disponible","Wrap code lines to the available width","Adapter les lignes de code à la largeur disponible"),y,wrap_draft);y+=60;
            if(access.scope==SettingsScope::lesson&&!access.course_id.empty()){
                checkbox(wrap_overwrite,tr("Permitir que el ajuste global sobrescriba este curso","Allow global wrapping changes to overwrite this course","Autoriser le réglage global à remplacer celui de ce cours"),y,wrap_allowed);y+=62;}
        }
        if(access.scope==SettingsScope::lesson&&access.programming){
            label(tr("Atajos de programación","Programming shortcuts","Raccourcis de programmation"),y,23,true);y+=48;
            item(L"BUTTON",tr("Editar los 20 atajos…","Edit the 20 shortcuts…","Modifier les 20 raccourcis…"),edit_shortcuts,y,60,WS_TABSTOP|BS_PUSHBUTTON,18,true);y+=76;
            label(tr("TAB permanece fijo. Los atajos se comparten entre todos los ejercicios de Programación.","TAB remains fixed. Shortcuts are shared by all Programming exercises.","TAB reste fixe. Les raccourcis sont partagés par tous les exercices de Programmation."),y,16,false,56);y+=72;
        }
        if(access.scope==SettingsScope::global){
            if(!access.programming_course_ids.empty()){
                item(L"BUTTON",tr("Restablecer excepciones de ajuste de línea en Programación","Reset Programming line-wrap exceptions","Réinitialiser les exceptions de retour à la ligne en Programmation"),reset_wrap_exceptions,y,70,WS_TABSTOP|BS_PUSHBUTTON);y+=90;}
            if(!access.math_course_ids.empty()){
                item(L"BUTTON",tr("Restablecer excepciones de ajuste de línea en Matemáticas y Física","Reset Mathematics and Physics line-wrap exceptions","Réinitialiser les exceptions de retour à la ligne en Mathématiques et Physique"),reset_math_wrap_exceptions,y,70,WS_TABSTOP|BS_PUSHBUTTON);y+=90;}
            color_controls(y);appearance_save_button(y);
            if(access.reset_route){
                label(tr("Progreso por ruta","Progress by route","Progression par parcours"),y,23,true);y+=48;
                item(L"BUTTON",tr("Reiniciar progreso de Programación…","Reset Programming progress…","Réinitialiser la progression en Programmation…"),reset_programming,y,60,WS_TABSTOP|BS_PUSHBUTTON,18,true);y+=76;
                item(L"BUTTON",tr("Reiniciar progreso de Matemáticas y Física…","Reset Mathematics and Physics progress…","Réinitialiser la progression en Mathématiques et Physique…"),reset_math,y,60,WS_TABSTOP|BS_PUSHBUTTON,18,true);y+=90;
            }
        }else if(access.scope==SettingsScope::lesson&&!access.course_id.empty())appearance_save_button(y);
        progress(y);}
        extent=y+20;refresh_appearance();binding=false;layout();
    }
    void paint_background(HDC dc)const{
        if(!dc)return;
        RECT area{};GetClientRect(window,&area);FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::canvas));
        const auto saved=SaveDC(dc);const auto palette=reader_theme::palette(window);
        const auto pen=CreatePen(PS_SOLID,std::max(1,px(1)),palette.high_contrast?GetSysColor(COLOR_WINDOWTEXT):palette.outline);
        SelectObject(dc,pen);SelectObject(dc,reader_theme::brush(reader_theme::Surface::card));
        for(const auto& bounds:selector_cards)RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,px(8),px(8));
        RestoreDC(dc,saved);DeleteObject(pen);
    }
    void layout(){
        if(laying_out||!window)return;laying_out=true;struct LayoutGuard{bool& value;~LayoutGuard(){value=false;}} guard{laying_out};
        RECT area{};GetClientRect(window,&area);const auto margin=std::min(px(24),std::max(0,static_cast<int>(area.right)/4));
        const auto width=std::max(1,static_cast<int>(area.right)-2*margin),padding=px(18);
        int cursor=0,authored_end=0;
        for(auto& row:rows){
            const auto id=GetDlgCtrlID(row.window);const int gap=row.y-authored_end;
            row.actual_top=rows.data()==&row?px(row.y):cursor+px(std::max(8,gap));row.actual_width=id==back?px(56):width;
            if(row.dropdown){
                const auto inner_width=std::max(1,width-2*padding);RECT native{};GetWindowRect(row.window,&native);
                const auto selection=static_cast<int>(SendMessageW(row.window,CB_GETITEMHEIGHT,static_cast<WPARAM>(-1),0));
                const auto frame=std::max(0,static_cast<int>(native.bottom-native.top)-selection);
                row.actual_height=combo_selection_height(row.window,inner_width)+frame+2*padding;
            }else{
                wchar_t name[32]{};GetClassNameW(row.window,name,32);
                if(id==back)row.actual_height=px(56);
                else if(_wcsicmp(name,L"BUTTON")==0){
                    const auto type=GetWindowLongPtrW(row.window,GWL_STYLE)&BS_TYPEMASK;
                    const bool check=type==BS_AUTOCHECKBOX||type==BS_CHECKBOX;
                    const auto inset=check?px(40):(id>=global_color&&id<=math_color?px(76):px(28));
                    row.actual_height=std::max(px(check?58:62),measured_text_height(row.window,std::max(1,width-inset))+px(check?12:16));
                }else if(_wcsicmp(name,L"STATIC")==0)row.actual_height=std::max(px(24),measured_text_height(row.window,width)+px(4));
                else row.actual_height=px(row.height);
            }
            cursor=row.actual_top+row.actual_height;authored_end=row.y+row.height;
        }
        const auto content_height=cursor+px(24);scroll=std::clamp(scroll,0,std::max(0,content_height-static_cast<int>(area.bottom)));
        SendMessageW(window,WM_SETREDRAW,FALSE,0);selector_cards.clear();
        for(const auto& row:rows){const auto top=row.actual_top-scroll;
            if(row.dropdown){
                selector_cards.push_back({margin,top,margin+width,top+row.actual_height});
                const auto inner_width=std::max(1,width-2*padding),field=combo_selection_height(row.window,inner_width);
                MoveWindow(row.window,margin+padding,top+padding,inner_width,field+px(210),FALSE);size_combo(row.window);
            }else MoveWindow(row.window,margin,top,row.actual_width,row.actual_height,FALSE);
        }
        SendMessageW(window,WM_SETREDRAW,TRUE,0);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,content_height-1),static_cast<UINT>(area.bottom),scroll,0};SetScrollInfo(window,SB_VERT,&info,TRUE);reader_theme::refresh_scrollbars(window);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        RECT adjusted{};GetClientRect(window,&adjusted);if(adjusted.right!=area.right)PostMessageW(window,WM_SIZE,0,MAKELPARAM(adjusted.right,adjusted.bottom));
    }
    void change(int id,int notification){if(id==back){PostMessageW(window,close_message,0,0);return;}if(binding)return;if(id==reset){if(access.reset_scope)access.reset_scope();return;}
        if((id==reset_programming||id==reset_math)&&access.reset_route){access.reset_route(id==reset_programming?"programming":"math_physics");return;}
        if(notification!=CBN_SELCHANGE&&notification!=BN_CLICKED)return;
        const auto selection=static_cast<int>(SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0));
        const bool checked=SendDlgItemMessageW(window,id,BM_GETCHECK,0,0)==BST_CHECKED;
        if(id==save_appearance){save_appearance_settings();return;}
        if(id==edit_shortcuts){show_shortcuts();return;}
        if(id>=global_color&&id<=math_color){choose_color(static_cast<std::size_t>(id-global_color));return;}
        if(id>=global_color_reset&&id<=math_color_reset){const auto index=static_cast<std::size_t>(id-global_color_reset);colors[index]=default_colors[index];refresh_appearance();return;}
        if(id==text_inherit){inherits_scale=checked;refresh_appearance();return;}
        if(id==wrap_overwrite){wrap_allowed=checked;refresh_appearance();return;}
        if(id==programming_color_overwrite||id==math_color_overwrite){color_overwrite[id-programming_color_overwrite]=checked;refresh_appearance();return;}
        if(id==reset_wrap_exceptions||id==reset_math_wrap_exceptions){
            const auto& courses=id==reset_wrap_exceptions?access.programming_course_ids:access.math_course_ids;
            access.update([&](state::StateData& data){appearance::reset_wrap_exceptions(data,courses);});
            if(access.changed)access.changed();return;
        }
        if(access.scope==SettingsScope::global&&(id==auto_lesson||id==auto_unit||id==auto_course||id==swipe_finish||id==code_wrap)){
            if(id==auto_lesson)flow_draft.between_lessons=checked;if(id==auto_unit)flow_draft.between_units=checked;
            if(id==auto_course)flow_draft.between_courses=checked;if(id==swipe_finish)flow_draft.swipe_finish=checked;
            if(id==code_wrap)wrap_draft=checked;refresh_appearance();return;
        }
        if(access.scope==SettingsScope::course&&(id==use_global||id==save_flow||id==auto_lesson||id==auto_unit||id==auto_course||id==swipe_finish)){
            if(id==save_flow){access.update([&](state::StateData& data){if(flow_uses_global)curriculum::clear_course_flow(data,access.course_id);else curriculum::save_course_flow(data,access.course_id,flow_draft);});EnableWindow(GetDlgItem(window,save_flow),FALSE);if(access.changed)access.changed();return;}
            if(id==use_global){flow_uses_global=checked;for(const auto item:{auto_lesson,auto_unit,auto_course,swipe_finish})EnableWindow(GetDlgItem(window,item),!checked);}
            if(id==auto_lesson)flow_draft.between_lessons=checked;
            if(id==auto_unit)flow_draft.between_units=checked;
            if(id==auto_course)flow_draft.between_courses=checked;
            if(id==swipe_finish)flow_draft.swipe_finish=checked;
            EnableWindow(GetDlgItem(window,save_flow),TRUE);return;
        }
        access.update([&](state::StateData& data){auto& native=data.preferences["native_windows"];auto& speech=data.preferences["lesson_narration_settings"];
            static constexpr const char* ui_tags[]{"es","en","fr"};static constexpr const char* tags[]{"es","en-US","en-GB","fr"};
            switch(id){
            case ui_language:native["ui_language"]=std::string(selection==3?"auto":ui_tags[std::clamp(selection,0,2)]);break;
            case content_language:native["content_language"]=std::string(ui_tags[std::clamp(selection,0,2)]);break;
            case enabled:speech["enabled"]=checked;break;
            case engine:speech["engine"]=std::string(selection==1?"WINDOWS":"KOKORO");break;
            case language:speech["language"]=std::string(tags[std::clamp(selection,0,3)]);break;
            case voice:if(selection>=0&&static_cast<std::size_t>(selection)<voices.size()){const auto lang=state::string_value(data,"lesson_narration_settings","language","es");const bool system=SendDlgItemMessageW(window,engine,CB_GETCURSEL,0,0)==1;speech[std::string(system?"windows_voice:":"kokoro_voice:")+lang]=voices[selection];if(!system)speech["kokoro_voice"]=voices[selection];}break;
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
        if(id==language||id==engine){binding=true;fill_voices(access.read());binding=false;}
        layout();
        if(access.changed)access.changed();
    }
};
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<Settings*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Settings*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);
    if(message==WM_CTLCOLORSTATIC&&reinterpret_cast<HWND>(lp)==self->eyebrow_label&&self->eyebrow_label){SetBkMode(reinterpret_cast<HDC>(wp),TRANSPARENT);SetTextColor(reinterpret_cast<HDC>(wp),reader_theme::accent_ink(window,reader_theme::palette(window).canvas));return reinterpret_cast<LRESULT>(reader_theme::brush(reader_theme::Surface::canvas));}
    LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
    try{switch(message){
    case WM_CREATE:reader_theme::apply(window,reader_theme::Surface::canvas);self->build();return 0;
    case WM_SIZE:self->layout();return 0;
    case WM_ERASEBKGND:self->paint_background(reinterpret_cast<HDC>(wp));return 1;
    case WM_PRINTCLIENT:self->paint_background(reinterpret_cast<HDC>(wp));return 0;
    case WM_COMMAND:self->change(LOWORD(wp),HIWORD(wp));return 0;
    case close_message:if(self->access.close)self->access.close();return 0;
    case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item.CtlID>=global_color&&item.CtlID<=math_color){
            const auto palette=reader_theme::palette(window);FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::card));
            auto swatch=item.rcItem;swatch.left+=self->px(14);swatch.right=swatch.left+self->px(28);swatch.top+=self->px(14);swatch.bottom-=self->px(14);
            const auto fill=CreateSolidBrush(self->colors[item.CtlID-global_color]);FillRect(item.hDC,&swatch,fill);DeleteObject(fill);FrameRect(item.hDC,&swatch,GetSysColorBrush(palette.high_contrast?COLOR_WINDOWTEXT:COLOR_GRAYTEXT));
            auto text=item.rcItem;text.left+=self->px(60);text.right-=self->px(12);text.top+=self->px(8);text.bottom-=self->px(8);
            const auto previous=SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));SetTextColor(item.hDC,palette.text);SetBkMode(item.hDC,TRANSPARENT);
            wchar_t caption[180]{};GetWindowTextW(item.hwndItem,caption,180);DrawTextW(item.hDC,caption,-1,&text,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(item.hDC,previous);
            if(item.itemState&ODS_FOCUS){auto focus=item.rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(item.hDC,&focus);}return TRUE;
        }
        if((item.CtlID==back||item.CtlID==reset)&&self->access.programming&&!reader_theme::palette(window).high_contrast){
            const auto brush=CreateSolidBrush(reader_theme::palette(window).accent);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
            auto bounds=item.rcItem;
            if(item.CtlID==back){InflateRect(&bounds,-self->px(18),-self->px(8));draw_icon(item.hDC,bounds,Icon::back,reader_theme::accent_foreground(window));}
            else{wchar_t text[200]{};GetWindowTextW(item.hwndItem,text,200);const auto old=SelectObject(item.hDC,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));SetTextColor(item.hDC,reader_theme::accent_foreground(window));SetBkMode(item.hDC,TRANSPARENT);
                InflateRect(&bounds,-self->px(14),-self->px(8));auto measured=bounds;
                DrawTextW(item.hDC,text,-1,&measured,DT_CENTER|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
                bounds.top+=std::max<LONG>(0,(bounds.bottom-bounds.top-(measured.bottom-measured.top))/2);
                DrawTextW(item.hDC,text,-1,&bounds,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);SelectObject(item.hDC,old);}
            if(item.itemState&ODS_FOCUS){InflateRect(&bounds,-2,-2);DrawFocusRect(item.hDC,&bounds);}return TRUE;
        }
        if(item.CtlID==back){FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::card));auto bounds=item.rcItem;InflateRect(&bounds,-self->px(18),-self->px(8));draw_icon(item.hDC,bounds,Icon::back,reader_theme::accent_ink(window,reader_theme::palette(window).card));return TRUE;}return reader_theme::draw_button(item);}
    case WM_MOUSEWHEEL:self->scroll-=GET_WHEEL_DELTA_WPARAM(wp)*self->px(48)/WHEEL_DELTA;self->layout();return 0;
    case WM_HSCROLL:if(lp&&!self->binding&&reinterpret_cast<HWND>(lp)==GetDlgItem(window,text_scale)){
        self->scale_draft=std::clamp(static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0)),75,150);self->refresh_appearance();return 0;}break;
    case WM_VSCROLL:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(window,SB_VERT,&info);switch(LOWORD(wp)){case SB_LINEUP:self->scroll-=self->px(32);break;case SB_LINEDOWN:self->scroll+=self->px(32);break;case SB_PAGEUP:self->scroll-=self->px(240);break;case SB_PAGEDOWN:self->scroll+=self->px(240);break;case SB_THUMBTRACK:self->scroll=info.nTrackPos;break;default:break;}self->layout();return 0;}
    case WM_LBUTTONDOWN:self->drag={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};self->dragging=true;SetCapture(window);return 0;
    case WM_MOUSEMOVE:if(self->dragging&&(wp&MK_LBUTTON)){self->scroll+=self->drag.y-GET_Y_LPARAM(lp);self->drag.y=GET_Y_LPARAM(lp);self->layout();}return 0;
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
    auto settings=std::make_unique<Settings>(std::move(access));const auto window=CreateWindowExW(0,class_name,L"Opciones",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,0,0,parent,nullptr,instance,settings.get());
    if(!window)throw std::runtime_error("No se pudo abrir las opciones.");settings.release();return window;
}
}
