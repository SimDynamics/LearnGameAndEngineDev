#include "presentation/windows/narration_licenses.hpp"
#include "presentation/windows/card_list.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <richedit.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
constexpr wchar_t class_name[]=L"LearnGameAndEngineDev.NarrationLicenses";
constexpr UINT activate_notice=WM_APP+1;
struct Notice {const wchar_t* path;const char* es;const char* en;const char* fr;};
const std::array files{
    Notice{L"app/src/main/res/raw/kokoro_model_notice.txt","Kokoro y voces","Kokoro and voices","Kokoro et voix"},
    Notice{L"app/src/main/res/raw/french_fluidaudio_license.txt","Apache License 2.0","Apache License 2.0","Apache License 2.0"},
    Notice{L"app/src/main/res/raw/french_siwis_attribution.txt","Voz francesa Siwis","Siwis French voice","Voix française Siwis"},
    Notice{L"app/src/main/res/raw/french_fluid_resources_notices.txt","FluidAudio · recursos franceses","FluidAudio · French resources","FluidAudio · ressources françaises"},
    Notice{L"app/src/main/res/raw/french_rule_g2p_notice.txt","Pronunciación francesa de palabras desconocidas (Apache-2.0)","French unknown-word pronunciation (Apache-2.0)","Prononciation française des mots inconnus (Apache-2.0)"},
    Notice{L"app/src/main/res/raw/misaki_bart_attribution.txt","Misaki · modelos BART de inglés","Misaki · English BART models","Misaki · modèles BART anglais"},
    Notice{L"app/src/main/res/raw/spacy_pos_attribution.txt","spaCy · atribución del análisis gramatical","spaCy · grammatical analysis credits","spaCy · crédits de l’analyse grammaticale"},
    Notice{L"app/src/main/res/raw/spacy_pos_spacy_license.txt","spaCy · MIT","spaCy · MIT","spaCy · MIT"},
    Notice{L"app/src/main/res/raw/spacy_pos_thinc_license.txt","Thinc · MIT","Thinc · MIT","Thinc · MIT"},
    Notice{L"app/src/main/res/raw/spacy_pos_model_license.txt","Modelo de spaCy · MIT","spaCy model · MIT","Modèle spaCy · MIT"},
    Notice{L"app/src/main/res/raw/onnxruntime_license.txt","ONNX Runtime · MIT","ONNX Runtime · MIT","ONNX Runtime · MIT"},
    Notice{L"app/src/main/res/raw/onnxruntime_third_party_notices.txt","ONNX Runtime · componentes","ONNX Runtime · components","ONNX Runtime · composants"},
    Notice{L"app/src/main/res/raw/onnxruntime_source_information.txt","ONNX Runtime · fuentes y avisos adicionales","ONNX Runtime · sources and additional notices","ONNX Runtime · sources et avis supplémentaires"},
    Notice{L"app/src/main/assets/licenses/FluidAudio-SpanishG2P.txt","FluidAudio · español","FluidAudio · Spanish","FluidAudio · espagnol"},
    Notice{L"app/src/main/assets/narration/misaki/NOTICE.txt","Misaki · inglés","Misaki · English","Misaki · anglais"},
    Notice{L"app/src/main/assets/narration/misaki/LICENSE.txt","Misaki · Apache 2.0","Misaki · Apache 2.0","Misaki · Apache 2.0"}};
