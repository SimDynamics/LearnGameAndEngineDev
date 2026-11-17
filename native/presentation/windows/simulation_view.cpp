#include "presentation/windows/simulation_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
using namespace curriculum;
constexpr wchar_t view_class[]=L"LearnGameAndEngineDev.Simulation";
constexpr wchar_t array_class[]=L"LearnGameAndEngineDev.SimulationArray";
constexpr wchar_t statistics_class[]=L"LearnGameAndEngineDev.SimulationStatistics";
constexpr int title_id=100,description_id=101,scenario_label_id=102,scenario_id=103,
    array_label_id=104,array_id=105,step_id=106,explanation_id=107,variables_id=108,
    output_label_id=109,output_id=110,previous_id=111,next_id=112,reset_id=113,
    input_label_id=114,input_id=115,apply_id=116,restore_id=117,error_id=118,
    groups_label_id=119,input_format_id=120,step_selector_id=121,group_first_id=200;
bool passive_message(UINT message) {
    return message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK||
        message==WM_MOUSEMOVE||message==WM_CAPTURECHANGED||message==WM_CANCELMODE||message==WM_NCDESTROY;
}
COLORREF blend(COLORREF background,COLORREF foreground,unsigned amount) {
    const auto channel=[amount](unsigned a,unsigned b){return (a*(255-amount)+b*amount)/255;};
    return RGB(channel(GetRValue(background),GetRValue(foreground)),
        channel(GetGValue(background),GetGValue(foreground)),channel(GetBValue(background),GetBValue(foreground)));
}
std::string data_identity(const std::vector<SimulationValue>& values) {
    std::uint64_t hash=14695981039346656037ULL;
    const auto append=[&](std::string_view text){for(const unsigned char byte:text){hash^=byte;hash*=1099511628211ULL;}};
    for(const auto& value:values){const auto text=simulation_value_text(value);
        append(std::holds_alternative<std::string>(value)?"text/":"int/");
        append(std::to_string(text.size()));append(":");append(text);append(";");}
    return std::to_string(hash);
}
void register_class(const wchar_t* name,WNDPROC procedure) {
    WNDCLASSEXW existing{sizeof(existing)};const auto module=GetModuleHandleW(nullptr);
    if(GetClassInfoExW(module,name,&existing))return;
    WNDCLASSEXW type{sizeof(type)};type.hInstance=module;type.lpfnWndProc=procedure;
    type.lpszClassName=name;type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&type))throw std::runtime_error("No se pudo crear la vista de simulación.");
}
// Author text and data can contain long tokens. Insert display-only breaks,
// retaining explicit lines, Unicode pairs and the original accessible text.
std::wstring wrapped_text(HDC dc,std::string_view value,int width) {
    const auto text=wide(value);std::wstring result;
    for(std::size_t at=0;at<text.size();){int fit=0;SIZE extent{};
        if(text[at]==L'\r'||text[at]==L'\n'){
            if(text[at]==L'\r'&&at+1<text.size()&&text[at+1]==L'\n')++at;
            result+=L'\n';++at;continue;}
        const auto newline=text.find_first_of(L"\r\n",at);
        const auto end=newline==std::wstring::npos?text.size():newline;
        GetTextExtentExPointW(dc,text.data()+at,static_cast<int>(end-at),std::max(1,width),&fit,nullptr,&extent);
        auto count=static_cast<std::size_t>(std::max(1,fit));
        if(at+count<end&&text[at+count-1]>=0xD800&&text[at+count-1]<=0xDBFF){
            if(count>1)--count;else ++count;}
        count=std::min(count,end-at);
        if(at+count<end){const auto separator=text.find_last_of(L" \t",at+count-1);
            if(separator!=std::wstring::npos&&separator>=at&&separator+1>at)count=separator+1-at;}
        result.append(text,at,count);at+=count;if(at<end)result+=L'\n';
    }
    return result;
}
std::wstring literal_button_caption(std::wstring_view text) {
    std::wstring result;for(const auto value:text){result+=value;if(value==L'&')result+=L'&';}return result;
}
std::string window_text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));return utf8(value);
}
}

struct SimulationView::Impl {
    SimulationSpec spec;
    SimulationTrace trace;
    std::string locale;
    SimulationCallbacks callbacks;
    std::map<std::string,TextSemantics> semantics;
    HWND host=nullptr,canvas=nullptr;
    HMODULE rich=nullptr;
    HFONT font=nullptr,base_font=nullptr,heading_font=nullptr,index_font=nullptr,value_font=nullptr;
    Microsoft::WRL::ComPtr<IAccPropServices> accessibility;
    std::map<int,HWND> controls;
    std::vector<HWND> group_buttons;
    std::vector<RECT> cell_rects;
    struct Marker {RECT bounds;std::string text;POINT tip;};
    std::vector<Marker> markers;
    RECT limit_bounds{};
    std::string limit_text;
    struct Statistic {std::string label,value;RECT bounds{};};
    std::vector<Statistic> statistics;
    std::map<int,std::string> visible_text;
    std::map<int,int> requested_heights;
    std::size_t position=0,scenario=0;
    SimulationInputFormat input_format=SimulationInputFormat::lines;
    int width=1,height=1,scale=100,canvas_height=1;
    bool initializing=true,arranging=false,layout_valid=false;
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    std::string error,input_key;

