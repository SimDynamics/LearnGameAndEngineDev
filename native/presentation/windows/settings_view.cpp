#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/system_speech.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace lgaed::presentation {
namespace {
constexpr wchar_t class_name[]=L"LearnGameAndEngineDev.SettingsView";
constexpr UINT close_message=WM_APP+222;
enum {back=5100,ui_language,content_language,enabled,engine,language,voice,rate,advance,controls,
    auto_lesson,auto_unit,auto_course,swipe_finish,code_wrap,reset};
struct Row{HWND window;int y,height;bool dropdown=false;};
struct Settings {
    SettingsAccess access; HWND window=nullptr;std::vector<Row> rows;std::vector<HFONT> fonts;
    std::string locale="es";int scroll=0,extent=0;bool binding=false;POINT drag{};bool dragging=false;
    std::vector<std::string> voices;
    std::vector<std::string> voice_names;
    explicit Settings(SettingsAccess value):access(std::move(value)){}
    ~Settings(){for(auto font:fonts)DeleteObject(font);}
    int px(int v)const{return MulDiv(v,static_cast<int>(GetDpiForWindow(window)),96);}
    std::string tr(const char* es,const char* en,const char* fr)const{return locale=="en"?en:locale=="fr"?fr:es;}
    HWND item(const wchar_t* cls,std::string text,int id,int y,int height,DWORD style=0,int size=12,bool bold=false){
        const auto instance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));
        const auto control=CreateWindowExW(0,cls,wide(text).c_str(),WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        const bool dropdown=wcscmp(cls,L"COMBOBOX")==0;
        reader_theme::apply(control,dropdown?reader_theme::Surface::card:reader_theme::Surface::canvas);
        const auto font=CreateFontW(-MulDiv(size,static_cast<int>(GetDpiForWindow(window)),72),0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        fonts.push_back(font);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);rows.push_back({control,y,height,dropdown});return control;
    }
    void label(std::string text,int y,int size=12,bool bold=false,int height=34){item(L"STATIC",std::move(text),0,y,height,SS_LEFT,size,bold);}
    void combo(int id,std::initializer_list<const char*> values,int y,int selection){
        const auto control=item(L"COMBOBOX","",id,y,64,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        for(auto text:values)SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(text).c_str()));
        SendMessageW(control,CB_SETCURSEL,selection,0);
        SendMessageW(control,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(52));
        SendMessageW(control,CB_SETITEMHEIGHT,0,px(36));
    }
    void checkbox(int id,std::string text,int y,bool checked){const auto control=item(L"BUTTON",std::move(text),id,y,40,WS_TABSTOP|BS_AUTOCHECKBOX);
        SendMessageW(control,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);}
    static bool boolean(const state::StateData& data,const char* domain,const char* key,bool fallback){const auto value=state::find(data,domain,key);return value&&std::holds_alternative<bool>(*value)?std::get<bool>(*value):fallback;}
    void fill_voices(const state::StateData& data){
        const auto index=static_cast<int>(SendDlgItemMessageW(window,language,CB_GETCURSEL,0,0));
        static constexpr const char* tags[]{"es","en-US","en-GB","fr"};
        const bool system=SendDlgItemMessageW(window,engine,CB_GETCURSEL,0,0)==1;
        if(system){voices.clear();voice_names.clear();for(const auto& value:windows::system_voices())if(windows::system_voice_matches(value.language,tags[std::clamp(index,0,3)])){voices.push_back(value.id);voice_names.push_back(value.name);}}
        else {
        voices=index==0?std::vector<std::string>{"ef_dora","em_alex","em_santa"}:index==1?std::vector<std::string>{"af_heart","af_bella","af_nicole","af_sarah","am_adam","am_michael"}
            :index==2?std::vector<std::string>{"bf_emma","bf_isabella","bm_george","bm_lewis"}:std::vector<std::string>{"ff_siwis"};
        voice_names=voices;}
        const auto selected=state::string_value(data,"lesson_narration_settings",std::string(system?"windows_voice:":"kokoro_voice:")+tags[std::clamp(index,0,3)],voices.empty()?"":voices.front());
        const auto control=GetDlgItem(window,voice);SendMessageW(control,CB_RESETCONTENT,0,0);
        for(const auto& name:voice_names)SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(name).c_str()));
        if(voices.empty())SendMessageW(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(tr("No hay voces instaladas para este idioma","No installed voices for this language","Aucune voix installée pour cette langue")).c_str()));
        const auto found=std::ranges::find(voices,selected);SendMessageW(control,CB_SETCURSEL,found==voices.end()?0:found-voices.begin(),0);
    }
    void build(){
        binding=true;const auto data=access.read();locale=state::string_value(data,"native_windows","ui_language","es");
        const auto code=state::string_value(data,"native_windows","content_language","es");
        auto button=item(L"BUTTON",tr("Volver","Back","Retour"),back,20,44,WS_TABSTOP|BS_OWNERDRAW);reader_theme::primary_button(button);
        label(access.title.empty()?tr("Opciones","Options","Options"):access.title,94,27,true,48);
        label(access.subtitle.empty()?tr("Configuración general de LearnGameAndEngineDev","General LearnGameAndEngineDev settings","Configuration générale de LearnGameAndEngineDev"):access.subtitle,150,13,false,44);
        int y=222;
        label(tr("Idioma de la aplicación","Application language","Langue de l’application"),y,18,true);y+=44;
        label(tr("Los menús, el contenido y la narración se configuran de forma independiente.","Menus, lesson content and narration can be configured independently.","Les menus, le contenu et la narration se configurent indépendamment."),y,12,false,48);y+=58;
        combo(ui_language,{"Español","English","Français"},y,locale=="en"?1:locale=="fr"?2:0);y+=100;
        label(tr("Idioma del contenido","Lesson content language","Langue du contenu"),y,18,true);y+=44;
        combo(content_language,{"Español","English","Français"},y,code=="en"?1:code=="fr"?2:0);y+=100;
        label(tr("Lectura en voz alta","Read aloud","Lecture à voix haute"),y,20,true);y+=48;
        checkbox(enabled,tr("Activar la lectura de las lecciones","Enable lesson narration","Activer la lecture des leçons"),y,boolean(data,"lesson_narration_settings","enabled",true));y+=52;
        label(tr("Motor de voz","Speech engine","Moteur vocal"),y);y+=34;
        combo(engine,{"Kokoro · neuronal","Windows · voces del sistema"},y,state::string_value(data,"lesson_narration_settings","engine","KOKORO")=="WINDOWS"?1:0);y+=86;
        label(tr("Idioma de narración","Narration language","Langue de narration"),y);y+=34;
        const auto lang=state::string_value(data,"lesson_narration_settings","language","es");
        combo(language,{"Español","English · US","English · GB","Français"},y,lang=="en-US"?1:lang=="en-GB"?2:lang=="fr"?3:0);y+=86;
        label(tr("Voz","Voice","Voix"),y);y+=34;combo(voice,{},y,0);fill_voices(data);y+=86;
        label(tr("Velocidad de lectura","Playback speed","Vitesse de lecture"),y);y+=34;
        const auto stored=state::find(data,"lesson_narration_settings","rate");const auto value=stored&&std::holds_alternative<double>(*stored)?std::get<double>(*stored):1.0;
        combo(rate,{"0.5×","1×","1.5×","2×"},y,value<.75?0:value<1.25?1:value<1.75?2:3);y+=88;
        checkbox(controls,tr("Mostrar la barra flotante del lector","Show floating reading controls","Afficher les commandes de lecture flottantes"),y,boolean(data,"lesson_narration_settings","controls_visible",true));y+=50;
        checkbox(advance,tr("Preparar audio anticipadamente","Prepare upcoming audio","Préparer l’audio à l’avance"),y,boolean(data,"lesson_narration_settings","kokoro_advance_generation",true));y+=94;
        label(tr("Navegación y código","Navigation and code","Navigation et code"),y,20,true);y+=52;
        checkbox(auto_lesson,tr("Continuar entre lecciones","Continue between lessons","Continuer entre les leçons"),y,boolean(data,"native_windows","automatic_between_lessons",false));y+=48;
        checkbox(auto_unit,tr("Continuar entre unidades","Continue between units","Continuer entre les unités"),y,boolean(data,"native_windows","automatic_between_units",false));y+=48;
        checkbox(auto_course,tr("Continuar entre cursos","Continue between courses","Continuer entre les cours"),y,boolean(data,"native_windows","automatic_between_courses",false));y+=48;
        checkbox(swipe_finish,tr("Permitir finalizar una lección mediante arrastre","Allow swiping to finish a lesson","Autoriser le balayage pour terminer une leçon"),y,boolean(data,"native_windows","swipe_finish",true));y+=48;
        checkbox(code_wrap,tr("Ajustar las líneas de código al ancho disponible","Wrap code lines to the available width","Adapter les lignes de code à la largeur disponible"),y,boolean(data,"native_windows","code_wrap",false));y+=86;
        if(access.reset_scope){label(tr("Progreso","Progress","Progression"),y,20,true);y+=50;
            const auto reset_button=item(L"BUTTON",tr("Reiniciar este contenido…","Reset this content…","Réinitialiser ce contenu…"),reset,y,54,WS_TABSTOP|BS_PUSHBUTTON);reader_theme::primary_button(reset_button);y+=70;
            label(tr("Elimina únicamente el progreso y las respuestas del contenido seleccionado.","Only progress and answers for the selected content will be removed.","Supprime uniquement la progression et les réponses du contenu sélectionné."),y,11,false,50);y+=78;}
        extent=y+20;binding=false;layout();
    }
    void layout(){RECT area{};GetClientRect(window,&area);const auto margin=px(24);const auto width=std::max(px(100),static_cast<int>(area.right)-2*margin);
        const auto max=std::max(0,px(extent)-static_cast<int>(area.bottom));scroll=std::clamp(scroll,0,max);
        SendMessageW(window,WM_SETREDRAW,FALSE,0);
        for(const auto& row:rows){const auto id=GetDlgCtrlID(row.window);MoveWindow(row.window,margin,px(row.y)-scroll,id==back?px(72):width,px(row.height)+(row.dropdown?px(210):0),FALSE);}
        SendMessageW(window,WM_SETREDRAW,TRUE,0);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,px(extent)-1),static_cast<UINT>(area.bottom),scroll,0};SetScrollInfo(window,SB_VERT,&info,TRUE);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void change(int id,int notification){if(binding)return;if(id==back){PostMessageW(window,close_message,0,0);return;}if(id==reset){if(access.reset_scope)access.reset_scope();return;}
        if(notification!=CBN_SELCHANGE&&notification!=BN_CLICKED)return;
        const auto selection=static_cast<int>(SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0));
        const bool checked=SendDlgItemMessageW(window,id,BM_GETCHECK,0,0)==BST_CHECKED;
        access.update([&](state::StateData& data){auto& native=data.preferences["native_windows"];auto& speech=data.preferences["lesson_narration_settings"];
            static constexpr const char* ui_tags[]{"es","en","fr"};static constexpr const char* tags[]{"es","en-US","en-GB","fr"};
            switch(id){
            case ui_language:native["ui_language"]=std::string(ui_tags[std::clamp(selection,0,2)]);break;
            case content_language:native["content_language"]=std::string(ui_tags[std::clamp(selection,0,2)]);break;
            case enabled:speech["enabled"]=checked;break;
            case engine:speech["engine"]=std::string(selection==1?"WINDOWS":"KOKORO");break;
            case language:speech["language"]=std::string(tags[std::clamp(selection,0,3)]);break;
            case voice:if(selection>=0&&static_cast<std::size_t>(selection)<voices.size()){const auto lang=state::string_value(data,"lesson_narration_settings","language","es");const bool system=SendDlgItemMessageW(window,engine,CB_GETCURSEL,0,0)==1;speech[std::string(system?"windows_voice:":"kokoro_voice:")+lang]=voices[selection];if(!system)speech["kokoro_voice"]=voices[selection];}break;
            case rate:speech["rate"]=std::array{.5,1.,1.5,2.}[std::clamp(selection,0,3)];break;
            case controls:speech["controls_visible"]=checked;break;
            case advance:speech["kokoro_advance_generation"]=checked;break;
            case auto_lesson:native["automatic_between_lessons"]=checked;break;
            case auto_unit:native["automatic_between_units"]=checked;break;
            case auto_course:native["automatic_between_courses"]=checked;break;
            case swipe_finish:native["swipe_finish"]=checked;break;
            case code_wrap:native["code_wrap"]=checked;break;
            default:break;
            }});
        if(id==language||id==engine){binding=true;fill_voices(access.read());binding=false;}
        if(access.changed)access.changed();
    }
};
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<Settings*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<Settings*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
    try{switch(message){
    case WM_CREATE:self->build();return 0;
    case WM_SIZE:self->layout();return 0;
    case WM_ERASEBKGND:{RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wp),&area,reader_theme::brush(reader_theme::Surface::canvas));return 1;}
    case WM_COMMAND:self->change(LOWORD(wp),HIWORD(wp));return 0;
    case close_message:if(self->access.close)self->access.close();return 0;
    case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item.CtlID==back){FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::card));auto bounds=item.rcItem;InflateRect(&bounds,-self->px(18),-self->px(8));draw_icon(item.hDC,bounds,Icon::back,reader_theme::palette().accent);return TRUE;}return reader_theme::draw_button(item);}
    case WM_MOUSEWHEEL:self->scroll-=GET_WHEEL_DELTA_WPARAM(wp)*self->px(48)/WHEEL_DELTA;self->layout();return 0;
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
    const auto instance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent,GWLP_HINSTANCE));
    WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=class_name;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    auto settings=std::make_unique<Settings>(std::move(access));const auto window=CreateWindowExW(0,class_name,L"Opciones",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,0,0,parent,nullptr,instance,settings.get());
    if(!window)throw std::runtime_error("No se pudo abrir las opciones.");settings.release();return window;
}
}
