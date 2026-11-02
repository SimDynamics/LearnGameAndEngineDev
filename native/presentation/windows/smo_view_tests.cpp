#include "presentation/windows/smo_view.hpp"
#include "presentation/windows/internal_surface_test_support.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
constexpr int back_id=7100,run_id=7101,pause_id=7102,triangle_id=7103,cube_id=7104,
    rules_id=7105,benchmark_id=7106,particles_id=7107,sustained_id=7108,source_id=7110,preview_id=7114,api_first_id=7120,
    gpu_rules_id=7140,d12_id=7141,rules_api_id=7150,d12_api_id=7160;

void check(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
struct Window {
    HWND value=nullptr;
    ~Window(){reset();}
    void reset(HWND next=nullptr){if(IsWindow(value))DestroyWindow(value);value=next;}
};
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
    if(message.message==WM_QUIT)continue;TranslateMessage(&message);DispatchMessageW(&message);}}
void pump_for(ULONGLONG duration){const auto end=GetTickCount64()+duration;do{pump();Sleep(5);}while(GetTickCount64()<end);pump();}
HWND button(HWND view,int id){const auto item=GetDlgItem(view,id);
    check(item&&IsWindowVisible(item)&&IsWindowEnabled(item),"A real SMO laboratory action is absent or disabled");return item;}
void click(HWND view,int id){SendMessageW(button(view,id),BM_CLICK,0,0);pump();}
SmoLabSnapshot inspect(HWND view,bool pixels=true){auto result=inspect_smo_lab(view,pixels);
    check(result.surface.error.empty(),"The real SMO graphics surface reported an error: "+result.surface.error);return result;}
template<class Predicate>SmoLabSnapshot until(HWND view,Predicate predicate,std::string_view reason){
    const auto end=GetTickCount64()+15000;
    do{pump();auto result=inspect(view,false);check(result.error.empty(),result.error);if(predicate(result))return result;Sleep(5);}while(GetTickCount64()<end);
    throw std::runtime_error(std::string(reason));
}
SmoLabSnapshot until_error(HWND view,std::string_view reason){
    const auto end=GetTickCount64()+15000;
    do{pump();auto result=inspect(view,false);if(!result.error.empty())return inspect(view);Sleep(5);}while(GetTickCount64()<end);
    throw std::runtime_error(std::string(reason));
}
std::string source_file(std::string_view sample){const auto bytes=state::read_bytes(
    std::filesystem::path(build::source_root)/"native"/(sample=="triangle"?"smo_triangle.smo":"smo_cube.smo"),64*1024);
    return std::string(bytes.begin(),bytes.end());}
void replace(std::string& text,std::string_view from,std::string_view to){const auto at=text.find(from);
    check(at!=std::string::npos,"The actual sample source no longer contains the intended edit");text.replace(at,from.size(),to);}