    Impl(const SimulationSpec& source,std::string language,SimulationCallbacks events,
            const std::map<std::string,TextSemantics>& authored)
        :spec(source),trace(build_simulation_trace(spec)),locale(std::move(language)),
         callbacks(std::move(events)),semantics(authored),input_key(data_identity(trace.original)) {}
    ~Impl() {
        *alive=false;
        if(host&&IsWindow(host))DestroyWindow(host);
        for(const auto value:{font,base_font,heading_font,index_font,value_font})if(value)DeleteObject(value);
        if(rich)FreeLibrary(rich);
    }
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(host)),96);}
    int sp(int value)const{return std::max(1,MulDiv(px(value),scale,100));}
    HWND item(int id)const{const auto found=controls.find(id);return found==controls.end()?nullptr:found->second;}
    const TextSemantics* semantic(std::string_view path)const {
        const auto found=semantics.find(std::string(path));return found==semantics.end()?nullptr:&found->second;
    }
    std::size_t count()const {
        if(spec.view==SimulationViewMode::walkthrough)return trace.frames.size();
        if(spec.view==SimulationViewMode::candidates)return std::max<std::size_t>(1,trace.candidate_frames.size());
        if(spec.view==SimulationViewMode::groups)return std::max<std::size_t>(1,trace.groups.size());
        return 1;
    }
    const SimulationFrame& frame()const {
        if(spec.view==SimulationViewMode::summary)return trace.frames.back();
        if(spec.view==SimulationViewMode::array)return trace.frames.front();
        if(spec.view==SimulationViewMode::groups||spec.view==SimulationViewMode::candidates)
            return trace.candidate_frames.empty()?trace.frames.front():trace.frames.at(trace.candidate_frames.at(position));
        return trace.frames.at(position);
    }
    bool stepping()const{return spec.view==SimulationViewMode::walkthrough||spec.view==SimulationViewMode::candidates;}
    HWND create(const wchar_t* type,int id,DWORD style=0) {
        const auto child=CreateWindowExW(0,type,L"",WS_CHILD|WS_VISIBLE|style,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!child)throw std::runtime_error("No se pudo crear un control de la simulación.");
        controls.emplace(id,child);reader_theme::apply(child,reader_theme::Surface::card);
        return child;
    }
    HWND label(int id) {
        const bool scrolling=id==output_id||id==explanation_id;
        const auto child=create(MSFTEDIT_CLASS,id,ES_READONLY|ES_MULTILINE|(scrolling?ES_AUTOVSCROLL|WS_VSCROLL:0));
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(base_font),FALSE);
        SendMessageW(child,EM_SETTARGETDEVICE,0,0);
        SendMessageW(child,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE);
        SetWindowSubclass(child,passive_proc,1,reinterpret_cast<DWORD_PTR>(this));return child;
    }
    HWND button(int id,std::string_view text,DWORD style=BS_PUSHBUTTON) {
        const auto child=create(L"BUTTON",id,WS_TABSTOP|style);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SetWindowTextW(child,literal_button_caption(wide(text)).c_str());
        SetWindowSubclass(child,button_proc,2,reinterpret_cast<DWORD_PTR>(this));return child;
    }
    void set_label(int id,std::string text,RichTextRole role=RichTextRole::body,const TextSemantics* authored=nullptr) {
        visible_text[id]=std::move(text);
        set_rich_text(item(id),visible_text.at(id),{},false,role,locale,{}, {},authored);
        PARAFORMAT2 paragraph{};paragraph.cbSize=sizeof(paragraph);paragraph.dwMask=PFM_LINESPACING|PFM_SPACEAFTER;
        paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;paragraph.dySpaceAfter=0;
        SendMessageW(item(id),EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
        SendMessageW(item(id),EM_SETZOOM,scale,100);
    }
    void color_plain_text(int id) {
        CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;
        format.crTextColor=reader_theme::foreground(item(id));
        SendMessageW(item(id),EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
        SendMessageW(item(id),EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
    }
    void fonts(bool rebase=true) {
        layout_valid=false;requested_heights.clear();
        const auto make=[&](int logical,bool scaled,const wchar_t* face,int weight=FW_NORMAL){
            const auto result=CreateFontW(-(scaled?sp(logical):px(logical)),0,0,0,weight,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);
            if(!result)throw std::runtime_error("No se pudo preparar la tipografía de la simulación.");return result;};
        const auto next=make(18,true,L"Roboto"),base=rebase?make(18,false,L"Roboto"):base_font,
            heading=make(23,true,L"Roboto",FW_BOLD),index=make(13,true,L"Roboto"),value=make(30,true,L"Roboto",FW_BOLD);
        const std::array old{font,rebase?base_font:nullptr,heading_font,index_font,value_font};
        font=next;base_font=base;heading_font=heading;index_font=index;value_font=value;
        for(const auto& [id,child]:controls){wchar_t type[48]{};GetClassNameW(child,type,48);
            if(std::wstring_view(type)==MSFTEDIT_CLASS){
                // RichEdit retains character formatting. A zoom-only change
                // must not replace that base with WM_SETFONT at the old zoom.
                if(rebase){SendMessageW(child,EM_SETZOOM,1,1);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(base_font),FALSE);}
                SendMessageW(child,EM_SETZOOM,scale,100);
            }else SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);}
        for(const auto value:old)if(value)DeleteObject(value);
    }
    void initialize(HWND parent) {
        register_class(view_class,procedure);register_class(array_class,array_procedure);register_class(statistics_class,statistics_procedure);
        rich=LoadLibraryW(L"Msftedit.dll");if(!rich)throw std::runtime_error("RichEdit no está disponible.");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,view_class,L"Simulación interactiva",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
            0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
        if(!host)throw std::runtime_error("No se pudo abrir la simulación.");
        reader_theme::apply(host,reader_theme::Surface::card);fonts();
        (void)CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(accessibility.GetAddressOf()));
        for(const auto id:{title_id,description_id,scenario_label_id,array_label_id,step_id,explanation_id,
                output_label_id,output_id,input_label_id,error_id,groups_label_id})label(id);
        const auto selector=create(L"COMBOBOX",scenario_id,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        SendMessageW(selector,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SendMessageW(selector,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Datos originales"));
        for(const auto& example:spec.scenarios){const auto text=wide(example.label);SendMessageW(selector,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
        SendMessageW(selector,CB_SETCURSEL,0,0);accessible(selector,"Escenario de la simulación");
        const auto step_selector=create(L"COMBOBOX",step_selector_id,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        SendMessageW(step_selector,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        accessible(step_selector,"Ir a un paso calculado de la simulación");
        canvas=CreateWindowExW(0,array_class,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(array_id)),GetModuleHandleW(nullptr),this);
        if(!canvas)throw std::runtime_error("No se pudo mostrar el array.");controls.emplace(array_id,canvas);
        const auto statistics_window=CreateWindowExW(0,statistics_class,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(variables_id)),GetModuleHandleW(nullptr),this);
        if(!statistics_window)throw std::runtime_error("No se pudieron mostrar las variables.");controls.emplace(variables_id,statistics_window);
        SetWindowPos(item(explanation_id),canvas,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(item(output_id),statistics_window,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        button(previous_id,"Anterior");button(next_id,"Siguiente");button(reset_id,"Reiniciar");
        reader_theme::primary_button(item(next_id));
        const auto format=create(L"COMBOBOX",input_format_id,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        SendMessageW(format,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SendMessageW(format,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Un texto por línea"));
        SendMessageW(format,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Lista entre comillas"));
        SendMessageW(format,CB_SETCURSEL,0,0);accessible(format,"Formato de los datos editables");
        const auto editor=create(MSFTEDIT_CLASS,input_id,WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);
        reader_theme::apply(editor,reader_theme::Surface::field);use_plain_text_input(editor);
        SendMessageW(editor,WM_SETFONT,reinterpret_cast<WPARAM>(base_font),FALSE);
        SendMessageW(editor,EM_EXLIMITTEXT,0,65536);
        button(apply_id,"Aplicar datos");button(restore_id,"Restablecer datos");
        if(spec.text_values&&std::ranges::any_of(trace.original,[](const auto& value){
            const auto* text=std::get_if<std::string>(&value);return text&&text->empty();}))
            input_format=SimulationInputFormat::json;
        set_edit_text(editor,simulation_input_text(trace.original,input_format));
        rebuild_groups();rebuild_steps();update();initializing=false;
    }
    void accessible(HWND target,const std::string& name) {
        if(accessibility)(void)accessibility->SetHwndPropStr(target,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,
            PROPID_ACC_NAME,wide(name).c_str());
    }
    void rebuild_groups() {
        for(const auto child:group_buttons){controls.erase(GetDlgCtrlID(child));if(IsWindow(child))DestroyWindow(child);}group_buttons.clear();
        if(spec.view!=SimulationViewMode::groups)return;
        for(std::size_t i=0;i<trace.groups.size();++i){const auto& group=trace.groups[i];
            const auto caption="Grupo "+std::to_string(i+1)+" · "+simulation_value_display_text(group.value);
            const auto child=button(group_first_id+static_cast<int>(i),caption,
                BS_AUTORADIOBUTTON|BS_MULTILINE|(i==0?WS_GROUP:0));
            accessible(child,caption+". Índices "+std::to_string(group.begin)+" a "+std::to_string(group.end-1)+
                ". Frecuencia "+std::to_string(group.end-group.begin)+".");
            SetWindowPos(child,group_buttons.empty()?item(scenario_id):group_buttons.back(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            group_buttons.push_back(child);
        }
    }
    void rebuild_steps() {
        const auto selector=item(step_selector_id);
        SendMessageW(selector,CB_RESETCONTENT,0,0);
        if(!stepping())return;
        const auto add=[&](std::string caption){const auto wide_caption=wide(caption);
            SendMessageW(selector,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide_caption.c_str()));};
        if(spec.view==SimulationViewMode::candidates){
            if(trace.candidate_frames.empty())add("Sin grupos");
            else for(std::size_t index=0;index<trace.candidate_frames.size();++index){
                const auto& phase=trace.frames.at(trace.candidate_frames[index]);
                const auto& value=trace.groups.at(phase.group-1).value;
                add("Grupo "+std::to_string(index+1)+" · "+simulation_value_display_text(value));
            }
        }else for(std::size_t index=0;index<trace.frames.size();++index){
            const auto& phase=trace.frames[index];std::string label;
            switch(phase.phase){
            case SimulationPhase::initial: label="Inicio";break;
            case SimulationPhase::start: label="Comienza grupo";break;
            case SimulationPhase::advance: label="Continúa grupo";break;
            case SimulationPhase::new_maximum: label="Nuevo máximo";break;
            case SimulationPhase::tie: label="Empate";break;
            case SimulationPhase::below_maximum: label="Menor que máximo";break;
            case SimulationPhase::done: label="Fin del recorrido";break;
            case SimulationPhase::empty: label="Array vacío";break;
            }
            if(phase.group>0)label+=" · grupo "+std::to_string(phase.group);
            add("Paso "+std::to_string(index+1)+" · "+label);
        }
        SendMessageW(selector,CB_SETCURSEL,static_cast<WPARAM>(position),0);
    }
    std::string array_text()const {
        std::string text=spec.labels.array+". ";
        if(trace.ordered.empty())text+="No hay valores.";
        for(std::size_t i=0;i<trace.ordered.size();++i){if(i)text+="; ";text+="índice "+std::to_string(i)+": "+simulation_value_display_text(trace.ordered[i]);}
        const auto& state=frame();
        if(spec.view==SimulationViewMode::array){for(std::size_t i=0;i<trace.groups.size();++i){const auto& group=trace.groups[i];
            text+=". Grupo "+std::to_string(i+1)+": índices "+std::to_string(group.begin)+" a "+std::to_string(group.end-1)+".";}}
        else if(spec.view!=SimulationViewMode::summary){
            if(state.begin<state.end)text+=". Intervalo resaltado ["+std::to_string(state.begin)+", "+std::to_string(state.end)+").";
            text+=". "+spec.labels.start+" = "+std::to_string(state.begin)+"; "+spec.labels.end+" = "+std::to_string(state.end)+" (límite exclusivo).";}
        return text;
    }
    std::string leader_text(const SimulationFrame& state)const {
        if(state.leaders.empty())return "ninguno";
        std::string result;for(const auto& value:state.leaders){if(!result.empty())result+=", ";result+=simulation_value_display_text(value);}return result;
    }
    void update() {
        layout_valid=false;requested_heights.clear();
        const auto& state=frame();
        set_label(title_id,spec.title,RichTextRole::heading,semantic("/title"));
        set_label(description_id,spec.description,RichTextRole::body,semantic("/description"));
        set_label(scenario_label_id,"Escenario");set_label(array_label_id,spec.labels.array+" · "+std::to_string(trace.ordered.size())+" elementos");
        set_label(groups_label_id,"Selecciona un grupo");
        std::string status;
        if(stepping())status=(spec.view==SimulationViewMode::candidates?"Grupo ":"Paso ")+std::to_string(position+1)+" de "+std::to_string(count());
        else if(spec.view==SimulationViewMode::groups)status=trace.groups.empty()?"No hay grupos":"Grupo "+std::to_string(position+1)+" de "+std::to_string(trace.groups.size());
        else if(spec.view==SimulationViewMode::summary)status="Resumen de los grupos";
        set_label(step_id,status);set_label(explanation_id,state.explanation);
        if(stepping())SendMessageW(item(step_selector_id),CB_SETCURSEL,static_cast<WPARAM>(position),0);
        statistics.clear();
        if(spec.view==SimulationViewMode::candidates){
            const auto examined=state.group>0&&state.group<=trace.groups.size()?simulation_value_display_text(trace.groups[state.group-1].value):"ninguno";
            statistics.push_back({spec.text_values?"Etiqueta examinada":"Valor examinado",examined});
            statistics.push_back({spec.labels.frequency,std::to_string(state.frequency)});
        }else if(spec.view!=SimulationViewMode::summary){statistics.push_back({spec.labels.start,std::to_string(state.begin)});
            statistics.push_back({spec.labels.end+" (exclusivo)",std::to_string(state.end)});
            statistics.push_back({spec.labels.frequency,std::to_string(state.frequency)});}
        if(spec.view!=SimulationViewMode::groups){statistics.push_back({spec.labels.maximum,std::to_string(state.maximum)});
            statistics.push_back({spec.labels.leaders,leader_text(state)});}
        std::string variables;for(const auto& statistic:statistics){if(!variables.empty())variables+='\n';variables+=statistic.label+": "+statistic.value;}
        visible_text[variables_id]=variables;SetWindowTextW(item(variables_id),wide(variables).c_str());accessible(item(variables_id),variables);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,item(variables_id),OBJID_CLIENT,CHILDID_SELF);
        set_label(output_label_id,spec.labels.output+(state.output.empty()?" (vacía)":""));
        visible_text[output_id]=state.output;set_edit_text(item(output_id),state.output);color_plain_text(output_id);
        SendMessageW(item(output_id),EM_SETZOOM,scale,100);color_plain_text(input_id);
        const auto input_help=!spec.text_values?std::string("Datos editables: un entero por línea."):
            input_format==SimulationInputFormat::json?
                std::string(R"(Datos editables: una lista de textos entre comillas. Ejemplo: ["", "patio", " "]. [] no contiene elementos; "" es una cadena vacía y " " contiene un espacio.)"):
                std::string("Datos editables: un texto por línea; los espacios se conservan. Un campo vacío no contiene elementos. Para introducir una cadena vacía, elige Lista entre comillas.");
        set_label(input_label_id,input_help);accessible(item(input_id),input_help);
        SendMessageW(item(input_format_id),CB_SETCURSEL,input_format==SimulationInputFormat::json?1:0,0);
        set_label(error_id,error);
        EnableWindow(item(previous_id),stepping()&&position>0);
        EnableWindow(item(next_id),stepping()&&position+1<count());
        EnableWindow(item(reset_id),stepping()&&position>0);
        for(std::size_t i=0;i<group_buttons.size();++i)SendMessageW(group_buttons[i],BM_SETCHECK,i==position?BST_CHECKED:BST_UNCHECKED,0);
        const auto description=array_text();SetWindowTextW(canvas,wide(description).c_str());accessible(canvas,description);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,canvas,OBJID_CLIENT,CHILDID_SELF);
        if(!initializing)arrange(width,true);
        InvalidateRect(canvas,nullptr,FALSE);InvalidateRect(item(variables_id),nullptr,FALSE);
    }
    void publish() {
        if(initializing)return;const auto life=alive;const auto layout=callbacks.layout_changed,changed=callbacks.changed;
        if(layout)layout();if(*life&&changed)changed();
    }
    bool go(std::size_t next) {
        if(next>=count())return false;if(next==position)return true;
        position=next;update();publish();return true;
    }
    const std::vector<SimulationValue>& original_data()const{return scenario?spec.scenarios.at(scenario-1).values:spec.values;}
    void replace_data(std::vector<SimulationValue> values,bool replace_editor) {
        // Validation and every frame finish before replacing the visible model.
        auto candidate=build_simulation_trace(spec,std::move(values));
        auto identity=data_identity(candidate.original);
        auto next_format=input_format;
        std::optional<std::string> replacement;
        if(replace_editor){
            if(spec.text_values&&std::ranges::any_of(candidate.original,[](const auto& value){
                const auto* text=std::get_if<std::string>(&value);return text&&text->empty();}))
                next_format=SimulationInputFormat::json;
            replacement=simulation_input_text(candidate.original,next_format);
        }
        trace=std::move(candidate);input_key=std::move(identity);position=0;error.clear();
        if(replacement){input_format=next_format;set_edit_text(item(input_id),*replacement);}
        rebuild_groups();rebuild_steps();update();publish();
    }
    void apply_input() {
        try {auto values=simulation_parse_input(spec,edit_text(item(input_id)),input_format);replace_data(std::move(values),false);}
        catch(const std::exception& failure){error=std::string("No se aplicaron los datos. ")+failure.what();update();publish();}
    }
    void change_input_format(SimulationInputFormat requested) {
        if(requested==input_format)return;
        // Converting a pending edit never applies it or moves the walkthrough.
        // A format that cannot represent every value leaves the edit intact.
        try {
            const auto values=simulation_parse_input(spec,edit_text(item(input_id)),input_format);
            const auto converted=simulation_input_text(values,requested);
            set_edit_text(item(input_id),converted);input_format=requested;error.clear();
        }catch(const std::exception& failure){
            error=std::string("No se cambió el formato; se conservaron los datos escritos. ")+failure.what();
        }
        update();publish();
    }
    int measure(std::string_view text,int available,HFONT selected=nullptr)const {
        if(text.empty())return 0;const auto dc=GetDC(host);const auto previous=SelectObject(dc,selected?selected:font);
        RECT box{0,0,std::max(1,available),0};const auto content=wrapped_text(dc,text,std::max(1,available));
        DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&box,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
        SelectObject(dc,previous);ReleaseDC(host,dc);
        return std::max(sp(22),static_cast<int>(box.bottom-box.top))+sp(8);
    }
    int array_layout(int available) {
        cell_rects.clear();markers.clear();limit_text.clear();limit_bounds={};
        const bool pointer=spec.view==SimulationViewMode::groups||stepping();
        const auto& state=frame();
        const auto marker_text=[&](std::size_t index){std::string result;
            if(pointer&&state.begin==index)result=spec.labels.start+" = "+std::to_string(index);
            if(pointer&&state.end==index){if(!result.empty())result+='\n';result+=spec.labels.end+" = "+std::to_string(index);}return result;};
        if(pointer&&state.end==trace.ordered.size()){
            if(state.begin==state.end)limit_text=spec.labels.start+" = "+std::to_string(state.begin)+" · ";
            limit_text+=spec.labels.end+" = "+std::to_string(state.end)+" · límite del array";}
        if(trace.ordered.empty()){
            const auto limit_height=measure(limit_text,available,index_font);limit_bounds={0,sp(64),available,sp(64)+limit_height};
            return sp(64)+limit_height;}
        const auto gap=sp(8);int desired=sp(80);
        for(const auto& value:trace.ordered){const auto text=wide(simulation_value_display_text(value));const auto dc=GetDC(host);const auto old=SelectObject(dc,font);
            SIZE measured{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&measured);SelectObject(dc,old);ReleaseDC(host,dc);
            desired=std::max(desired,std::min(sp(170),static_cast<int>(measured.cx)+sp(24)));}
        const auto columns=std::min<int>(static_cast<int>(trace.ordered.size()),std::max(1,(available+gap)/(desired+gap)));
        const auto cell_width=std::max(1,(available-gap*(columns-1))/columns);
        int top=sp(4);
        for(std::size_t first=0;first<trace.ordered.size();first+=static_cast<std::size_t>(columns)){
            const auto last=std::min(trace.ordered.size(),first+static_cast<std::size_t>(columns));int row=sp(spec.view==SimulationViewMode::array?88:68),marker_height=0;
            for(auto i=first;i<last;++i){const auto dc=GetDC(host);const auto old=SelectObject(dc,font);
                const auto content=wrapped_text(dc,simulation_value_display_text(trace.ordered[i]),std::max(1,cell_width-sp(16)));
                RECT bounds{0,0,std::max(1,cell_width-sp(16)),0};DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                SelectObject(dc,old);ReleaseDC(host,dc);row=std::max(row,static_cast<int>(bounds.bottom)+sp(spec.view==SimulationViewMode::array?62:42));
                if(const auto caption=marker_text(i);!caption.empty())marker_height=std::max(marker_height,measure(caption,cell_width,index_font)+sp(8));}
            for(auto i=first;i<last;++i){const auto x=static_cast<int>(i-first)*(cell_width+gap);
                const auto caption=marker_text(i);if(!caption.empty())markers.push_back({{x,top,x+cell_width,top+marker_height-sp(8)},caption,{x+cell_width/2,top+marker_height}});
                cell_rects.push_back({x,top+marker_height,x+cell_width,top+marker_height+row});}
            top+=marker_height+row+gap;
        }
        if(!limit_text.empty()){const auto limit_height=measure(limit_text,available,index_font);limit_bounds={0,top+sp(5),available,top+sp(5)+limit_height};top=limit_bounds.bottom+sp(4);}
        return top;
    }
    int statistics_layout(int available) {
        const int gap=sp(10);int top=0;
        for(std::size_t first=0;first<statistics.size();){
            const auto band=statistics.size()==5&&first<3?std::size_t{3}:
                statistics.size()==4&&first<2?std::size_t{2}:statistics.size();
            const auto maximum=std::min(band-first,statistics.size()-first);
            const auto columns=std::min<int>(static_cast<int>(maximum),std::max(1,(available+gap)/(sp(140)+gap)));
            const auto cell_width=std::max(1,(available-gap*(columns-1))/columns);int row=sp(100);
            for(auto index=first;index<first+static_cast<std::size_t>(columns);++index){
                const auto label_height=measure(statistics[index].label,std::max(1,cell_width-sp(20)),index_font);
                const auto dc=GetDC(host);const auto old=SelectObject(dc,value_font);
                const auto content=wrapped_text(dc,statistics[index].value,std::max(1,cell_width-sp(20)));
                RECT value{0,0,std::max(1,cell_width-sp(20)),0};DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&value,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                SelectObject(dc,old);ReleaseDC(host,dc);row=std::max(row,label_height+static_cast<int>(value.bottom)+sp(24));}
            for(int column=0;column<columns;++column){const auto x=column*(cell_width+gap);statistics[first+column].bounds={x,top,x+cell_width,top+row};}
            first+=static_cast<std::size_t>(columns);top+=row+gap;
        }
        return std::max(1,top);
    }
    int arrange(int requested,bool move) {
        if(arranging||!host)return height;arranging=true;
        struct Guard{bool& value;~Guard(){value=false;}} guard{arranging};
        layout_valid=false;
        width=std::max(1,requested);const auto margin=std::min(px(16),std::max(0,(width-1)/4));
        const auto available=std::max(1,width-2*margin);int top=px(16);
        const auto place=[&](int id,int x,int y,int w,int h,bool shown){const auto child=item(id);if(!child)return;
            if(move){ShowWindow(child,shown?SW_SHOWNOACTIVATE:SW_HIDE);if(shown)MoveWindow(child,x,y,std::max(1,w),std::max(1,h),TRUE);}};
        const auto text_row=[&](int id,bool shown=true,int minimum=0){const auto found=visible_text.find(id);const auto text=found==visible_text.end()?std::string_view{}:std::string_view(found->second);
            shown=shown&&(!text.empty()||minimum>0);
            int full_height=std::max(minimum,measure(text,available,id==title_id?heading_font:font));
            const bool scrolling=id==output_id||id==explanation_id;
            const auto limit=scrolling?sp(id==output_id?240:400):std::numeric_limits<int>::max();
            int h=std::min(full_height,limit);
            if(shown&&move){bool settled=false;
                // REQRESIZE already supplies the requested control size. Adding
                // padding to that result makes repeated layouts grow. Recheck
                // the applied height because scrollbars can change text width.
                for(int pass=0;pass<4;++pass){place(id,margin,top,available,h,true);requested_heights.erase(id);
                    SendMessageW(item(id),EM_REQUESTRESIZE,0,0);
                    if(const auto native=requested_heights.find(id);native!=requested_heights.end())full_height=std::max(full_height,native->second);
                    const auto next=std::min(full_height,limit);
                    if(next<=h){settled=true;break;}h=next;
                }
                if(!settled)throw std::runtime_error("No se pudo ajustar la altura del texto de la simulación.");
            }
            if(scrolling&&move){const auto child=item(id);const auto style=GetWindowLongPtrW(child,GWL_STYLE);
                SetWindowLongPtrW(child,GWL_STYLE,shown&&full_height>h?style|WS_TABSTOP:style&~WS_TABSTOP);}
            place(id,margin,top,available,h,shown);if(shown)top+=h+px(8);};
        text_row(title_id);text_row(description_id);
        text_row(scenario_label_id,!spec.scenarios.empty());
        place(scenario_id,margin,top,available,sp(240),!spec.scenarios.empty());if(!spec.scenarios.empty())top+=sp(38)+px(12);
        const bool groups=spec.view==SimulationViewMode::groups;
        text_row(groups_label_id,groups&&!group_buttons.empty());
        if(groups&&!group_buttons.empty()){
            const int gap=px(8),columns=std::max(1,std::min(static_cast<int>(group_buttons.size()),(available+gap)/(sp(170)+gap)));
            const int button_width=std::max(1,(available-gap*(columns-1))/columns);
            for(std::size_t first=0;first<group_buttons.size();first+=static_cast<std::size_t>(columns)){
                const auto last=std::min(group_buttons.size(),first+static_cast<std::size_t>(columns));int row=sp(44);
                for(auto i=first;i<last;++i){const auto caption="Grupo "+std::to_string(i+1)+" · "+simulation_value_display_text(trace.groups[i].value);
                    row=std::max(row,measure(caption,std::max(1,button_width-sp(28)))+px(8));}
                for(auto i=first;i<last;++i){const auto id=group_first_id+static_cast<int>(i);
                    if(move){const auto caption="Grupo "+std::to_string(i+1)+" · "+simulation_value_display_text(trace.groups[i].value);
                        const auto dc=GetDC(host);const auto old=SelectObject(dc,font);
                        const auto wrapped=wrapped_text(dc,caption,std::max(1,button_width-sp(28)));
                        SelectObject(dc,old);ReleaseDC(host,dc);SetWindowTextW(item(id),literal_button_caption(wrapped).c_str());}
                    place(id,margin+static_cast<int>(i-first)*(button_width+gap),top,button_width,row,true);}
                top+=row+gap;
            }
            top+=px(8);
        }
        text_row(array_label_id);
        canvas_height=array_layout(available);place(array_id,margin,top,available,canvas_height,true);top+=canvas_height+px(12);
        text_row(step_id);
        place(step_selector_id,margin,top,available,sp(240),stepping());
        if(stepping())top+=sp(38)+px(8);
        text_row(explanation_id);
        const bool stats=spec.view!=SimulationViewMode::array;
        const auto stats_height=statistics_layout(available);place(variables_id,margin,top,available,stats_height,stats);if(stats)top+=stats_height+px(8);
        const bool output=spec.view==SimulationViewMode::walkthrough||spec.view==SimulationViewMode::candidates||spec.view==SimulationViewMode::summary;
        text_row(output_label_id,output);text_row(output_id,output,sp(34));
        const auto buttons=[&](std::initializer_list<int> ids,bool shown){
            if(!shown){for(const auto id:ids)place(id,0,0,1,1,false);return;}
            const int gap=px(8),n=static_cast<int>(ids.size());
            int minimum=sp(136);for(const auto id:ids){const auto caption=window_text(item(id));minimum=std::max(minimum,measure(caption,sp(240))+sp(75));}
            const bool horizontal=available>=minimum*n+gap*(n-1);const auto bw=horizontal?(available-gap*(n-1))/n:available;
            const auto h=std::max(sp(44),sp(30)+px(12));int column=0;
            for(const auto id:ids){place(id,margin+(horizontal?column*(bw+gap):0),top,bw,h,true);if(!horizontal)top+=h+gap;++column;}
            if(horizontal)top+=h+gap;top+=px(8);
        };
        buttons({previous_id,next_id,reset_id},stepping());
        text_row(input_label_id,spec.editable);
        const bool text_format=spec.editable&&spec.text_values;
        place(input_format_id,margin,top,available,sp(150),text_format);if(text_format)top+=sp(38)+px(8);
        place(input_id,margin,top,available,sp(116),spec.editable);if(spec.editable)top+=sp(116)+px(8);
        buttons({apply_id,restore_id},spec.editable);text_row(error_id,!error.empty());
        height=top+px(8);layout_valid=move;return height;
    }
    int measured_height(int requested) {
        requested=std::max(1,requested);
        return layout_valid&&requested==width?height:arrange(requested,true);
    }
    bool selected(std::size_t index)const {
        if(spec.view==SimulationViewMode::array)return true;
        if(spec.view==SimulationViewMode::summary){const auto& value=trace.ordered[index];return std::ranges::find(frame().leaders,value)!=frame().leaders.end();}
        return index>=frame().begin&&index<frame().end;
    }
    void paint_array(HDC dc) {
        RECT area{};GetClientRect(canvas,&area);const auto colors=reader_theme::palette(host);
        const auto fill=CreateSolidBrush(colors.card);FillRect(dc,&area,fill);DeleteObject(fill);SetBkMode(dc,TRANSPARENT);
        if(trace.ordered.empty()){const auto old=SelectObject(dc,font);SetTextColor(dc,colors.muted);auto empty=area;empty.bottom=sp(64);
            DrawTextW(dc,L"No hay valores.",-1,&empty,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,old);}
        const std::array<COLORREF,6> group_colors{RGB(83,164,240),RGB(66,189,145),RGB(239,172,67),RGB(186,133,235),RGB(233,123,151),RGB(87,190,203)};
        const auto selected_group=spec.view==SimulationViewMode::groups?position:frame().group?frame().group-1:0;
        for(std::size_t i=0;i<cell_rects.size();++i){const auto active=selected(i);auto bounds=cell_rects[i];
            const auto group=spec.view==SimulationViewMode::array?
                static_cast<std::size_t>(std::ranges::find_if(trace.groups,[&](const auto& value){return i>=value.begin&&i<value.end;})-trace.groups.begin()):selected_group;
            const auto tint=colors.high_contrast?colors.accent:group_colors[group%group_colors.size()];
            const auto background=active?(colors.high_contrast?colors.accent:blend(colors.card,tint,70)):colors.field;
            const auto brush=CreateSolidBrush(background);const auto pen=CreatePen(PS_SOLID,active?std::max(1,px(2)):1,active?tint:colors.outline);
            const auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);
            RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,px(10),px(10));SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);DeleteObject(brush);
            const auto foreground=colors.high_contrast&&active?GetSysColor(COLOR_HIGHLIGHTTEXT):colors.text;
            auto index_area=bounds;index_area.left+=sp(6);index_area.right-=sp(6);index_area.top+=sp(5);index_area.bottom=index_area.top+sp(21);
            auto old=SelectObject(dc,index_font);SetTextColor(dc,foreground);
            const auto index=wide("índice "+std::to_string(i));DrawTextW(dc,index.c_str(),-1,&index_area,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            auto value_area=bounds;value_area.left+=sp(8);value_area.right-=sp(8);value_area.top=index_area.bottom+sp(3);value_area.bottom-=sp(spec.view==SimulationViewMode::array?28:8);
            SelectObject(dc,font);const auto value=wrapped_text(dc,simulation_value_display_text(trace.ordered[i]),value_area.right-value_area.left);
            DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&value_area,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
            if(spec.view==SimulationViewMode::array){auto caption=bounds;caption.top=caption.bottom-sp(24);caption.bottom-=sp(4);SelectObject(dc,index_font);
                const auto name=wide("grupo "+std::to_string(group+1));DrawTextW(dc,name.c_str(),-1,&caption,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);}
            SelectObject(dc,old);
        }
        auto old=SelectObject(dc,index_font);SetTextColor(dc,colors.text);
        const auto pen=CreatePen(PS_SOLID,std::max(1,px(1)),colors.text);const auto old_pen=SelectObject(dc,pen);
        for(const auto& marker:markers){auto label=marker.bounds;const auto caption=wrapped_text(dc,marker.text,label.right-label.left);
            DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&label,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
            MoveToEx(dc,marker.tip.x,marker.tip.y-sp(7),nullptr);LineTo(dc,marker.tip.x,marker.tip.y-sp(1));
            MoveToEx(dc,marker.tip.x-sp(3),marker.tip.y-sp(4),nullptr);LineTo(dc,marker.tip.x,marker.tip.y-sp(1));LineTo(dc,marker.tip.x+sp(3),marker.tip.y-sp(4));}
        if(!limit_text.empty()){auto label=limit_bounds;const auto caption=wrapped_text(dc,limit_text,label.right-label.left);
            DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&label,DT_RIGHT|DT_WORDBREAK|DT_NOPREFIX);
            if(!cell_rects.empty()){const auto& last=cell_rects.back();const auto x=std::min(last.right-1,area.right-1);
                MoveToEx(dc,x,last.bottom+sp(2),nullptr);LineTo(dc,x,limit_bounds.top-sp(2));LineTo(dc,std::max<LONG>(0,x-sp(9)),limit_bounds.top-sp(2));}}
        SelectObject(dc,old_pen);DeleteObject(pen);SelectObject(dc,old);
    }
    void paint_statistics(HWND window,HDC dc) {
        RECT area{};GetClientRect(window,&area);const auto colors=reader_theme::palette(host);
        FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::card));SetBkMode(dc,TRANSPARENT);
        for(const auto& statistic:statistics){const auto brush=CreateSolidBrush(colors.field);const auto pen=CreatePen(PS_SOLID,1,colors.outline);
            const auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);const auto& bounds=statistic.bounds;
            RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,px(12),px(12));SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);DeleteObject(brush);
            auto label=bounds;InflateRect(&label,-sp(10),-sp(8));label.bottom=label.top+measure(statistic.label,label.right-label.left,index_font);
            auto old=SelectObject(dc,index_font);SetTextColor(dc,colors.high_contrast?colors.text:colors.muted);const auto caption=wrapped_text(dc,statistic.label,label.right-label.left);
            DrawTextW(dc,caption.c_str(),static_cast<int>(caption.size()),&label,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
            RECT value{bounds.left+sp(10),label.bottom+sp(3),bounds.right-sp(10),bounds.bottom-sp(10)};
            SelectObject(dc,value_font);SetTextColor(dc,colors.text);const auto content=wrapped_text(dc,statistic.value,value.right-value.left);
            DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&value,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);
        }
    }
    void paint(HDC dc)const {
        RECT area{};GetClientRect(host,&area);FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::card));
    }
    std::vector<SimulationReadingRegion> regions()const {
        std::vector<SimulationReadingRegion> result;
        const auto add=[&](int id,std::string key){const auto text=visible_text.find(id);if(text!=visible_text.end()&&!text->second.empty()&&
            (GetWindowLongPtrW(item(id),GWL_STYLE)&WS_VISIBLE))result.push_back({std::move(key),text->second,item(id),id==variables_id});};
        add(title_id,"title");add(description_id,"description");
        const auto prefix="state/"+std::to_string(scenario)+"/"+input_key+"/"+std::to_string(position)+"/";
        add(array_label_id,prefix+"array-label");result.push_back({prefix+"array",array_text(),canvas,true});
        add(step_id,prefix+"step");add(explanation_id,prefix+"explanation");add(variables_id,prefix+"variables");
        add(output_label_id,prefix+"output-label");add(output_id,prefix+"output");add(input_label_id,"input-label");add(error_id,prefix+"error");
        return result;
    }
    LRESULT command(WPARAM command,LPARAM) {
        const auto id=LOWORD(command),event=HIWORD(command);
        if(id==scenario_id&&event==CBN_SELCHANGE){const auto selected=SendMessageW(item(scenario_id),CB_GETCURSEL,0,0);
            if(selected>=0&&static_cast<std::size_t>(selected)<=spec.scenarios.size()){
                const auto previous=scenario;scenario=static_cast<std::size_t>(selected);
                try{replace_data(original_data(),true);}catch(...){scenario=previous;SendMessageW(item(scenario_id),CB_SETCURSEL,scenario,0);throw;}}
            return 0;}
        if(id==step_selector_id&&event==CBN_SELCHANGE&&stepping()){
            const auto selected=SendMessageW(item(step_selector_id),CB_GETCURSEL,0,0);
            if(selected>=0&&static_cast<std::size_t>(selected)<count())go(static_cast<std::size_t>(selected));
            return 0;
        }
        if(id==input_format_id&&event==CBN_SELCHANGE&&spec.editable&&spec.text_values){
            const auto selected=SendMessageW(item(input_format_id),CB_GETCURSEL,0,0);
            if(selected==0||selected==1)change_input_format(selected==1?SimulationInputFormat::json:SimulationInputFormat::lines);
            return 0;
        }
        if(event!=BN_CLICKED)return 0;
        if(id==previous_id){if(position)go(position-1);}
        else if(id==next_id)go(position+1);
        else if(id==reset_id)go(0);
        else if(id==apply_id&&spec.editable)apply_input();
        else if(id==restore_id&&spec.editable)replace_data(original_data(),true);
        else if(id>=group_first_id&&static_cast<std::size_t>(id-group_first_id)<group_buttons.size())go(id-group_first_id);
        return 0;
    }
    static LRESULT CALLBACK passive_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto* self=reinterpret_cast<Impl*>(raw);
        if(message==WM_GETDLGCODE){const auto* key=reinterpret_cast<const MSG*>(lp);
            const auto flags=DefSubclassProc(item,message,wp,lp);
            if(key&&key->message==WM_KEYDOWN&&key->wParam==VK_TAB)return flags&~(DLGC_WANTTAB|DLGC_WANTALLKEYS);
            return flags;}
        if(self->callbacks.passive_gesture&&passive_message(message)){
            const auto handled=self->callbacks.passive_gesture(item,message,wp,lp);if(handled&&message!=WM_NCDESTROY)return *handled;}
        if(message==WM_MOUSEWHEEL){SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE};
            if(!GetScrollInfo(item,SB_VERT,&scroll)||!scroll.nPage||scroll.nMax-scroll.nMin+1<=static_cast<int>(scroll.nPage))
                return SendMessageW(GetParent(item),message,wp,lp);}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(item,passive_proc,1);
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK button_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto* self=reinterpret_cast<Impl*>(raw);const auto id=GetDlgCtrlID(item);
        const bool group=id>=group_first_id&&static_cast<std::size_t>(id-group_first_id)<self->group_buttons.size();
        if(message==WM_GETDLGCODE){const auto* key=reinterpret_cast<const MSG*>(lp);auto value=DefSubclassProc(item,message,wp,lp);
            if(group)value|=DLGC_WANTARROWS;if(key&&key->wParam==VK_RETURN)value|=DLGC_WANTMESSAGE;return value;}
        if(message==WM_KEYDOWN&&wp==VK_RETURN){if(!(lp&(LPARAM{1}<<30)))SendMessageW(item,BM_CLICK,0,0);return 0;}
        if(message==WM_KEYUP&&wp==VK_RETURN)return 0;
        if(message==WM_CHAR&&(wp=='\r'||wp=='\n'))return 0;
        if(message==WM_KEYDOWN&&group&&(wp==VK_LEFT||wp==VK_UP||wp==VK_RIGHT||wp==VK_DOWN)){
            const auto current=static_cast<std::size_t>(id-group_first_id);const bool forward=wp==VK_RIGHT||wp==VK_DOWN;
            const auto next=forward?(current+1)%self->group_buttons.size():(current+self->group_buttons.size()-1)%self->group_buttons.size();
            const auto target=self->group_buttons[next];SetFocus(target);SendMessageW(target,BM_CLICK,0,0);return 0;}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(item,button_proc,2);
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK array_procedure(HWND item,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->canvas=item;SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(item,message,wp,lp);
        if(self->callbacks.passive_gesture&&passive_message(message)){
            const auto handled=self->callbacks.passive_gesture(item,message,wp,lp);if(handled&&message!=WM_NCDESTROY)return *handled;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);self->paint_array(dc);EndPaint(item,&paint);return 0;}
        if(message==WM_PRINTCLIENT){self->paint_array(reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(item),message,wp,lp);
        return DefWindowProcW(item,message,wp,lp);
    }
    static LRESULT CALLBACK statistics_procedure(HWND item,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(item,message,wp,lp);
        if(self->callbacks.passive_gesture&&passive_message(message)){
            const auto handled=self->callbacks.passive_gesture(item,message,wp,lp);if(handled&&message!=WM_NCDESTROY)return *handled;}
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);self->paint_statistics(item,dc);EndPaint(item,&paint);return 0;}
        if(message==WM_PRINTCLIENT){self->paint_statistics(item,reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(item),message,wp,lp);
        return DefWindowProcW(item,message,wp,lp);
    }
    static LRESULT CALLBACK procedure(HWND item,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->host=item;SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(item,message,wp,lp);
        try {
            LRESULT color{};if(reader_theme::control_color(message,wp,lp,color))return color;
            if(message==WM_COMMAND&&!self->initializing)return self->command(wp,lp);
            if(message==WM_NOTIFY&&lp){const auto* notification=reinterpret_cast<NMHDR*>(lp);
                if(notification->code==EN_REQUESTRESIZE){const auto* request=reinterpret_cast<REQRESIZE*>(lp);
                    self->requested_heights[GetDlgCtrlID(notification->hwndFrom)]=static_cast<int>(request->rc.bottom-request->rc.top);return 0;}}
            if(message==WM_DRAWITEM&&lp&&reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;
            if(message==WM_SIZE&&!self->initializing&&!self->arranging){RECT area{};GetClientRect(item,&area);
                const auto requested=std::max(1,static_cast<int>(area.right-area.left));
                if(!self->layout_valid||requested!=self->width){const auto required=self->arrange(requested,true);
                    // A parent's normal placement uses the already measured
                    // width. Only an independent width change needs reflow.
                    const auto callback=self->callbacks.layout_changed;
                    if(callback&&required!=area.bottom-area.top)callback();}
                return 0;}
            if(message==WM_DPICHANGED_AFTERPARENT&&!self->initializing){self->fonts();self->update();self->publish();return 0;}
            if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);self->paint(dc);EndPaint(item,&paint);return 0;}
            if(message==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(wp));return 0;}
            if(message==WM_ERASEBKGND)return 1;
            if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(item),message,wp,lp);
        }catch(const std::exception& failure){self->error=std::string("No se pudo actualizar la simulación. ")+failure.what();
            if(self->item(error_id)){SetWindowTextW(self->item(error_id),wide(self->error).c_str());ShowWindow(self->item(error_id),SW_SHOWNOACTIVATE);}return 0;}
        if(message==WM_NCDESTROY){self->host=nullptr;SetWindowLongPtrW(item,GWLP_USERDATA,0);}
        return DefWindowProcW(item,message,wp,lp);
    }
};

SimulationView::SimulationView(HWND parent,const curriculum::SimulationSpec& spec,std::string locale,
        SimulationCallbacks callbacks,const std::map<std::string,curriculum::TextSemantics>& semantics)
    :impl_(std::make_unique<Impl>(spec,std::move(locale),std::move(callbacks),semantics)){impl_->initialize(parent);}
SimulationView::~SimulationView()=default;
HWND SimulationView::window()const{return impl_->host;}
int SimulationView::height_for_width(int width){return impl_->measured_height(width);}
void SimulationView::set_text_scale(int percentage){percentage=std::clamp(percentage,25,150);if(percentage==impl_->scale)return;
    impl_->scale=percentage;impl_->fonts(false);impl_->update();}
std::size_t SimulationView::step_index()const{return impl_->position;}
std::size_t SimulationView::step_count()const{return impl_->count();}
bool SimulationView::go_to_step(std::size_t index){return impl_->go(index);}
void SimulationView::reset(){impl_->go(0);}
std::vector<SimulationReadingRegion> SimulationView::reading_regions()const{return impl_->regions();}
} // namespace lgaed::presentation