struct View {
    HWND window=nullptr,list=nullptr,body=nullptr,heading=nullptr,close=nullptr;HFONT font=nullptr,title_font=nullptr;
    std::string locale;
    ~View(){if(font)DeleteObject(font);if(title_font)DeleteObject(title_font);}
    int px(int value)const{return MulDiv(value,GetDpiForWindow(window),96);}
    std::string tr(const char* es,const char* en,const char* fr)const{return locale=="fr"?fr:locale=="en"?en:es;}
    std::wstring title()const{return wide(tr("Licencias y atribuciones","Licenses and credits","Licences et crédits"));}
    std::wstring label(std::size_t index)const{return wide(tr(files[index].es,files[index].en,files[index].fr));}
    void fonts(){
        const auto regular=CreateFontW(-px(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        const auto bold=CreateFontW(-px(22),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        for(const auto child:{list,body,close})SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(regular),TRUE);
        SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(bold),TRUE);
        if(font)DeleteObject(font);if(title_font)DeleteObject(title_font);font=regular;title_font=bold;
        CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_SIZE|CFM_FACE|CFM_COLOR;format.yHeight=18*15;format.crTextColor=reader_theme::foreground(body);wcscpy_s(format.szFaceName,L"Roboto");
        SendMessageW(body,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
    }
    void layout(){RECT area{};GetClientRect(window,&area);const int margin=px(24),width=std::max(1L,area.right-2*margin),button_height=px(48);
        std::wstring title_text(static_cast<std::size_t>(GetWindowTextLengthW(heading))+1,L'\0');title_text.resize(GetWindowTextW(heading,title_text.data(),static_cast<int>(title_text.size())));
        const auto heading_dc=GetDC(heading);const auto heading_old=SelectObject(heading_dc,title_font);RECT title_bounds{0,0,width,0};
        DrawTextW(heading_dc,title_text.c_str(),-1,&title_bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(heading_dc,heading_old);ReleaseDC(heading,heading_dc);
        const int heading_height=std::max(px(32),static_cast<int>(title_bounds.bottom));
        MoveWindow(heading,margin,margin,width,heading_height,TRUE);
        const int top=margin+heading_height+px(12),height=std::max(1,static_cast<int>(area.bottom)-top-margin-button_height-px(12));
        MoveWindow(list,margin,top,width,height,TRUE);MoveWindow(body,margin,top,width,height,TRUE);
        MoveWindow(close,std::max(margin,static_cast<int>(area.right)-margin-px(140)),area.bottom-margin-button_height,px(140),button_height,TRUE);
        const auto dc=GetDC(list);const auto old=SelectObject(dc,font);
        for(std::size_t index=0;index<files.size();++index){RECT measure{0,0,std::max(1,width-px(32)),0};const auto text=label(index);DrawTextW(dc,text.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
            SendMessageW(list,LB_SETITEMHEIGHT,index,std::max(px(48),static_cast<int>(measure.bottom)+px(24)));}
        SelectObject(dc,old);ReleaseDC(list,dc);
    }
    void show_notice(){const auto index=SendMessageW(list,LB_GETCURSEL,0,0);if(index<0||index>=static_cast<LRESULT>(files.size()))return;
        const auto bytes=state::read_bytes(std::filesystem::path(build::source_root)/files[static_cast<std::size_t>(index)].path,1024*1024);
        set_edit_text(body,std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));SendMessageW(body,EM_SETSEL,0,0);
        SetWindowTextW(heading,label(static_cast<std::size_t>(index)).c_str());fonts();layout();ShowWindow(list,SW_HIDE);ShowWindow(body,SW_SHOW);SetFocus(body);
    }
    void back(){DestroyWindow(window);}
};
LRESULT CALLBACK child_keys(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(child,child_keys,1);return DefSubclassProc(child,message,wp,lp);}
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE){PostMessageW(GetParent(child),WM_COMMAND,IDCANCEL,0);return 0;}
    return DefSubclassProc(child,message,wp,lp);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<View*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);
    if(message==WM_NCDESTROY){SetWindowLongPtrW(window,GWLP_USERDATA,0);delete self;return DefWindowProcW(window,message,wp,lp);}
    if(message==WM_SIZE){if(self->list)self->layout();return 0;}
    if(message==WM_DPICHANGED){const auto* bounds=reinterpret_cast<const RECT*>(lp);SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOACTIVATE|SWP_NOZORDER);self->fonts();self->layout();return 0;}
    if(message==WM_DPICHANGED_AFTERPARENT){self->fonts();self->layout();return 0;}
    if(message==activate_notice){self->show_notice();return 0;}
    if((message==WM_COMMAND&&((LOWORD(wp)==IDCANCEL&&HIWORD(wp)==0&&lp==0)||(LOWORD(wp)==4&&HIWORD(wp)==BN_CLICKED)))||(message==WM_KEYDOWN&&wp==VK_ESCAPE)){self->back();return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DRAWITEM&&wp==2){const auto& draw=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(draw.itemID>=files.size())return FALSE;
        FillRect(draw.hDC,&draw.rcItem,reader_theme::brush(reader_theme::Surface::screen));SetBkMode(draw.hDC,TRANSPARENT);SetTextColor(draw.hDC,reader_theme::foreground(window));
        const auto old=SelectObject(draw.hDC,self->font);auto bounds=draw.rcItem;InflateRect(&bounds,-self->px(16),-self->px(12));const auto text=self->label(draw.itemID);
        DrawTextW(draw.hDC,text.c_str(),-1,&bounds,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(draw.hDC,old);return TRUE;}
    if(message==WM_DRAWITEM)return reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
    if(message==WM_ERASEBKGND){RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::screen));return 1;}
    LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
    return DefWindowProcW(window,message,wp,lp);
}
}
HWND open_narration_licenses(HWND owner,std::string_view locale){
    static const auto rich_edit=LoadLibraryW(L"Msftedit.dll");if(!rich_edit)throw std::runtime_error("Cannot load narration license text control");
    const auto module=GetModuleHandleW(nullptr);WNDCLASSEXW existing{};existing.cbSize=sizeof(existing);
    if(!GetClassInfoExW(module,class_name,&existing)){WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=module;cls.lpfnWndProc=procedure;cls.lpszClassName=class_name;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reader_theme::brush(reader_theme::Surface::screen);
        if(!RegisterClassExW(&cls))throw std::runtime_error("Cannot register narration license window");}
    auto* self=new View;self->locale=locale;const auto dpi=GetDpiForWindow(owner);
    const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,class_name,self->title().c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(840,dpi,96),MulDiv(680,dpi,96),owner,nullptr,module,self);
    if(!window)throw std::runtime_error("Cannot open narration license window");
    try{
        reader_theme::apply(window,reader_theme::Surface::screen);
        self->heading=CreateWindowExW(0,L"STATIC",self->title().c_str(),WS_CHILD|WS_VISIBLE,0,0,1,1,window,reinterpret_cast<HMENU>(3),module,nullptr);
        self->body=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(1),module,nullptr);
        self->list=create_card_list(window,2,activate_notice);
        self->close=CreateWindowExW(0,L"BUTTON",wide(self->tr("Cerrar","Close","Fermer")).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,1,1,window,reinterpret_cast<HMENU>(4),module,nullptr);
        if(!self->heading||!self->body||!self->close)throw std::runtime_error("Cannot create narration license controls");
        for(const auto child:{self->heading,self->body,self->close}){reader_theme::apply(child,reader_theme::Surface::screen);SetWindowSubclass(child,child_keys,1,0);}
        reader_theme::set_text_color(self->body,RGB(210,214,220));reader_theme::primary_button(self->close);
        for(std::size_t index=0;index<files.size();++index)SendMessageW(self->list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(self->label(index).c_str()));
        self->fonts();self->layout();ShowWindow(window,SW_SHOW);SetFocus(self->list);return window;
    }catch(...){DestroyWindow(window);throw;}
}
}
