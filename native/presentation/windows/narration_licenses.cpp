#include "presentation/windows/narration_licenses.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <richedit.h>
#include <commctrl.h>
#include <algorithm>
#include <filesystem>
#include <array>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
constexpr wchar_t class_name[]=L"LearnGameAndEngineDev.NarrationLicenses";
LRESULT CALLBACK child_keys(HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){RemoveWindowSubclass(child,child_keys,1);return DefSubclassProc(child,message,wp,lp);}
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE&&!SendDlgItemMessageW(GetParent(child),2,CB_GETDROPPEDSTATE,0,0)){
        PostMessageW(GetParent(child),WM_CLOSE,0,0);return 0;
    }
    return DefSubclassProc(child,message,wp,lp);
}
const std::array files{
    std::pair{L"app/src/main/res/raw/kokoro_model_notice.txt","Kokoro"},
    std::pair{L"app/src/main/res/raw/french_fluidaudio_license.txt","FluidAudio · Apache 2.0"},
    std::pair{L"app/src/main/res/raw/french_siwis_attribution.txt","SIWIS"},
    std::pair{L"app/src/main/res/raw/french_fluid_resources_notices.txt","FluidAudio · French resources"},
    std::pair{L"app/src/main/res/raw/french_rule_g2p_notice.txt","French G2P"},
    std::pair{L"app/src/main/res/raw/misaki_bart_attribution.txt","Misaki · BART"},
    std::pair{L"app/src/main/res/raw/spacy_pos_attribution.txt","spaCy · POS"},
    std::pair{L"app/src/main/res/raw/spacy_pos_spacy_license.txt","spaCy · License"},
    std::pair{L"app/src/main/res/raw/spacy_pos_thinc_license.txt","Thinc · License"},
    std::pair{L"app/src/main/res/raw/spacy_pos_model_license.txt","spaCy model · License"},
    std::pair{L"app/src/main/res/raw/onnxruntime_license.txt","ONNX Runtime"},
    std::pair{L"app/src/main/res/raw/onnxruntime_third_party_notices.txt","ONNX Runtime · Third-party notices"},
    std::pair{L"app/src/main/res/raw/onnxruntime_source_information.txt","ONNX Runtime · Source information"},
    std::pair{L"app/src/main/assets/licenses/FluidAudio-SpanishG2P.txt","FluidAudio · Spanish G2P"},
    std::pair{L"app/src/main/assets/narration/misaki/NOTICE.txt","Misaki · English G2P · Notices"},
    std::pair{L"app/src/main/assets/narration/misaki/LICENSE.txt","Misaki · License"}};
void select_notice(HWND window){
    const auto selected=SendMessageW(GetDlgItem(window,2),CB_GETCURSEL,0,0);
    if(selected<0||selected>=static_cast<LRESULT>(files.size()))return;
    const auto bytes=state::read_bytes(std::filesystem::path(build::source_root)/files[static_cast<std::size_t>(selected)].first,1024*1024);
    const auto body=GetDlgItem(window,1);
    set_edit_text(body,std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_SIZE|CFM_FACE|CFM_COLOR;format.yHeight=18*15;
    format.crTextColor=reader_theme::foreground(body);wcscpy_s(format.szFaceName,L"Roboto");
    SendMessageW(body,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
    SendMessageW(body,EM_SETSEL,0,0);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_COMMAND&&LOWORD(wp)==IDCANCEL&&HIWORD(wp)==0&&lp==0){DestroyWindow(window);return 0;}
    if(message==WM_SIZE){RECT bounds{};GetClientRect(window,&bounds);const int inset=MulDiv(20,GetDpiForWindow(window),96);
        const int selector=MulDiv(48,GetDpiForWindow(window),96);
        MoveWindow(GetDlgItem(window,2),inset,inset,std::max(1L,bounds.right-2*inset),selector+400,TRUE);
        MoveWindow(GetDlgItem(window,1),inset,inset+selector+inset,std::max(1L,bounds.right-2*inset),std::max(1L,bounds.bottom-3*inset-selector),TRUE);return 0;}
    if(message==WM_COMMAND&&LOWORD(wp)==2&&HIWORD(wp)==CBN_SELCHANGE){select_notice(window);return 0;}
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_NCDESTROY){if(const auto font=reinterpret_cast<HFONT>(RemovePropW(window,L"LGAED.LicenseFont")))DeleteObject(font);}
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE){DestroyWindow(window);return 0;}
    if(message==WM_ERASEBKGND){RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(wp),&bounds,reader_theme::brush(reader_theme::Surface::screen));return 1;}
    LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
    return DefWindowProcW(window,message,wp,lp);
}
}
HWND open_narration_licenses(HWND owner,std::string_view locale){
    static const auto rich_edit=LoadLibraryW(L"Msftedit.dll");
    if(!rich_edit)throw std::runtime_error("Cannot load narration license text control");
    const auto module=GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{};existing.cbSize=sizeof(existing);
    if(!GetClassInfoExW(module,class_name,&existing)){WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=module;cls.lpfnWndProc=procedure;
        cls.lpszClassName=class_name;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reader_theme::brush(reader_theme::Surface::screen);
        if(!RegisterClassExW(&cls))throw std::runtime_error("Cannot register narration license window");}
    const auto title=locale=="fr"?L"Licences et attributions de lecture":locale=="en"?L"Reading licenses and attributions":L"Licencias y atribuciones de lectura";
    const auto window=CreateWindowExW(WS_EX_CONTROLPARENT,class_name,title,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,840,680,owner,nullptr,module,nullptr);
    if(!window)throw std::runtime_error("Cannot open narration license window");
    reader_theme::apply(window,reader_theme::Surface::screen);
    const auto text=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
        20,20,780,600,window,reinterpret_cast<HMENU>(1),module,nullptr);
    if(!text){DestroyWindow(window);throw std::runtime_error("Cannot create narration license text control");}
    reader_theme::apply(text,reader_theme::Surface::screen);
    reader_theme::set_text_color(text,RGB(210,214,220));
    const auto selector=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,
        20,20,780,400,window,reinterpret_cast<HMENU>(2),module,nullptr);
    if(!selector){DestroyWindow(window);throw std::runtime_error("Cannot create narration license selector");}
    SetWindowSubclass(text,child_keys,1,0);SetWindowSubclass(selector,child_keys,1,0);
    reader_theme::apply(selector,reader_theme::Surface::catalog_card);
    reader_theme::set_text_color(selector,RGB(210,214,220));
    const auto font=CreateFontW(-MulDiv(18,GetDpiForWindow(window),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
    if(font){SetPropW(window,L"LGAED.LicenseFont",font);SendMessageW(selector,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);}
    for(const auto& [path,label]:files)SendMessageW(selector,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(label).c_str()));
    SendMessageW(selector,CB_SETCURSEL,0,0);select_notice(window);
    SendMessageW(window,WM_SIZE,0,0);ShowWindow(window,SW_SHOW);SetFocus(text);return window;
}
}
