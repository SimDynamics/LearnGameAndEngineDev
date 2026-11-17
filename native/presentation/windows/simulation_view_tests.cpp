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
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lgaed::curriculum;
using namespace lgaed::presentation;
constexpr int scenario_id=103,array_id=105,step_id=106,explanation_id=107,variables_id=108,
    output_id=110,previous_id=111,next_id=112,reset_id=113,input_id=115,apply_id=116,restore_id=117,error_id=118,
    group_first_id=200;
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
std::string native_text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));return utf8(value);
}
std::string text(SimulationView& view,int id){const auto window=GetDlgItem(view.window(),id);check(window,"A simulation control is missing");
    wchar_t type[48]{};GetClassNameW(window,type,48);return std::wstring_view(type)==MSFTEDIT_CLASS?edit_text(window):native_text(window);}
int window_height(HWND window){RECT area{};GetClientRect(window,&area);return static_cast<int>(area.bottom-area.top);}
void key(HWND dialog,WPARAM value) {
    const auto target=GetFocus();check(target,"Keyboard input has no actual focused window");
    check(PostMessageW(target,WM_KEYDOWN,value,1)&&PostMessageW(target,WM_KEYUP,value,(LPARAM{1}<<30)|(LPARAM{1}<<31)|1),
        "Cannot queue native keyboard input");pump(dialog);
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
        !IsWindowEnabled(GetDlgItem(view.window(),next_id)),"The empty scenario fabricated another step or output");
    check(name(GetDlgItem(view.window(),array_id)).find("No hay valores")!=std::string::npos,"Empty accessible array is ambiguous");
}
void modes(HWND host,const std::filesystem::path& captures) {
    for(const auto [mode,file]:std::array{
            std::pair{SimulationViewMode::array,"array-wide.bmp"},std::pair{SimulationViewMode::groups,"groups-wide.bmp"},
            std::pair{SimulationViewMode::candidates,"candidates-wide.bmp"},std::pair{SimulationViewMode::summary,"summary-wide.bmp"}}){
        auto spec=fixture(mode);spec.editable=false;SimulationView view(host,spec,"es");resize(view,960);
        if(mode==SimulationViewMode::groups){
            const auto group=GetDlgItem(view.window(),group_first_id);check(group&&SendMessageW(group,BM_GETCHECK,0,0)==BST_CHECKED,"The group selector lacks its initial checked state");
            SendMessageW(group,WM_KEYDOWN,VK_RIGHT,1);pump();
            check(view.step_index()==1&&SendMessageW(GetDlgItem(view.window(),group_first_id+1),BM_GETCHECK,0,0)==BST_CHECKED,
                "Keyboard group navigation did not update the radio state");
            check(text(view,variables_id).find("inicio: 1\nfin (exclusivo): 5")!=std::string::npos&&text(view,variables_id).find("frecuencia: 4")!=std::string::npos,
                "Group selection showed another group's interval or frequency");
            check(name(GetDlgItem(view.window(),array_id)).find("[1, 5)")!=std::string::npos,"The selected interval has no non-color description");
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
    check(text(first,output_id)=="faro:1\npatio:2\nsol:2\n"&&text(first,variables_id)=="máxima: 2\nmayores: patio, sol",
        "Text array maxima and ties differ from their input");
    set_edit_text(GetDlgItem(first.window(),input_id)," sitio \n sitio \nsol");click(first,apply_id);
    check(text(first,output_id)==" sitio :2\nsol:1\n"&&text(second,output_id)=="faro:1\npatio:2\nsol:2\n",
        "Editing text lost meaningful spaces or modified another view");
    const auto long_input=std::string(120,'x')+"\n😀";
    set_edit_text(GetDlgItem(first.window(),input_id),long_input);click(first,apply_id);
    const auto long_trace=build_simulation_trace(spec,simulation_parse_input(spec,long_input));
    check(text(first,explanation_id)==long_trace.frames.back().explanation&&text(first,explanation_id).find("😀")!=std::string::npos,
        "The native explanation lost the literal Unicode surrogate pair");
    first.set_text_scale(150);resize(first,340);bounds(first,"narrow long edited text");
    if(!captures.empty())capture(first.window(),captures/"text-narrow-long-value.bmp");
    const auto narrow=first.height_for_width(340);resize(first,800);
    check(first.height_for_width(800)<narrow,"Widening long text retained the previous narrow height");
    resize(first,340);check(first.height_for_width(340)==narrow,"Returning to the same narrow layout changed its height");
    first.set_text_scale(100);resize(first,340);check(first.height_for_width(340)<narrow,"Reducing text scale retained the enlarged layout");
    first.set_text_scale(150);resize(first,340);check(first.height_for_width(340)==narrow,"Restoring text scale changed the measured long-text height");
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
void scrollable_text_keyboard(HWND host) {
    auto spec=fixture(SimulationViewMode::summary);spec.values.clear();
    for(std::int64_t value=0;value<64;++value)spec.values.emplace_back(value);
    spec.explanations.done.clear();for(int line=0;line<70;++line)spec.explanations.done+="Explicación de este paso.\n";
    SimulationView view(host,spec,"es");resize(view,800);bounds(view,"scrollable explanation/output");
    const auto explanation=GetDlgItem(view.window(),explanation_id),output=GetDlgItem(view.window(),output_id);
    check((GetWindowLongPtrW(explanation,GWL_STYLE)&WS_TABSTOP)&&(GetWindowLongPtrW(output,GWL_STYLE)&WS_TABSTOP),
        "Scrollable read-only content cannot be reached with Tab");
    const auto selector=GetDlgItem(view.window(),scenario_id);SetFocus(selector);
    check(GetFocus()==selector,"Cannot focus the isolated native scenario control");
    bool visited_explanation=false,visited_output=false;
    for(int attempt=0;attempt<20&&(!visited_explanation||!visited_output);++attempt){
        key(host,VK_TAB);const auto focused=GetFocus();
        if(focused!=explanation&&focused!=output)continue;
        const auto before=SendMessageW(focused,EM_GETFIRSTVISIBLELINE,0,0);key(host,VK_NEXT);
        const auto after=SendMessageW(focused,EM_GETFIRSTVISIBLELINE,0,0);
        check(after>before,"PageDown did not reveal hidden read-only simulation content");
        if(focused==explanation)visited_explanation=true;else visited_output=true;
    }
    check(visited_explanation&&visited_output,"Real IsDialogMessage Tab traversal skipped a scrollable explanation or output");
    const auto original_output=text(view,output_id);check(original_output.find("63:1\n")!=std::string::npos,"The scrolled output lost its last line");
    SendMessageW(selector,CB_SETCURSEL,2,0);SendMessageW(view.window(),WM_COMMAND,MAKEWPARAM(scenario_id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(selector));pump(host);
    check(!(GetWindowLongPtrW(output,GWL_STYLE)&WS_TABSTOP)&&!(GetWindowLongPtrW(explanation,GWL_STYLE)&WS_TABSTOP),
        "Short replacement content retained unnecessary scrolling tab stops");
    SetFocus(nullptr);
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
    navigation(host.value,captures);editable_data(host.value,captures);modes(host.value,captures);text_and_independent_views(host.value,captures);
    parent_width_changes(host.value);long_labels_and_literal_values(host.value,captures);scrollable_text_keyboard(host.value);
    check(!IsWindowVisible(host.value)&&GetAncestor(GetForegroundWindow(),GA_ROOT)!=host.value,"Simulation QA activated the desktop");
    std::cout<<"PASS native simulation view: actual buttons, keyboard, exact replay, editable/scenario data, groups, candidates, summary, current narration and responsive controls\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native simulation view: "<<error.what()<<'\n';return 1;}