void edit(HWND view,std::string_view source){const auto item=GetDlgItem(view,source_id);check(item!=nullptr,"The real SMO RichEdit is missing");
    SetFocus(item);const auto text=wide(source);SendMessageW(item,EM_SETSEL,0,-1);
    SendMessageW(item,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
    check(edit_text(item)==source,"The actual editor did not retain the requested source");
}
HWND owned_rules(HWND host){struct Search{HWND owner,result=nullptr;} search{host};
    EnumChildWindows(host,[](HWND item,LPARAM data)->BOOL{auto& current=*reinterpret_cast<Search*>(data);wchar_t name[100]{};
        GetClassNameW(item,name,100);if(GetParent(item)==current.owner&&IsWindowVisible(item)&&std::wstring_view(name)==L"LearnGameAndEngineDev.SmoRules"){
            current.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.result;
}
struct ApiMenuProbe{bool initialized=false,valid=false;int first=api_first_id;};
LRESULT CALLBACK api_menu_proc(HWND view,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    auto& probe=*reinterpret_cast<ApiMenuProbe*>(data);
    if(message==WM_INITMENUPOPUP){probe.initialized=true;const auto menu=reinterpret_cast<HMENU>(wp);probe.valid=GetMenuItemCount(menu)==4;
        const std::array<std::wstring_view,4> names{L"DirectX 11",L"DirectX 12",L"Vulkan",L"OpenGL"};
        for(int i=0;i<4;++i){wchar_t caption[100]{};GetMenuStringW(menu,static_cast<UINT>(i),caption,100,MF_BYPOSITION);
            probe.valid=probe.valid&&GetMenuItemID(menu,i)==static_cast<UINT>(probe.first+i)&&std::wstring_view(caption).starts_with(names[static_cast<std::size_t>(i)]);}
        PostMessageW(view,WM_CANCELMODE,0,0);
    }
    return DefSubclassProc(view,message,wp,lp);
}
void CALLBACK cancel_api_menu(HWND view,UINT,UINT_PTR timer,DWORD){KillTimer(view,timer);EndMenu();}
void inspect_api_menu(HWND view,int action=sustained_id,int first=api_first_id){ApiMenuProbe probe;probe.first=first;constexpr UINT_PTR token=0x534d4f;
    check(SetWindowSubclass(view,api_menu_proc,token,reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,"Cannot inspect the real graphics API menu");
    const auto timer=SetTimer(view,token,250,cancel_api_menu);
    if(!timer){RemoveWindowSubclass(view,api_menu_proc,token);throw std::runtime_error("Cannot guard the isolated native popup lifecycle");}
    SendMessageW(GetDlgItem(view,action),BM_CLICK,0,0);
    KillTimer(view,timer);RemoveWindowSubclass(view,api_menu_proc,token);pump();
    check(probe.initialized&&probe.valid&&!inspect(view,false).benchmarking,
        "The actual minute-test menu omitted its four API commands or ran an API after dismissal");
}
void check_scene_preview(HWND view){const auto preview=GetDlgItem(view,preview_id);
    check(preview&&IsWindowVisible(preview)&&!IsWindowVisible(GetDlgItem(view,7112)),"The completed dynamic scene did not display its actual preview");
    RECT area{};GetClientRect(preview,&area);check(area.right>0&&area.bottom>0,"The scene preview has no drawable area");
    const auto screen=GetDC(preview),memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(preview,screen);
    if(!memory||!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);throw std::runtime_error("Cannot inspect the real scene preview pixels");}
    const auto original=SelectObject(memory,bitmap);DRAWITEMSTRUCT item{};item.CtlType=ODT_STATIC;item.CtlID=preview_id;item.itemAction=ODA_DRAWENTIRE;
    item.hwndItem=preview;item.hDC=memory;item.rcItem=area;const auto rendered=SendMessageW(view,WM_DRAWITEM,preview_id,reinterpret_cast<LPARAM>(&item));GdiFlush();
    std::set<std::uint32_t> colors;const auto* values=static_cast<const std::uint32_t*>(pixels);const auto count=static_cast<std::size_t>(area.right)*area.bottom;
    for(std::size_t i=0;i<count&&colors.size()<8;++i)colors.insert(values[i]&0x00ffffffu);
    SelectObject(memory,original);DeleteObject(bitmap);DeleteDC(memory);
    check(rendered!=FALSE&&colors.size()>4,"The real GPU ray-traced preview was replaced by an empty solid panel");
}
void assert_blank(const SmoLabSnapshot& value,std::uint64_t blank_hash){
    check(!value.running&&value.vertices==0&&value.faces==0&&value.surface.colored_pixels==0&&value.surface.pixel_hash==blank_hash,
        "Editing or rejecting source retained stale SMO geometry: running="+std::to_string(value.running)+
        " vertices="+std::to_string(value.vertices)+" faces="+std::to_string(value.faces)+
        " colored_pixels="+std::to_string(value.surface.colored_pixels)+" actual_hash="+std::to_string(value.surface.pixel_hash)+
        " expected_blank_hash="+std::to_string(blank_hash));
}
void internal_info_only(){
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"native-smo-info-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    check(!std::filesystem::exists(environment.paths.root),"Internal SMO info QA requires a new private profile");initialize_profile_directories(environment.paths);
    Window host;host.value=CreateWindowExW(0,L"STATIC",L"Internal SMO information QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,30,30,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host.value!=nullptr,"Cannot create internal SMO info QA root");Window view;
    view.value=create_smo_view(host.value,environment,[]{},GraphicsBackend::warp);RECT area{};GetClientRect(host.value,&area);MoveWindow(view.value,0,0,area.right,area.bottom,TRUE);pump();
    const auto before=inspect_smo_lab(view.value,false);
    click(view.value,rules_id);const auto rules=owned_rules(host.value);internal_surface_test::require_internal_surface(host.value,rules);
    check(!IsWindowVisible(view.value)&&GetDlgItem(rules,IDCANCEL)&&edit_text(GetDlgItem(rules,1)).find("alternativa(")!=std::string::npos,
        "Internal SMO information lost its original content, Back action or retained source page");
    RECT root_bounds{};GetWindowRect(host.value,&root_bounds);MoveWindow(host.value,root_bounds.left,root_bounds.top,560,380,TRUE);layout_internal_windows(host.value);pump();
    internal_surface_test::require_internal_surface(host.value,rules);
    MoveWindow(host.value,root_bounds.left,root_bounds.top,root_bounds.right-root_bounds.left,root_bounds.bottom-root_bounds.top,TRUE);layout_internal_windows(host.value);
    SendMessageW(GetDlgItem(rules,IDCANCEL),BM_CLICK,0,0);pump();
    const auto after=inspect_smo_lab(view.value,false);
    check(!IsWindow(rules)&&IsWindowVisible(view.value)&&IsWindowEnabled(host.value)&&after.source==before.source&&!after.running&&!after.benchmarking,
        "Returning from internal SMO info changed the draft, executed source or failed to restore the page");
    click(view.value,rules_id);const auto repeated=owned_rules(host.value);internal_surface_test::require_internal_surface(host.value,repeated);
    SendMessageW(repeated,WM_CLOSE,0,0);pump();check(!IsWindow(repeated)&&IsWindowVisible(view.value),"Repeated internal SMO info did not close cleanly");
    std::cout<<"PASS internal SMO information: one visible root/client bounds/resize, original text, Back/reopen and retained source; no SMO execution or benchmark\n";
}
void exercise(std::string_view sample,SmoProgramBackend program_backend){
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"native-smo-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    check(!std::filesystem::exists(environment.paths.root),"The isolated SMO QA profile already exists");initialize_profile_directories(environment.paths);
    {
        state::StateStore store(environment.paths.root);auto data=store.load();
        data.preferences["smo_qa_unrelated"]["sentinel"]=std::string("preserve ordinary lesson data \t\n");
        state::Draft untouched;untouched.source="int preserved = 7;\n";untouched.input=" \t \n";
        state::put_draft(data,"unrelated_cpp_editor",untouched);store.save(data);
    }
    Window host;host.value=CreateWindowExW(0,L"STATIC",L"SMO real interpreter and WARP integration QA",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1280,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host.value!=nullptr,"Could not create the isolated SMO QA host");
    unsigned returned=0;Window view;
    const auto create=[&]{const auto result=create_smo_view(host.value,environment,[&]{++returned;},GraphicsBackend::warp,program_backend);
        check(result!=nullptr,"Could not create the actual SMO laboratory");RECT area{};GetClientRect(host.value,&area);
        MoveWindow(result,0,0,area.right,area.bottom,TRUE);pump();return result;};
    view.value=create();if(sample=="cube")click(view.value,cube_id);
    for(const auto id:{back_id,run_id,pause_id,triangle_id,cube_id,rules_id,benchmark_id,particles_id,sustained_id,gpu_rules_id,d12_id})(void)button(view.value,id);
    const auto original=source_file(sample);auto initial=inspect(view.value);
    check(initial.sample==sample&&initial.source==original&&!initial.running&&initial.vertices==0&&initial.surface.colored_pixels==0,
        "The editor did not open the actual unexecuted SMO sample");
    check(initial.surface.frames==0&&initial.surface.width>0&&initial.surface.height>0,
        "Opening an unexecuted editor should retain its blank canvas and defer numerical GPU preparation");
    const auto blank_hash=initial.surface.pixel_hash;
    const auto source=GetDlgItem(view.value,source_id);CHARFORMAT2W format{};format.cbSize=sizeof(format);
    SendMessageW(source,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
    // RichEdit reports its text format in twips, not an HFONT handle.
    const auto dpi=static_cast<int>(GetDpiForWindow(view.value));
    check(std::wstring_view(format.szFaceName)==reader_theme::code_font_face()&&
        std::abs(format.yHeight-MulDiv(MulDiv(18,dpi,96),1440,dpi))<=1&&format.crTextColor==reader_theme::foreground(source),
        "The real source editor did not receive its native code font and DPI metrics");
    click(view.value,rules_id);const auto rules=owned_rules(host.value);check(rules&&IsWindowVisible(rules),"The SMO rules action did not open its real window");
    check(edit_text(GetDlgItem(rules,1)).find("alternativa(")!=std::string::npos,"The rules window omitted the explicit alternative-result notation");
    SendMessageW(rules,WM_CLOSE,0,0);pump();check(!IsWindow(rules),"The rules window did not close independently");

    const auto expected_vertices=sample=="triangle"?3u:8u,expected_faces=sample=="triangle"?1u:12u;
    click(view.value,run_id);initial=until(view.value,[&](const auto& value){return value.running&&value.vertices==expected_vertices&&value.faces==expected_faces;},"The complete editor program did not finish GPU initialization");
    check(initial.error.empty()&&initial.running&&initial.vertices==expected_vertices&&initial.faces==expected_faces&&initial.surface.colored_pixels>100,
        "Executing the original editor source did not render its actual triangle or cube geometry");
    check(initial.backend==program_backend,"The editor silently substituted its numerical execution backend.");
    check(initial.surface.software&&initial.surface.backend.starts_with("Direct3D 11"),"The complete editor program did not use the explicitly selected WARP compute device");
    const auto start_frames=initial.surface.frames,start_hash=initial.surface.pixel_hash;
    (void)until(view.value,[&](const auto& value){return value.surface.frames>=start_frames+8;},"The SMO temporal update did not submit real GPU frames");
    check(inspect(view.value).surface.pixel_hash!=start_hash,"SMO temporal state did not change the actual rendered image");
    click(view.value,pause_id);const auto paused=inspect(view.value);pump_for(180);const auto held=inspect(view.value);
    check(!held.running&&held.surface.frames==paused.surface.frames&&held.surface.pixel_hash==paused.surface.pixel_hash,
        "Pause continued executing SMO updates or changed the displayed GPU frame");
    click(view.value,pause_id);(void)until(view.value,[&](const auto& value){return value.surface.frames>=held.surface.frames+4;},"Resume did not restart the actual SMO update path");
    click(view.value,pause_id);check(inspect(view.value).surface.pixel_hash!=held.surface.pixel_hash,"Resume retained a frozen native image");

    auto changed=original;replace(changed,sample=="triangle"?"color(1, .6, .2);":"color(.2, .8, 1);","color(.2, 1, .2);");
    replace(changed,sample=="triangle"?"estado velocidad = 1;":"estado velocidad = .6;","estado velocidad = 2;");
    edit(view.value,changed);assert_blank(inspect(view.value),blank_hash);
    click(view.value,run_id);const auto updated=until(view.value,[&](const auto& value){return value.running&&value.vertices==expected_vertices&&value.faces==expected_faces;},"The modified source did not finish GPU initialization");
    check(updated.error.empty()&&updated.running&&updated.source==changed&&updated.vertices==expected_vertices&&updated.faces==expected_faces&&
        updated.surface.colored_pixels>100&&updated.surface.pixel_hash!=paused.surface.pixel_hash,
        "Run ignored the modified source or reused the preceding SMO GPU result");
    click(view.value,pause_id);
    edit(view.value,changed+"\nno_es_smo;\n");click(view.value,run_id);const auto invalid=until_error(view.value,"Invalid GPU source did not publish its compiler diagnostic");
    check(!invalid.error.empty(),"Invalid editor source did not report a real compiler diagnostic");assert_blank(invalid,blank_hash);
    auto too_large=changed;replace(too_large,"vertice(-1,","vertice(1"+std::string(40,'0')+",");
    edit(view.value,too_large);click(view.value,run_id);const auto unsupported=until_error(view.value,"The GPU graphics envelope did not publish its range diagnostic");
    check(!unsupported.error.empty(),"A coordinate outside the graphics adapter range did not produce a diagnostic");assert_blank(unsupported,blank_hash);
    edit(view.value,changed);click(view.value,run_id);(void)until(view.value,[&](const auto& value){return value.running&&value.vertices==expected_vertices;},"Valid source did not recover after a rejected GPU program");click(view.value,pause_id);

    const auto other=sample=="triangle"?std::string_view("cube"):std::string_view("triangle");
    const auto other_id=sample=="triangle"?cube_id:triangle_id,current_id=sample=="triangle"?triangle_id:cube_id;
    click(view.value,other_id);auto other_draft=source_file(other)+"\n// independent second sample draft\n";
    check(inspect(view.value).sample==other,"The real sample selector did not select its own program");edit(view.value,other_draft);
    click(view.value,current_id);check(inspect(view.value).source==changed,"Changing samples overwrote the first sample draft");
    click(view.value,other_id);check(inspect(view.value).source==other_draft,"The second sample did not retain its independent edited source");
    click(view.value,current_id);
    SendMessageW(GetDlgItem(view.value,source_id),EM_SETSEL,7,12);
    click(view.value,other_id);click(view.value,current_id);CHARRANGE cursor{};
    SendMessageW(GetDlgItem(view.value,source_id),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&cursor));
    check(cursor.cpMin==7&&cursor.cpMax==12,"Changing only the selection did not preserve its origin-aware editor position");
    const auto final_draft=changed+"\n// immediately closed without waiting for the save timer\n";edit(view.value,final_draft);
    view.reset();view.value=create();const auto restored=inspect(view.value);
    check(restored.sample==sample&&restored.source==final_draft&&!restored.running&&restored.vertices==0&&restored.surface.colored_pixels==0,
        "Recreating the laboratory lost its latest editor draft or executed stale source automatically");
    if(sample=="triangle"){
        SendMessageW(button(view.value,benchmark_id),BM_CLICK,0,0);
        check(inspect(view.value,false).benchmarking,"The GPU stress action did not start its asynchronous real worker");
        const auto benchmark_draft=final_draft+"// editor remains responsive during GPU test\n";edit(view.value,benchmark_draft);
        check(inspect(view.value,false).source==benchmark_draft,"The real source editor froze or lost text during GPU work");
        SendMessageW(button(view.value,benchmark_id),BM_CLICK,0,0);
        (void)until(view.value,[](const auto& value){return !value.benchmarking;},"The actual GPU stress worker did not honor cancellation");
        check(inspect(view.value,false).source==benchmark_draft&&IsWindowEnabled(GetDlgItem(view.value,run_id)),
            "Cancelling GPU work lost the draft or left normal execution disabled");
        SendMessageW(button(view.value,particles_id),BM_CLICK,0,0);
        check(inspect(view.value,false).benchmarking,"The actual particle stress action is disconnected");
        SendMessageW(button(view.value,particles_id),BM_CLICK,0,0);
        (void)until(view.value,[](const auto& value){return !value.benchmarking;},"The particle GPU worker did not honor cancellation");
        check(inspect(view.value,false).source==benchmark_draft,"Particle cancellation changed the edited SMO source");
        // WARP runs the small real particle workload. Exercise the complete
        // async result window without repeating physical stress measurements.
        SendMessageW(button(view.value,particles_id),BM_CLICK,0,0);
        (void)until(view.value,[](const auto& value){return !value.benchmarking;},"The real particle result did not reach its UI owner");
        const auto report_window=owned_rules(host.value);
        check(report_window&&edit_text(GetDlgItem(report_window,1)).find("WARP")!=std::string::npos,
            "The actual particle report window omitted its software/hardware evidence level");
        check(inspect(view.value,false).source==benchmark_draft,"Displaying the real particle result replaced its source document");
        SendMessageW(report_window,WM_CLOSE,0,0);pump();
        inspect_api_menu(view.value);
        // Only Direct3D11 is requested here: WARP maps it to the actual small
        // .08s/.02s correctness workload. Other APIs are observed in the menu,
        // never started as accidental physical minute-long UI tests.
        SendMessageW(view.value,WM_COMMAND,MAKEWPARAM(api_first_id,0),0);
        check(inspect(view.value,false).benchmarking,"The actual Direct3D11 scene menu command did not start its worker");
        SendMessageW(button(view.value,sustained_id),BM_CLICK,0,0);
        (void)until(view.value,[](const auto& value){return !value.benchmarking;},"The real dynamic scene worker did not honor the visible Cancel action");
        check(inspect(view.value,false).source==benchmark_draft&&IsWindowEnabled(GetDlgItem(view.value,run_id)),
            "Cancelling the dynamic scene changed its source or retained disabled execution controls");
        SendMessageW(view.value,WM_COMMAND,MAKEWPARAM(api_first_id,0),0);
        check(inspect(view.value,false).benchmarking,"The dynamic scene did not restart after cancellation");
        (void)until(view.value,[](const auto& value){return !value.benchmarking;},"The actual WARP scene did not complete and publish its result");
        const auto scene_window=owned_rules(host.value);check(scene_window!=nullptr,"The dynamic scene omitted its independently owned results window");
        const auto scene_text=edit_text(GetDlgItem(scene_window,1));
        check(scene_text.find("WARP")!=scene_text.npos&&scene_text.find("A/B/C")!=scene_text.npos&&scene_text.find("0 / 0")!=scene_text.npos,
            "The real dynamic result omitted its WARP evidence level or zero state/image mismatch counts");
        check_scene_preview(view.value);
        check(inspect(view.value,false).source==benchmark_draft,"Publishing the actual scene preview/result replaced the edited SMO source");
        SendMessageW(scene_window,WM_CLOSE,0,0);pump();
        inspect_api_menu(view.value,gpu_rules_id,rules_api_id);inspect_api_menu(view.value,d12_id,d12_api_id);
        for(const auto command:{rules_api_id,d12_api_id}){
            SendMessageW(view.value,WM_COMMAND,MAKEWPARAM(command,0),0);
            check(inspect(view.value,false).benchmarking,"The real arithmetic menu command did not start its worker");
            SendMessageW(button(view.value,command==rules_api_id?gpu_rules_id:d12_id),BM_CLICK,0,0);
            (void)until(view.value,[](const auto& value){return !value.benchmarking;},"The arithmetic GPU worker ignored cancellation");
            check(inspect(view.value,false).source==benchmark_draft&&IsWindowEnabled(GetDlgItem(view.value,run_id)),"Cancelling arithmetic GPU work changed the draft or retained disabled controls");
        }
    }
    click(view.value,back_id);check(returned==1,"The actual SMO Back control did not invoke its owner");view.reset();
    const auto retained=state::StateStore(environment.paths.root).load();const auto untouched=state::get_draft(retained,"unrelated_cpp_editor","");
    check(state::string_value(retained,"smo_qa_unrelated","sentinel")=="preserve ordinary lesson data \t\n"&&
        untouched.source=="int preserved = 7;\n"&&untouched.input==" \t \n",
        "SMO draft persistence changed unrelated lesson preferences, source, or intentional stdin whitespace");
    std::cout<<"PASS SMO "<<sample<<(program_backend==SmoProgramBackend::compiled_binary32?" compiled binary32":" F12 reference")<<": actual source, WARP pixels and changing hashes, pause/resume, edits, diagnostics, separate drafts and recreation. WARP is not a physical GPU performance result.\n";
}
}

int main(int argc,char** argv){try{
    const bool info_only=argc==2&&std::string_view(argv[1])=="--internal-info";
    check(info_only||((argc==2||argc==3)&&(std::string_view(argv[1])=="triangle"||std::string_view(argv[1])=="cube")&&(argc==2||std::string_view(argv[2])=="f12")),"Expected triangle/cube with optional f12, or --internal-info");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);reader_theme::prepare_fonts();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};check(InitCommonControlsEx(&controls)!=FALSE,"Could not initialize native controls");
    if(info_only){internal_info_only();return 0;}
    exercise(argv[1],argc==3?SmoProgramBackend::experimental_f12:SmoProgramBackend::compiled_binary32);return 0;
}catch(const std::exception& error){std::cerr<<"SMO laboratory integration failure: "<<error.what()<<'\n';return 1;}}
