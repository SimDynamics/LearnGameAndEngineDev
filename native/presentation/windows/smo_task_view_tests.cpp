#include "presentation/windows/smo_view.hpp"
#include "presentation/windows/smo_lazy_task.hpp"
#include "presentation/windows/smo_subdivision_task.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/state_store.hpp"
#include <commctrl.h>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,std::string_view why){if(!value)throw std::runtime_error(std::string(why));}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
struct Window{HWND value=nullptr;~Window(){if(IsWindow(value))DestroyWindow(value);}};
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_SIZE){if(const auto child=GetWindow(window,GW_CHILD))MoveWindow(child,0,0,LOWORD(lp),HIWORD(lp),TRUE);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
HWND result_window(HWND host){struct Search{HWND owner,result=nullptr;} s{host};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND item,LPARAM data)->BOOL{auto& s=*reinterpret_cast<Search*>(data);wchar_t name[100]{};GetClassNameW(item,name,100);
        if(GetWindow(item,GW_OWNER)==s.owner&&std::wstring_view(name)==L"LearnGameAndEngineDev.SmoRules"){s.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.result;}
void until_done(HWND view){const auto end=GetTickCount64()+15000;do{pump();const auto s=inspect_smo_lab(view,false);check(s.error.empty(),s.error);if(!s.benchmarking)return;Sleep(5);}while(GetTickCount64()<end);throw std::runtime_error("The real integrated task worker failed to complete promptly.");}
void inspect_pixels(HWND view){const auto preview=GetDlgItem(view,7114);check(preview&&IsWindowVisible(preview),"No real task preview was displayed.");RECT r{};GetClientRect(preview,&r);const auto screen=GetDC(preview),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(preview,screen);check(dc&&bitmap&&pixels,"Cannot inspect task presentation.");const auto old=SelectObject(dc,bitmap);
    DRAWITEMSTRUCT draw{};draw.CtlType=ODT_STATIC;draw.CtlID=7114;draw.hwndItem=preview;draw.hDC=dc;draw.rcItem=r;SendMessageW(view,WM_DRAWITEM,7114,reinterpret_cast<LPARAM>(&draw));GdiFlush();
    std::set<std::uint32_t> colors;const auto* p=static_cast<const std::uint32_t*>(pixels);for(std::size_t i=0;i<static_cast<std::size_t>(r.right)*r.bottom&&colors.size()<4;++i)colors.insert(p[i]);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);check(colors.size()>1,"Task visualization was an empty panel.");}
}
int main(int argc,char** argv){try{const bool visual=argc==2&&std::string_view(argv[1])=="visual";check(argc==1||visual,"Expected optional visual argument.");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);reader_theme::prepare_fonts();INITCOMMONCONTROLSEX c{sizeof(c),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&c)!=FALSE,"Cannot initialize controls.");
    const auto env=resolve_windows_environment({ProfileId::Codex,L"smo-integrated-tasks-"+std::to_wstring(GetCurrentProcessId()),false});initialize_profile_directories(env.paths);
    WNDCLASSW host_class{};host_class.lpfnWndProc=host_proc;host_class.hInstance=GetModuleHandleW(nullptr);host_class.lpszClassName=L"LearnGameAndEngineDev.SmoTaskQaHost";host_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);check(RegisterClassW(&host_class)!=0,"Cannot register QA host.");
    Window host{CreateWindowExW(0,host_class.lpszClassName,L"Laboratorio SMO · aplicaciones integradas (QA Codex)",WS_OVERLAPPEDWINDOW|WS_VISIBLE,70,60,1280,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};check(host.value!=nullptr,"No host.");
    Window view{create_smo_view(host.value,env,[]{},visual?GraphicsBackend::hardware:GraphicsBackend::warp)};check(view.value!=nullptr,"No actual SMO laboratory.");RECT area{};GetClientRect(host.value,&area);MoveWindow(view.value,0,0,area.right,area.bottom,TRUE);pump();
    check(GetDlgItem(view.value,7142)!=nullptr,"Integrated applications action missing.");const auto original=inspect_smo_lab(view.value,false).source;
    if(visual){ShowWindow(host.value,SW_MAXIMIZE);std::cout<<"Actual integrated laboratory ready for visual inspection. Close its QA host to end.\n"<<std::flush;while(IsWindow(host.value)){pump();Sleep(10);}return 0;}
    for(const auto command:{7202,7207,7307,7402,7240}){SendMessageW(view.value,WM_COMMAND,MAKEWPARAM(command,0),0);check(inspect_smo_lab(view.value,false).benchmarking,"The real task command did not launch its worker.");until_done(view.value);
        const auto result=result_window(host.value);check(result!=nullptr,"Task did not show its actual measurement/source window.");const auto text=edit_text(GetDlgItem(result,1));
        check(text.find("WARP")!=text.npos&&text.find("Código SMO ejecutado:")!=text.npos&&text.find(command==7240?smo_subdivision_source():smo_lazy_task_source())!=text.npos,"Result omitted software evidence or actual compiled source.");
        check(text.find(command==7240?"F12 directo sin máquina de instrucciones":"agrupación GPU")!=text.npos,"The real result window omitted the new measured variants.");
        inspect_pixels(view.value);check(inspect_smo_lab(view.value,false).source==original,"Task replaced the ordinary editor draft.");check(IsWindowEnabled(GetDlgItem(view.value,7101)),"Normal editor execution was not restored.");DestroyWindow(result);pump();}
    SendMessageW(view.value,WM_COMMAND,MAKEWPARAM(7202,0),0);SendMessageW(GetDlgItem(view.value,7142),BM_CLICK,0,0);until_done(view.value);check(inspect_smo_lab(view.value,false).source==original,"Cancellation changed editor source.");
    std::cout<<"PASS actual SMO task controls: grouped/mixed omission, F12 subdivisions, measured results/source, nonblank preview, cancellation and ordinary draft preservation. WARP is functional evidence only.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL integrated task UI: "<<e.what()<<'\n';return 1;}}
