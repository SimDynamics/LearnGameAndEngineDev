#include "presentation/windows/simulation_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lgaed::presentation {
namespace {
using namespace curriculum;
constexpr wchar_t view_class[]=L"LearnGameAndEngineDev.Simulation";
constexpr wchar_t array_class[]=L"LearnGameAndEngineDev.SimulationArray";
constexpr wchar_t statistics_class[]=L"LearnGameAndEngineDev.SimulationStatistics";
constexpr wchar_t frequency_class[]=L"LearnGameAndEngineDev.SimulationFrequencies";
constexpr int title_id=100,description_id=101,scenario_label_id=102,scenario_id=103,
    array_label_id=104,array_id=105,step_id=106,explanation_id=107,variables_id=108,
    output_label_id=109,output_id=110,previous_id=111,next_id=112,reset_id=113,
    input_label_id=114,input_id=115,apply_id=116,restore_id=117,error_id=118,
    groups_label_id=119,input_format_id=120,step_selector_id=121,original_order_id=122,transition_id=123,
    previous_decision_id=124,next_decision_id=125,result_comparison_id=126,
    frequencies_id=127,frequencies_label_id=128,frequency_filter_id=129,
    progress_label_id=130,progress_id=131,group_first_id=200;
// A child with a real scrollbar consumes a wheel gesture only while it can
// move in that direction. At either edge the lesson must continue scrolling.
bool can_scroll_with_wheel(HWND child,int delta) {
    if(delta==0)return false;
    SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE|SIF_POS};
    if(!GetScrollInfo(child,SB_VERT,&range)||range.nPage==0||
        range.nMax-range.nMin+1<=static_cast<int>(range.nPage))return false;
    const int last=std::max(range.nMin,range.nMax-
        std::max(0,static_cast<int>(range.nPage)-1));
    return delta>0?range.nPos>range.nMin:range.nPos<last;
}
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
// stable_sort keeps equal items in source order. Reconstruct the source
// position of each sorted cell without touching the validated trace or
// inventing a new authored value. A nonempty result means sorting moved data.
std::vector<std::size_t> stable_source_positions(const SimulationTrace& trace) {
    if(trace.original==trace.ordered)return {};
    std::vector<bool> used(trace.original.size(),false);
    std::vector<std::size_t> positions;
    positions.reserve(trace.ordered.size());
    for(const auto& value:trace.ordered){
        bool found=false;
        for(std::size_t index=0;index<trace.original.size();++index){
            if(used[index]||trace.original[index]!=value)continue;
            used[index]=true;positions.push_back(index);found=true;break;
        }
        if(!found)throw std::logic_error("La copia ordenada perdió la procedencia de un elemento.");
    }
    return positions;
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
    std::size_t authored_group_count=0,authored_maximum=0;
    std::vector<SimulationValue> authored_leaders;
    std::vector<std::size_t> source_positions;
    std::string locale;
    SimulationCallbacks callbacks;
    std::map<std::string,TextSemantics> semantics;
    HWND host=nullptr,canvas=nullptr,frequencies=nullptr;
    HMODULE rich=nullptr;
    HFONT font=nullptr,base_font=nullptr,heading_font=nullptr,index_font=nullptr,value_font=nullptr;
    Microsoft::WRL::ComPtr<IAccPropServices> accessibility;
    std::map<int,HWND> controls;
    std::vector<HWND> group_buttons;
    std::vector<RECT> cell_rects;
    std::optional<std::size_t> pressed_target,pressed_frequency_group;
    POINT pressed_at{},frequency_pressed_at{};
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
    int frequency_scroll=0,frequency_total_height=0;
    int input_wheel_delta=0;
    bool frequency_candidates_only=false;
    bool initializing=true,arranging=false,layout_valid=false;
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    std::string error,input_key,output_model_key,original_order_model_key;
    std::size_t output_scenario=0,original_order_scenario=0;
    COLORREF original_order_ink=CLR_INVALID;

    Impl(const SimulationSpec& source,std::string language,SimulationCallbacks events,
            const std::map<std::string,TextSemantics>& authored)
        :spec(source),trace(build_simulation_trace(spec)),
         source_positions(stable_source_positions(trace)),locale(std::move(language)),
         callbacks(std::move(events)),semantics(authored),input_key(data_identity(trace.original)) {
        // Cache only the authored final statistics, not a second 8 MiB trace.
        // Student data, scenarios and invalid drafts never rewrite this base.
        authored_group_count=trace.groups.size();
        authored_maximum=trace.frames.back().maximum;
        authored_leaders=trace.frames.back().leaders;
    }
    ~Impl() {
        *alive=false;
        if(host&&IsWindow(host))DestroyWindow(host);
        for(const auto value:{font,base_font,heading_font,index_font,value_font})if(value)DeleteObject(value);
        if(rich)FreeLibrary(rich);
    }
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(host)),96);}
    int sp(int value)const{return std::max(1,MulDiv(px(value),scale,100));}
    HWND item(int id)const{const auto found=controls.find(id);return found==controls.end()?nullptr:found->second;}
    // Preserve the position in the text, rather than a pixel scroll offset.
    // RichEdit's pixels and wrapped visual lines can change with reader zoom
    // or monitor DPI, while this character belongs to the same stdout.
    struct OutputReadingAnchor {
        LONG first_character = -1;
        CHARRANGE selection{};
        bool at_end = false;
    };
    std::optional<OutputReadingAnchor> capture_output_reading_anchor()const {
        const auto output=item(output_id);
        if(!output||!IsWindow(output))return std::nullopt;
        OutputReadingAnchor anchor;
        const auto first=SendMessageW(output,EM_GETFIRSTVISIBLELINE,0,0);
        if(first>=0)anchor.first_character=static_cast<LONG>(
            SendMessageW(output,EM_LINEINDEX,static_cast<WPARAM>(first),0));
        SendMessageW(output,EM_EXGETSEL,0,
            reinterpret_cast<LPARAM>(&anchor.selection));
        SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS};
        if(GetScrollInfo(output,SB_VERT,&scroll)&&scroll.nPage>0&&
                scroll.nMax-scroll.nMin+1>static_cast<int>(scroll.nPage))
            anchor.at_end=scroll.nPos>=std::max(scroll.nMin,
                scroll.nMax-std::max(0,static_cast<int>(scroll.nPage)-1));
        return anchor;
    }
    void restore_output_reading_anchor(const std::optional<OutputReadingAnchor>& saved)const {
        if(!saved)return;
        const auto output=item(output_id);
        if(!output||!IsWindow(output))return;
        const LONG length=static_cast<LONG>(GetWindowTextLengthW(output));
        const CHARRANGE selection{
            std::clamp(saved->selection.cpMin,LONG{0},length),
            std::clamp(saved->selection.cpMax,LONG{0},length)};
        SendMessageW(output,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
        if(saved->at_end){
            SendMessageW(output,WM_VSCROLL,MAKEWPARAM(SB_BOTTOM,0),0);
        }else if(saved->first_character>=0){
            // EM_EXLINEFROMCHAR supports the full RichEdit character range,
            // unlike the older EM_LINEFROMCHAR 64K compatibility limit.
            const auto target=SendMessageW(output,EM_EXLINEFROMCHAR,0,
                static_cast<LPARAM>(std::min(saved->first_character,length)));
            const auto current=SendMessageW(output,EM_GETFIRSTVISIBLELINE,0,0);
            if(target>=0&&current>=0&&target!=current)
                SendMessageW(output,EM_LINESCROLL,0,
                    static_cast<LPARAM>(target-current));
        }
    }
    // Always restore the active stdout reading position, including when a
    // late RichEdit zoom/reflow fails and its caller exits through an exception.
    struct OutputReadingRestorer {
        Impl& view;
        std::optional<OutputReadingAnchor> saved;
        explicit OutputReadingRestorer(Impl& owner)
            :view(owner),saved(owner.capture_output_reading_anchor()){}
        ~OutputReadingRestorer(){view.restore_output_reading_anchor(saved);}
        OutputReadingRestorer(const OutputReadingRestorer&)=delete;
        OutputReadingRestorer& operator=(const OutputReadingRestorer&)=delete;
    };
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
    // The frequency chart never exposes groups the active walkthrough has
    // not finished. A partial group is not a recorded histogram bar.
    std::size_t completed_groups()const {
        if(spec.view==SimulationViewMode::summary)return trace.groups.size();
        if(spec.view==SimulationViewMode::candidates)
            return trace.groups.empty()?0:std::min(trace.groups.size(),position+1);
        if(spec.view==SimulationViewMode::walkthrough)
            return static_cast<std::size_t>(
                std::upper_bound(trace.candidate_frames.begin(),
                                 trace.candidate_frames.end(),position)-
                trace.candidate_frames.begin());
        return 0;
    }
    // Distinguish the group examined in the current decision from the
    // candidates that reach the maximum. These are independent facts:
    // a losing group can be current without being a candidate.
    std::optional<std::size_t> current_decision_group()const {
        if(spec.view==SimulationViewMode::candidates){
            if(trace.candidate_frames.empty())return std::nullopt;
            return position;
        }
        if(spec.view!=SimulationViewMode::walkthrough)return std::nullopt;
        const auto& state=frame();
        if(state.phase!=SimulationPhase::new_maximum&&
           state.phase!=SimulationPhase::tie&&
           state.phase!=SimulationPhase::below_maximum)return std::nullopt;
        if(state.group==0||state.group>trace.groups.size())return std::nullopt;
        return state.group-1;
    }
    std::vector<std::size_t> frequency_indices()const {
        std::vector<std::size_t> indices;
        const auto available=completed_groups();
        indices.reserve(available);
        for(std::size_t i=0;i<available;++i){
            if(frequency_candidates_only&&
                std::ranges::find(frame().leaders,trace.groups[i].value)==frame().leaders.end())
                continue;
            indices.push_back(i);
        }
        return indices;
    }
    bool frequency_filter_useful()const {
        if(completed_groups()<2)return false;
        for(std::size_t i=0;i<completed_groups();++i)
            if(std::ranges::find(frame().leaders,trace.groups[i].value)==frame().leaders.end())
                return true;
        return false;
    }
    std::string frequency_text()const {
        const auto indices=frequency_indices();
        if(indices.empty())return {};
        std::string result="Frecuencias de "+std::to_string(indices.size())+
            (frequency_candidates_only?
                (indices.size()==1?" grupo candidato":" grupos candidatos"):
                (indices.size()==1?" grupo terminado":" grupos terminados"));
        if(frequency_candidates_only)result+=" entre "+std::to_string(completed_groups())+
            (completed_groups()==1?" grupo terminado":" grupos terminados");
        for(const auto i:indices){
            const auto& group=trace.groups[i];
            const bool candidate=std::ranges::find(frame().leaders,group.value)!=frame().leaders.end();
            result+=". Grupo "+std::to_string(i+1)+": "+simulation_value_display_text(group.value)+
                ", "+spec.labels.frequency+" "+std::to_string(group.end-group.begin);
            if(candidate)result+=", candidato al máximo actual";
            if(current_decision_group()==i)
                result+=", grupo de la decisión actual";
        }
        return result;
    }
    // Decision indices come exclusively from the immutable core trace.
    // Skip phases within a group without pretending they were executed by
    // the learner. The previous decision is always strictly before position.
    std::optional<std::size_t> previous_decision()const {
        if(spec.view!=SimulationViewMode::walkthrough)return std::nullopt;
        const auto& steps=trace.candidate_frames;
        const auto it=std::lower_bound(steps.begin(),steps.end(),position);
        if(it==steps.begin())return std::nullopt;
        return *std::prev(it);
    }
    std::optional<std::size_t> next_decision()const {
        if(spec.view!=SimulationViewMode::walkthrough)return std::nullopt;
        const auto& steps=trace.candidate_frames;
        const auto it=std::upper_bound(steps.begin(),steps.end(),position);
        if(it==steps.end())return std::nullopt;
        return *it;
    }
    HWND create(const wchar_t* type,int id,DWORD style=0) {
        const auto child=CreateWindowExW(0,type,L"",WS_CHILD|WS_VISIBLE|style,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!child)throw std::runtime_error("No se pudo crear un control de la simulación.");
        controls.emplace(id,child);reader_theme::apply(child,reader_theme::Surface::card);
        return child;
    }
    HWND label(int id) {
        const bool scrolling=id==output_id||id==explanation_id||id==original_order_id||id==transition_id||id==result_comparison_id;
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
        if(!set_rich_text_zoom(item(id),scale))
            throw std::runtime_error("RichEdit rechazó la escala del texto de simulación.");
    }
    void color_plain_text(int id) {
        CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;
        format.crTextColor=reader_theme::foreground(item(id));
        SendMessageW(item(id),EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
        SendMessageW(item(id),EM_SETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
    }
    void fonts(bool rebase=true,int requested=-1) {
        const int target=requested<0?scale:requested;
        // Stage fonts and RichEdit zoom before committing a new reader scale.
        // A late control can reject EM_SETZOOM after earlier controls accepted
        // it; no partial scale, lost HFONT or overwritten editor-local zoom
        // may escape that failure.
        std::array<HFONT,5> fresh{};
        const auto discard=[&] {for(const auto handle:fresh)if(handle)DeleteObject(handle);};
        const auto make=[&](int logical,bool scaled,const wchar_t* face,int weight=FW_NORMAL){
            const auto pixels=scaled?std::max(1,MulDiv(px(logical),target,100)):px(logical);
            const auto created=CreateFontW(-pixels,0,0,0,weight,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);
            if(!created)throw std::runtime_error("No se pudo preparar la tipografía de la simulación.");
            return created;
        };
        try {
            fresh[0]=make(18,true,L"Roboto");
            if(rebase)fresh[1]=make(18,false,L"Roboto");
            fresh[2]=make(23,true,L"Roboto",FW_BOLD);
            fresh[3]=make(13,true,L"Roboto");
            fresh[4]=make(30,true,L"Roboto",FW_BOLD);
        }catch(...){discard();throw;}
        const HFONT selected_base=rebase?fresh[1]:base_font;
        struct PreviousZoom {HWND child;int numerator,denominator;};
        std::vector<PreviousZoom> prior;
        try {
            prior.reserve(controls.size());
            for(const auto& [id,child]:controls) {
                wchar_t type[48]{};GetClassNameW(child,type,48);
                if(std::wstring_view(type)!=MSFTEDIT_CLASS)continue;
                int numerator=0,denominator=0;
                SendMessageW(child,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),
                    reinterpret_cast<LPARAM>(&denominator));
                prior.push_back({child,numerator,denominator});
                // Changing the base font is appropriate only after a DPI
                // rebase, not after ordinary reader Ctrl+wheel zoom.
                if(rebase){
                    SendMessageW(child,EM_SETZOOM,1,1);
                    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(selected_base),FALSE);
                }
                // Rebuilding base fonts for a new monitor is not a
                // request to change the student's independent editor zoom.
                // Reapply its exact previous native ratio after WM_SETFONT.
                const bool local_input=rebase&&id==input_id&&numerator>0&&denominator>0;
                const bool accepted=local_input?
                    SendMessageW(child,EM_SETZOOM,numerator,denominator)!=FALSE:
                    set_rich_text_zoom(child,target);
                if(!accepted)
                    throw std::runtime_error("RichEdit rechazó la escala de la simulación.");
            }
        }catch(...){
            // Restore the *actual* ratio of each control. The editable input
            // may have a local Ctrl+wheel scale different from reader scale.
            for(const auto& previous:prior)if(IsWindow(previous.child)){
                if(rebase){
                    SendMessageW(previous.child,EM_SETZOOM,1,1);
                    SendMessageW(previous.child,WM_SETFONT,reinterpret_cast<WPARAM>(base_font),FALSE);
                }
                SendMessageW(previous.child,EM_SETZOOM,
                    previous.numerator,previous.denominator);
            }
            // Even a rejected intermediate zoom may have requested native
            // RichEdit heights. Never reuse that provisional geometry.
            layout_valid=false;requested_heights.clear();
            discard();
            throw;
        }
        const std::array old{font,rebase?base_font:nullptr,heading_font,index_font,value_font};
        font=fresh[0];if(rebase)base_font=fresh[1];
        heading_font=fresh[2];index_font=fresh[3];value_font=fresh[4];
        scale=target;
        // A partial high-resolution Ctrl+wheel gesture belongs to the
        // previous editor scale. Do not combine it with a new reader-scale
        // transaction or DPI rebase; only committed changes reset the delta.
        input_wheel_delta=0;
        for(const auto& [id,child]:controls){
            wchar_t type[48]{};GetClassNameW(child,type,48);
            if(std::wstring_view(type)!=MSFTEDIT_CLASS)
                SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        }
        for(const auto handle:old)if(handle)DeleteObject(handle);
        layout_valid=false;requested_heights.clear();
    }
    void initialize(HWND parent) {
        register_class(view_class,procedure);register_class(array_class,array_procedure);
        register_class(statistics_class,statistics_procedure);
        register_class(frequency_class,frequencies_procedure);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_PROGRESS_CLASS};
        if(!InitCommonControlsEx(&common))
            throw std::runtime_error("No se pudo preparar el progreso del recorrido.");
        rich=LoadLibraryW(L"Msftedit.dll");if(!rich)throw std::runtime_error("RichEdit no está disponible.");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,view_class,L"Simulación interactiva",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,
            0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),this);
        if(!host)throw std::runtime_error("No se pudo abrir la simulación.");
        reader_theme::apply(host,reader_theme::Surface::card);fonts();
        (void)CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(accessibility.GetAddressOf()));
        for(const auto id:{title_id,description_id,scenario_label_id,array_label_id,original_order_id,
                step_id,explanation_id,transition_id,output_label_id,output_id,result_comparison_id,
                input_label_id,error_id,groups_label_id,frequencies_label_id,
                progress_label_id})label(id);
        const auto selector=create(L"COMBOBOX",scenario_id,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        SendMessageW(selector,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SendMessageW(selector,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Datos originales"));
        for(const auto& example:spec.scenarios){const auto text=wide(example.label);SendMessageW(selector,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
        SendMessageW(selector,CB_SETCURSEL,0,0);accessible(selector,"Escenario de la simulación");
        const auto step_selector=create(L"COMBOBOX",step_selector_id,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        SendMessageW(step_selector,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        accessible(step_selector,"Ir a un paso calculado de la simulación");
        const auto progress=create(PROGRESS_CLASSW,progress_id,PBS_SMOOTH);
        accessible(progress,"Progreso de posiciones recorridas en el array ordenado");
        canvas=CreateWindowExW(0,array_class,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(array_id)),GetModuleHandleW(nullptr),this);
        if(!canvas)throw std::runtime_error("No se pudo mostrar el array.");controls.emplace(array_id,canvas);
        const auto statistics_window=CreateWindowExW(0,statistics_class,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(variables_id)),GetModuleHandleW(nullptr),this);
        if(!statistics_window)throw std::runtime_error("No se pudieron mostrar las variables.");controls.emplace(variables_id,statistics_window);
        frequencies=CreateWindowExW(0,frequency_class,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,host,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(frequencies_id)),GetModuleHandleW(nullptr),this);
        if(!frequencies)throw std::runtime_error("No se pudieron mostrar las frecuencias.");
        controls.emplace(frequencies_id,frequencies);
        SetWindowPos(item(explanation_id),canvas,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(item(output_id),statistics_window,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        button(previous_id,"Anterior");button(next_id,"Siguiente");button(reset_id,"Reiniciar");
        button(previous_decision_id,"Decisión anterior");button(next_decision_id,"Siguiente decisión");
        button(frequency_filter_id,"Mostrar solo candidatos");
        reader_theme::primary_button(item(next_id));
        const auto format=create(L"COMBOBOX",input_format_id,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        SendMessageW(format,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        SendMessageW(format,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Un texto por línea"));
        SendMessageW(format,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Lista entre comillas"));
        SendMessageW(format,CB_SETCURSEL,0,0);accessible(format,"Formato de los datos editables");
        const auto editor=create(MSFTEDIT_CLASS,input_id,WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);
        reader_theme::apply(editor,reader_theme::Surface::field);use_plain_text_input(editor);
        // RichEdit's native Tab and Ctrl+wheel behavior can vary. Keep
        // text editing native, while consistently enforcing focus and zoom.
        if(!SetWindowSubclass(editor,input_navigation_proc,3,reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("No se pudieron preparar los controles del editor de datos.");
        SendMessageW(editor,WM_SETFONT,reinterpret_cast<WPARAM>(base_font),FALSE);
        if(!set_rich_text_zoom(editor,scale))
            throw std::runtime_error("RichEdit rechazó la escala inicial de los datos.");
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
    // The comparison uses applied data, never an unsubmitted editor draft.
    std::string original_order_text()const {
        if(!spec.sort||trace.original==trace.ordered)return {};
        std::string result="Entrada aplicada antes de ordenar (sin modificar):";
        for(std::size_t index=0;index<trace.original.size();++index){
            result+='\n';
            result+="índice "+std::to_string(index)+": "+simulation_value_display_text(trace.original[index]);
        }
        return result;
    }
    // Only start and advance count a new array element. The decision phases
    // compare a finished group and must never appear as another element read.
    std::optional<std::size_t> counted_cell()const {
        if(spec.view!=SimulationViewMode::walkthrough)return std::nullopt;
        const auto& step=frame();
        if((step.phase!=SimulationPhase::start&&step.phase!=SimulationPhase::advance)||
                step.end<=step.begin||step.end>trace.ordered.size())return std::nullopt;
        return step.end-1;
    }
    std::string array_text()const {
        std::string text=spec.labels.array+". ";
        if(trace.ordered.empty())text+="No hay valores.";
        for(std::size_t i=0;i<trace.ordered.size();++i){
            if(i)text+="; ";
            text+="índice "+std::to_string(i)+": "+simulation_value_display_text(trace.ordered[i]);
            if(!source_positions.empty())text+=" (origen índice "+std::to_string(source_positions[i])+")";
        }
        const auto& state=frame();
        if(const auto counted=counted_cell())
            text+=". Casilla contada ahora: índice "+std::to_string(*counted)+
                " = "+simulation_value_display_text(trace.ordered.at(*counted))+".";
        if(spec.view==SimulationViewMode::array){for(std::size_t i=0;i<trace.groups.size();++i){const auto& group=trace.groups[i];
            text+=". Grupo "+std::to_string(i+1)+": índices "+std::to_string(group.begin)+" a "+std::to_string(group.end-1)+".";}}
        else if(spec.view!=SimulationViewMode::summary){
            if(state.begin<state.end)text+=". Intervalo resaltado ["+std::to_string(state.begin)+", "+std::to_string(state.end)+").";
            text+=". "+spec.labels.start+" = "+std::to_string(state.begin)+"; "+spec.labels.end+" = "+std::to_string(state.end)+" (límite exclusivo).";}
        return text;
    }
    std::string values_text(const std::vector<SimulationValue>& values)const {
        if(values.empty())return "ninguno";
        std::string result;
        for(std::size_t i=0;i<values.size();++i){
            if(i)result+=", ";
            result+=simulation_value_display_text(values[i]);
        }
        return result;
    }
    std::string leader_text(const SimulationFrame& state)const{return values_text(state.leaders);}
    // Show comparisons only after the active model reaches its final state.
    // Before that, even a scenario with known authored data cannot reveal
    // a future winner or number of groups to the learner.
    std::string result_comparison_text()const {
        if(spec.view==SimulationViewMode::array||trace.original==spec.values)return {};
        const bool completed=spec.view==SimulationViewMode::summary||
            (spec.view==SimulationViewMode::walkthrough&&position+1==count())||
            ((spec.view==SimulationViewMode::groups||spec.view==SimulationViewMode::candidates)&&
                (trace.groups.empty()||position+1==count()));
        if(!completed)return {};
        const auto& current=trace.frames.back();
        std::string result="Comparación de resultados: datos iniciales → datos aplicados";
        const auto add=[&](std::string_view name,std::string left,std::string right){
            result+='\n';result+=name;result+=": "+left+" → "+right;
            if(left==right)result+=" (sin cambio)";
        };
        add("Cantidad de datos",std::to_string(spec.values.size()),std::to_string(trace.original.size()));
        add("Cantidad de grupos",std::to_string(authored_group_count),std::to_string(trace.groups.size()));
        add(spec.labels.maximum,std::to_string(authored_maximum),std::to_string(current.maximum));
        add(spec.labels.leaders,values_text(authored_leaders),values_text(current.leaders));
        return result;
    }
    // A phase transition is derived only from validated, already-calculated
    // frames. Direct jumps still compare adjacent *model* states instead of
    // accidentally comparing the last state visited by the learner.
    std::string transition_text()const {
        if(spec.view==SimulationViewMode::array||spec.view==SimulationViewMode::summary)return {};
        if(spec.view!=SimulationViewMode::walkthrough&&trace.candidate_frames.empty())return {};
        const SimulationFrame* before=nullptr;
        if(spec.view==SimulationViewMode::walkthrough){
            if(position==0)return {};
            before=&trace.frames.at(position-1);
        }else if(spec.view==SimulationViewMode::groups||spec.view==SimulationViewMode::candidates)
            before=position?&trace.frames.at(trace.candidate_frames.at(position-1)):&trace.frames.front();
        if(!before)return {};
        const auto& after=frame();
        std::vector<std::string> changes;
        // The model counts the element at end-1. A completed group compares
        // its frequency with the maximum *before* that group's decision.
        // Derive both statements from actual frames, never authored prose.
        if(after.phase==SimulationPhase::start||after.phase==SimulationPhase::advance){
            const auto observed=after.end-1;
            const auto& current_value=trace.ordered.at(observed);
            changes.push_back("Dato contado: índice "+std::to_string(observed)+" = "+
                simulation_value_display_text(current_value));
            if(observed==0){
                changes.push_back("Primera posición: el índice 0 no tiene un elemento anterior.");
            }else{
                // Both values have already been observed in the immutable
                // computed trace. Never compare against the NEXT element
                // or claim the exclusive 'end' boundary is an array index.
                const auto& previous_value=trace.ordered.at(observed-1);
                const bool equal=previous_value==current_value;
                changes.push_back("Relación con el anterior: índice "+
                    std::to_string(observed-1)+" = "+
                    simulation_value_display_text(previous_value)+
                    "; índice "+std::to_string(observed)+" = "+
                    simulation_value_display_text(current_value)+
                    (equal?". Son iguales: continúa el grupo.":
                        ". Son diferentes: comienza un grupo nuevo."));
            }
        }else if(after.phase==SimulationPhase::new_maximum||
                 after.phase==SimulationPhase::tie||
                 after.phase==SimulationPhase::below_maximum){
            const char* relation=after.phase==SimulationPhase::new_maximum?">":
                after.phase==SimulationPhase::tie?"=":"<";
            const char* consequence=after.phase==SimulationPhase::new_maximum?
                "Nuevo máximo: reemplaza candidatos.":
                after.phase==SimulationPhase::tie?
                "Empate: agrega el valor a los candidatos.":
                "No supera el máximo: conserva los candidatos.";
            changes.push_back("Comparación: "+spec.labels.frequency+" "+
                std::to_string(after.frequency)+" "+relation+" "+spec.labels.maximum+
                " anterior "+std::to_string(before->maximum)+". "+consequence);
        }
        const auto number=[&](std::string_view label,std::size_t previous,std::size_t current){
            if(previous!=current)
                changes.push_back(std::string(label)+": "+std::to_string(previous)+" → "+std::to_string(current));
        };
        number("grupo",before->group,after.group);
        number(spec.labels.start,before->begin,after.begin);
        number(spec.labels.end,before->end,after.end);
        number(spec.labels.frequency,before->frequency,after.frequency);
        number(spec.labels.maximum,before->maximum,after.maximum);
        if(before->leaders!=after.leaders)
            changes.push_back(spec.labels.leaders+": "+leader_text(*before)+" → "+leader_text(after));
        if(before->output!=after.output){
            // The core appends each group's output. Show the new line count,
            // not the complete output of another or future frame.
            if(after.output.size()>=before->output.size()&&
                    after.output.compare(0,before->output.size(),before->output)==0){
                const auto from=after.output.begin()+
                    static_cast<std::string::difference_type>(before->output.size());
                const auto lines=std::count(from,after.output.end(),'\n');
                changes.push_back(spec.labels.output+": +"+std::to_string(lines)+
                    (lines==1?" línea":" líneas"));
            }else changes.push_back(spec.labels.output+": actualizada");
        }
        if(changes.empty())return {};
        std::string result="Cambios desde el estado anterior:";
        for(const auto& change:changes)result+='\n'+change;
        return result;
    }
    void update() {
        layout_valid=false;requested_heights.clear();
        const auto& state=frame();
        set_label(title_id,spec.title,RichTextRole::heading,semantic("/title"));
        set_label(description_id,spec.description,RichTextRole::body,semantic("/description"));
        set_label(scenario_label_id,"Escenario");
        const auto original=original_order_text();
        set_label(array_label_id,spec.labels.array+(original.empty()?"":" · copia ordenada")+
            " · "+std::to_string(trace.ordered.size())+" elementos");
        // This source comparison remains the same across all steps of one
        // trace. Streaming it again on every Next/Previous loses the student's
        // native RichEdit selection and the line they were examining.
        const auto original_window=item(original_order_id);
        const auto previous_original=visible_text.find(original_order_id);
        const bool same_original_session=previous_original!=visible_text.end()&&
            original_order_model_key==input_key&&original_order_scenario==scenario;
        const auto current_ink=reader_theme::foreground(original_window);
        const bool original_unchanged=same_original_session&&
            previous_original->second==original&&original_order_ink==current_ink;
        CHARRANGE original_selection{};
        POINT original_scroll{};
        if(same_original_session&&!original_unchanged){
            SendMessageW(original_window,EM_EXGETSEL,0,
                reinterpret_cast<LPARAM>(&original_selection));
            SendMessageW(original_window,EM_GETSCROLLPOS,0,
                reinterpret_cast<LPARAM>(&original_scroll));
        }
        if(!original_unchanged)set_label(original_order_id,original);
        set_label(groups_label_id,"Selecciona un grupo");
        set_label(frequencies_label_id,std::string(frequency_candidates_only?
            "Frecuencias de candidatos":"Frecuencias de grupos terminados")+
            " · escala máxima visible "+std::to_string(std::max<std::size_t>(1,frame().maximum)));
        SetWindowTextW(item(frequency_filter_id),wide(
            frequency_candidates_only?"Mostrar todos los grupos":"Mostrar solo candidatos").c_str());
        accessible(item(frequency_filter_id),frequency_candidates_only?
            "Mostrar las barras de todos los grupos terminados":
            "Filtrar barras para mostrar sólo candidatos al máximo actual");
        const auto frequency_description=frequency_text();
        SetWindowTextW(frequencies,wide(frequency_description).c_str());
        accessible(frequencies,frequency_description);
        // The filter changes the accessible name without moving the model
        // step. Notify native readers as with the array and variable cards.
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,frequencies,OBJID_CLIENT,CHILDID_SELF);
        std::string status;
        if(stepping())status=(spec.view==SimulationViewMode::candidates?"Grupo ":"Paso ")+std::to_string(position+1)+" de "+std::to_string(count());
        else if(spec.view==SimulationViewMode::groups)status=trace.groups.empty()?"No hay grupos":"Grupo "+std::to_string(position+1)+" de "+std::to_string(trace.groups.size());
        else if(spec.view==SimulationViewMode::summary)status="Resumen de los grupos";
        set_label(step_id,status);set_label(explanation_id,state.explanation);
        set_label(transition_id,transition_text());
        // end is an exclusive boundary. In walkthrough, it also equals the
        // number of elements already observed across completed/partial runs.
        // It is not the frequency of the current group and never reads data[end].
        const bool show_progress=spec.view==SimulationViewMode::walkthrough&&
            !trace.ordered.empty();
        const auto progress_text=show_progress?
            "Elementos recorridos: "+std::to_string(state.end)+" de "+
                std::to_string(trace.ordered.size()):std::string{};
        set_label(progress_label_id,progress_text);
        SendMessageW(item(progress_id),PBM_SETRANGE32,0,
            static_cast<LPARAM>(trace.ordered.size()));
        SendMessageW(item(progress_id),PBM_SETPOS,
            static_cast<WPARAM>(show_progress?state.end:0),0);
        accessible(item(progress_id),progress_text.empty()?
            "No hay elementos para recorrer":progress_text);
        if(show_progress)
            NotifyWinEvent(EVENT_OBJECT_VALUECHANGE,item(progress_id),
                OBJID_CLIENT,CHILDID_SELF);
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
        set_label(result_comparison_id,result_comparison_text());
        set_label(output_label_id,spec.labels.output+(state.output.empty()?" (vacía)":""));
        // Counting another element often leaves accumulated stdout unchanged.
        // Replacing that same text resets RichEdit's scroll/caret and sends
        // needless change events, so retain the native document in place.
        const auto output_window=item(output_id);
        const auto previous_output=visible_text.find(output_id);
        const bool output_changed=previous_output==visible_text.end()||
            previous_output->second!=state.output;
        // A new scenario can produce exactly the same stdout from different
        // source data. Its scroll and text selection still belong to a new
        // reading session and must not inherit the earlier scenario's caret.
        const bool same_output_model=previous_output!=visible_text.end()&&
            output_model_key==input_key&&output_scenario==scenario;
        POINT output_scroll{};
        const bool restore_output_scroll=same_output_model;
        bool follow_output_end=false;
        CHARRANGE output_selection{};
        const bool preserve_selection=restore_output_scroll&&output_changed&&
            (state.output.starts_with(previous_output->second)||
             previous_output->second.starts_with(state.output));
        if(preserve_selection)
            SendMessageW(output_window,EM_EXGETSEL,0,
                reinterpret_cast<LPARAM>(&output_selection));
        if(restore_output_scroll){
            // Even recoloring unchanged text for a theme update may reflow
            // native RichEdit; retain the learner's actual viewport.
            SendMessageW(output_window,EM_GETSCROLLPOS,0,
                reinterpret_cast<LPARAM>(&output_scroll));
            if(output_changed){
                SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS};
                const bool at_end=GetScrollInfo(output_window,SB_VERT,&scroll)&&
                    scroll.nPage>0&&
                    scroll.nPos>=std::max(scroll.nMin,
                        scroll.nMax-std::max(0,static_cast<int>(scroll.nPage)-1));
                // Follow a new line only when the learner was already at the
                // bottom. Otherwise preserve their earlier reading position.
                follow_output_end=at_end&&state.output.size()>previous_output->second.size()&&
                    state.output.compare(0,previous_output->second.size(),previous_output->second)==0;
            }
        }
        visible_text[output_id]=state.output;
        if(output_changed){
            set_edit_text(output_window,state.output);
            if(preserve_selection){
                // RichEdit STREAMIN recreates the document and its selection.
                // Retain the selection only within the same calculated trace.
                const LONG length=static_cast<LONG>(GetWindowTextLengthW(output_window));
                const CHARRANGE restored{
                    std::clamp(output_selection.cpMin,LONG{0},length),
                    std::clamp(output_selection.cpMax,LONG{0},length)};
                SendMessageW(output_window,EM_EXSETSEL,0,
                    reinterpret_cast<LPARAM>(&restored));
            }
        }
        color_plain_text(output_id);
        if(!set_rich_text_zoom(output_window,scale))
            throw std::runtime_error("RichEdit rechazó la escala de la salida.");
        color_plain_text(input_id);
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
        EnableWindow(item(previous_decision_id),previous_decision().has_value());
        EnableWindow(item(next_decision_id),next_decision().has_value());
        for(std::size_t i=0;i<group_buttons.size();++i)SendMessageW(group_buttons[i],BM_SETCHECK,i==position?BST_CHECKED:BST_UNCHECKED,0);
        const auto description=array_text();SetWindowTextW(canvas,wide(description).c_str());accessible(canvas,description);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,canvas,OBJID_CLIENT,CHILDID_SELF);
        if(!initializing)arrange(width,true);
        if(same_original_session&&!original_unchanged){
            // A palette change needs fresh formatting, but must not become a
            // new reading session for unchanged applied data.
            const LONG length=static_cast<LONG>(GetWindowTextLengthW(original_window));
            const CHARRANGE restored{
                std::clamp(original_selection.cpMin,LONG{0},length),
                std::clamp(original_selection.cpMax,LONG{0},length)};
            SendMessageW(original_window,EM_EXSETSEL,0,
                reinterpret_cast<LPARAM>(&restored));
            SendMessageW(original_window,EM_SETSCROLLPOS,0,
                reinterpret_cast<LPARAM>(&original_scroll));
        }else if(!same_original_session&&previous_original!=visible_text.end()){
            // Two scenarios may carry the same values and display identical
            // source text, yet their reading sessions are independent.
            const CHARRANGE start{0,0};
            SendMessageW(original_window,EM_EXSETSEL,0,
                reinterpret_cast<LPARAM>(&start));
            SendMessageW(original_window,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
        }
        original_order_model_key=input_key;
        original_order_scenario=scenario;
        original_order_ink=current_ink;
        if(restore_output_scroll){
            if(follow_output_end)
                SendMessageW(output_window,WM_VSCROLL,MAKEWPARAM(SB_BOTTOM,0),0);
            else
                SendMessageW(output_window,EM_SETSCROLLPOS,0,
                    reinterpret_cast<LPARAM>(&output_scroll));
        }else if(previous_output!=visible_text.end()){
            const CHARRANGE start{0,0};
            SendMessageW(output_window,EM_EXSETSEL,0,
                reinterpret_cast<LPARAM>(&start));
            SendMessageW(output_window,WM_VSCROLL,MAKEWPARAM(SB_TOP,0),0);
        }
        output_model_key=input_key;
        output_scenario=scenario;
        InvalidateRect(canvas,nullptr,FALSE);InvalidateRect(item(variables_id),nullptr,FALSE);
        InvalidateRect(frequencies,nullptr,FALSE);
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
    void replace_trace(SimulationTrace candidate,bool replace_editor) {
        // Only fully validated traces reach this commit point. In particular,
        // parsed editor input already had its expansion checked by the core.
        auto next_source_positions=stable_source_positions(candidate);
        auto identity=data_identity(candidate.original);
        auto next_format=input_format;
        std::optional<std::string> replacement;
        if(replace_editor){
            if(spec.text_values&&std::ranges::any_of(candidate.original,[](const auto& value){
                const auto* text=std::get_if<std::string>(&value);return text&&text->empty();}))
                next_format=SimulationInputFormat::json;
            replacement=simulation_input_text(candidate.original,next_format);
        }
        trace=std::move(candidate);source_positions=std::move(next_source_positions);
        // A partial Ctrl+wheel movement belongs to the previous edit state.
        input_wheel_delta=0;
        frequency_scroll=0;frequency_total_height=0;frequency_candidates_only=false;
        pressed_frequency_group.reset();
        input_key=std::move(identity);position=0;error.clear();
        if(replacement){input_format=next_format;set_edit_text(item(input_id),*replacement);}
        rebuild_groups();rebuild_steps();update();publish();
    }
    void replace_data(std::vector<SimulationValue> values,bool replace_editor) {
        replace_trace(build_simulation_trace(spec,std::move(values)),replace_editor);
    }
    void apply_input() {
        try {
            // Parsing already builds and validates every frame. Reuse it,
            // rather than computing all authored scenarios and the edit twice.
            auto candidate=simulation_parse_input_trace(spec,edit_text(item(input_id)),input_format);
            replace_trace(std::move(candidate),false);
        }catch(const std::exception& failure){
            error=std::string("No se aplicaron los datos. ")+failure.what();update();publish();
        }
    }
    void change_input_format(SimulationInputFormat requested) {
        if(requested==input_format)return;
        input_wheel_delta=0;
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
        pressed_target.reset();cell_rects.clear();markers.clear();limit_text.clear();limit_bounds={};
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
            const auto last=std::min(trace.ordered.size(),first+static_cast<std::size_t>(columns));
            const auto origin_height=source_positions.empty()?0:sp(22);
            const auto counted_height=spec.view==SimulationViewMode::walkthrough?sp(22):0;
            int row=sp(spec.view==SimulationViewMode::array?88:68)+origin_height+counted_height,marker_height=0;
            for(auto i=first;i<last;++i){const auto dc=GetDC(host);const auto old=SelectObject(dc,font);
                const auto content=wrapped_text(dc,simulation_value_display_text(trace.ordered[i]),std::max(1,cell_width-sp(16)));
                RECT bounds{0,0,std::max(1,cell_width-sp(16)),0};DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                SelectObject(dc,old);ReleaseDC(host,dc);
                row=std::max(row,static_cast<int>(bounds.bottom)+
                    sp(spec.view==SimulationViewMode::array?62:42)+origin_height+counted_height);
                if(const auto caption=marker_text(i);!caption.empty())marker_height=std::max(marker_height,measure(caption,cell_width,index_font)+sp(8));}
            for(auto i=first;i<last;++i){const auto x=static_cast<int>(i-first)*(cell_width+gap);
                const auto caption=marker_text(i);if(!caption.empty())markers.push_back({{x,top,x+cell_width,top+marker_height-sp(8)},caption,{x+cell_width/2,top+marker_height}});
                cell_rects.push_back({x,top+marker_height,x+cell_width,top+marker_height+row});}
            top+=marker_height+row+gap;
        }
        if(!limit_text.empty()){const auto limit_height=measure(limit_text,available,index_font);limit_bounds={0,top+sp(5),available,top+sp(5)+limit_height};top=limit_bounds.bottom+sp(4);}
        return top;
    }
    int frequency_columns(int available,std::size_t visible)const {
        const int gap=sp(8);
        return std::max(1,std::min(static_cast<int>(visible),
            std::max(1,(available+gap)/(sp(100)+gap))));
    }
    int frequency_height(int available,std::size_t visible)const {
        if(!visible)return 0;
        const auto columns=static_cast<std::size_t>(frequency_columns(available,visible));
        return sp(8)+static_cast<int>((visible+columns-1)/columns)*sp(138)+sp(4);
    }
    void scroll_frequencies(int requested) {
        if(!frequencies||!frequency_total_height)return;
        SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE|SIF_POS};
        if(!GetScrollInfo(frequencies,SB_VERT,&range))return;
        const int limit=std::max(range.nMin,range.nMax-
            std::max(0,static_cast<int>(range.nPage)-1));
        const int next=std::clamp(requested,range.nMin,limit);
        if(next==range.nPos)return;
        SCROLLINFO changed{sizeof(changed),SIF_POS};
        changed.nPos=next;
        SetScrollInfo(frequencies,SB_VERT,&changed,TRUE);
        SCROLLINFO actual{sizeof(actual),SIF_POS};
        GetScrollInfo(frequencies,SB_VERT,&actual);
        frequency_scroll=actual.nPos;
        InvalidateRect(frequencies,nullptr,FALSE);
    }
    // Use one geometry calculation for painting, hit testing and native
    // regression tests. Slots map to original group indices through
    // frequency_indices(), also after selecting the candidate-only filter.
    RECT frequency_card_rect(std::size_t slot,std::size_t visible,int client_width)const {
        const int gap=sp(8);
        const int columns=frequency_columns(std::max(1,client_width),visible);
        const int card_width=std::max(1,(client_width-gap*(columns-1))/columns);
        const int row=static_cast<int>(slot/static_cast<std::size_t>(columns));
        const int column=static_cast<int>(slot%static_cast<std::size_t>(columns));
        const int left=column*(card_width+gap);
        const int top=sp(8)+row*sp(138)-frequency_scroll;
        return {left,top,left+card_width,top+sp(130)};
    }
    std::optional<RECT> frequency_bounds_in_canvas(std::size_t group_index)const {
        if(!frequencies||!(GetWindowLongPtrW(frequencies,GWL_STYLE)&WS_VISIBLE))
            return std::nullopt;
        const auto indices=frequency_indices();
        const auto it=std::ranges::find(indices,group_index);
        if(it==indices.end())return std::nullopt;
        RECT viewport{};GetClientRect(frequencies,&viewport);
        const auto card=frequency_card_rect(
            static_cast<std::size_t>(it-indices.begin()),indices.size(),viewport.right);
        RECT clipped{};
        if(!IntersectRect(&clipped,&card,&viewport))return std::nullopt;
        return clipped;
    }
    std::optional<std::size_t> frequency_target_at(POINT point)const {
        // Summary has no navigable state; the histogram is read-only there.
        if(spec.view!=SimulationViewMode::walkthrough&&
                spec.view!=SimulationViewMode::candidates)return std::nullopt;
        RECT viewport{};GetClientRect(frequencies,&viewport);
        if(!PtInRect(&viewport,point))return std::nullopt;
        const auto indices=frequency_indices();
        for(std::size_t slot=0;slot<indices.size();++slot){
            const auto card=frequency_card_rect(slot,indices.size(),viewport.right);
            if(PtInRect(&card,point))return indices[slot];
        }
        return std::nullopt;
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
            const bool scrolling=id==output_id||id==explanation_id||id==original_order_id||id==transition_id||id==result_comparison_id;
            const auto limit=scrolling?sp(id==original_order_id?180:id==transition_id?220:id==result_comparison_id?230:id==output_id?240:400):std::numeric_limits<int>::max();
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
        text_row(original_order_id);
        canvas_height=array_layout(available);place(array_id,margin,top,available,canvas_height,true);top+=canvas_height+px(12);
        text_row(step_id);
        const bool show_progress=spec.view==SimulationViewMode::walkthrough&&
            !trace.ordered.empty();
        text_row(progress_label_id,show_progress);
        place(progress_id,margin,top,available,sp(18),show_progress);
        if(show_progress)top+=sp(18)+px(8);
        place(step_selector_id,margin,top,available,sp(240),stepping());
        if(stepping())top+=sp(38)+px(8);
        text_row(explanation_id);text_row(transition_id);
        const bool stats=spec.view!=SimulationViewMode::array;
        const auto stats_height=statistics_layout(available);place(variables_id,margin,top,available,stats_height,stats);if(stats)top+=stats_height+px(8);
        const auto groups_done=completed_groups();
        const auto displayed=frequency_indices().size();
        text_row(frequencies_label_id,groups_done>0);
        const bool filter=groups_done>0&&
            (frequency_filter_useful()||frequency_candidates_only);
        place(frequency_filter_id,margin,top,available,sp(44),filter);
        if(filter)top+=sp(44)+px(8);
        if(groups_done){
            // A large histogram must not turn the whole study page into a
            // many-thousand-pixel chart. Show a viewport with an internal
            // scrollbar, while keeping every group's accessible description.
            const auto full=frequency_height(available,displayed);
            const int max_viewport=sp(440);
            const bool overflow=full>max_viewport;
            if(move){
                ShowScrollBar(frequencies,SB_VERT,overflow);
                const auto style=GetWindowLongPtrW(frequencies,GWL_STYLE);
                SetWindowLongPtrW(frequencies,GWL_STYLE,
                    overflow?style|WS_TABSTOP:style&~WS_TABSTOP);
            }
            const int viewport=std::min(full,max_viewport);
            place(frequencies_id,margin,top,available,viewport,true);
            if(move){
                RECT client{};GetClientRect(frequencies,&client);
                // Scrollbar presence can reduce the client width by a column.
                // Recompute the full height after Windows applies the style.
                frequency_total_height=frequency_height(std::max(1,static_cast<int>(client.right)),displayed);
                SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS};
                scroll.nMin=0;scroll.nMax=std::max(0,frequency_total_height-1);
                scroll.nPage=static_cast<UINT>(std::max(1L,client.bottom));
                scroll.nPos=frequency_scroll;
                SetScrollInfo(frequencies,SB_VERT,&scroll,TRUE);
                SCROLLINFO selected{sizeof(selected),SIF_POS};
                GetScrollInfo(frequencies,SB_VERT,&selected);
                frequency_scroll=selected.nPos;
                InvalidateRect(frequencies,nullptr,FALSE);
            }
            top+=viewport+px(8);
        }else{
            if(move){
                frequency_scroll=0;frequency_total_height=0;
                ShowScrollBar(frequencies,SB_VERT,FALSE);
                const auto style=GetWindowLongPtrW(frequencies,GWL_STYLE);
                SetWindowLongPtrW(frequencies,GWL_STYLE,style&~WS_TABSTOP);
            }
            place(frequencies_id,0,0,1,1,false);
        }
        const bool output=spec.view==SimulationViewMode::walkthrough||spec.view==SimulationViewMode::candidates||spec.view==SimulationViewMode::summary;
        text_row(output_label_id,output);text_row(output_id,output,sp(34));
        text_row(result_comparison_id);
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
        buttons({previous_decision_id,next_decision_id},
            spec.view==SimulationViewMode::walkthrough&&!trace.candidate_frames.empty());
        text_row(input_label_id,spec.editable);
        const bool text_format=spec.editable&&spec.text_values;
        place(input_format_id,margin,top,available,sp(150),text_format);if(text_format)top+=sp(38)+px(8);
        place(input_id,margin,top,available,sp(116),spec.editable);if(spec.editable)top+=sp(116)+px(8);
        buttons({apply_id,restore_id},spec.editable);text_row(error_id,!error.empty());
        height=top+px(8);layout_valid=move;return height;
    }
    int measured_height(int requested) {
        requested=std::max(1,requested);
        if(layout_valid&&requested==width)return height;
        // The parent may measure/reflow this child before sending WM_SIZE.
        // Keep the learner on the same output line, even on failed reflow.
        OutputReadingRestorer reading{*this};
        return arrange(requested,true);
    }
    // Use rendered cell rectangles, regardless of text scale or row wrapping.
    // The walkthrough frame represents a counted element, never a future
    // maximum/tie decision. Group views retain their existing selection.
    std::optional<std::size_t> target_at(POINT point)const {
        if(spec.view==SimulationViewMode::array||spec.view==SimulationViewMode::summary)return std::nullopt;
        for(std::size_t cell=0;cell<cell_rects.size();++cell){
            if(!PtInRect(&cell_rects[cell],point))continue;
            if(spec.view==SimulationViewMode::walkthrough){
                const auto observed=std::ranges::find_if(trace.frames,[cell](const SimulationFrame& frame){
                    return (frame.phase==SimulationPhase::start||frame.phase==SimulationPhase::advance)&&
                        frame.end==cell+1&&frame.begin<=cell;
                });
                if(observed!=trace.frames.end())
                    return static_cast<std::size_t>(observed-trace.frames.begin());
                return std::nullopt;
            }
            const auto group=std::ranges::find_if(trace.groups,[cell](const SimulationGroup& run){
                return cell>=run.begin&&cell<run.end;});
            if(group!=trace.groups.end())return static_cast<std::size_t>(group-trace.groups.begin());
            return std::nullopt;
        }
        return std::nullopt;
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
        const auto observed=counted_cell();
        const auto counted_footer=spec.view==SimulationViewMode::walkthrough?sp(22):0;
        for(std::size_t i=0;i<cell_rects.size();++i){
            const bool counted=observed&&*observed==i;
            const auto active=selected(i);auto bounds=cell_rects[i];
            const auto group=spec.view==SimulationViewMode::array?
                static_cast<std::size_t>(std::ranges::find_if(trace.groups,[&](const auto& value){return i>=value.begin&&i<value.end;})-trace.groups.begin()):selected_group;
            const auto tint=colors.high_contrast?colors.accent:group_colors[group%group_colors.size()];
            const auto background=active?(colors.high_contrast?colors.accent:blend(colors.card,tint,70)):colors.field;
            const auto brush=CreateSolidBrush(background);
            const auto pen=CreatePen(PS_SOLID,
                counted?std::max(2,sp(3)):active?std::max(1,px(2)):1,
                counted?(colors.high_contrast?colors.text:colors.accent):
                    active?tint:colors.outline);
            const auto old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,pen);
            RoundRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom,px(10),px(10));SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);DeleteObject(brush);
            const auto foreground=colors.high_contrast&&active?GetSysColor(COLOR_HIGHLIGHTTEXT):colors.text;
            auto index_area=bounds;index_area.left+=sp(6);index_area.right-=sp(6);index_area.top+=sp(5);index_area.bottom=index_area.top+sp(21);
            auto old=SelectObject(dc,index_font);SetTextColor(dc,foreground);
            const auto index=wide("índice "+std::to_string(i));DrawTextW(dc,index.c_str(),-1,&index_area,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            auto value_area=bounds;value_area.left+=sp(8);value_area.right-=sp(8);value_area.top=index_area.bottom+sp(3);
            value_area.bottom-=sp(spec.view==SimulationViewMode::array?28:8)+
                (source_positions.empty()?0:sp(22))+counted_footer;
            SelectObject(dc,font);const auto value=wrapped_text(dc,simulation_value_display_text(trace.ordered[i]),value_area.right-value_area.left);
            DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&value_area,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
            if(!source_positions.empty()){
                auto origin=bounds;origin.left+=sp(4);origin.right-=sp(4);
                origin.bottom-=sp(spec.view==SimulationViewMode::array?27:4)+counted_footer;
                origin.top=origin.bottom-sp(20);
                SelectObject(dc,index_font);
                const auto label=wide("origen "+std::to_string(source_positions[i]));
                DrawTextW(dc,label.c_str(),-1,&origin,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            }
            if(counted){
                // Text identifies the counted cell independently of color;
                // a footer reserved for all walkthrough frames prevents
                // clipping when the author uses long labels or source indices.
                auto caption=bounds;caption.left+=sp(5);caption.right-=sp(5);
                caption.bottom-=sp(4);caption.top=caption.bottom-sp(20);
                SelectObject(dc,index_font);SetTextColor(dc,foreground);
                DrawTextW(dc,L"contado",7,&caption,
                    DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
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
    void paint_frequencies(HDC dc) {
        RECT area{};GetClientRect(frequencies,&area);
        const auto colors=reader_theme::palette(host);
        FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::card));
        SetBkMode(dc,TRANSPARENT);
        const auto indices=frequency_indices();
        const auto visible=indices.size();
        if(!visible)return;
        std::size_t greatest=1;
        for(const auto i:indices)
            greatest=std::max(greatest,trace.groups[i].end-trace.groups[i].begin);
        for(std::size_t i=0;i<visible;++i){
            const RECT card=frequency_card_rect(i,visible,area.right);
            if(card.bottom<=0||card.top>=area.bottom)continue;
            const int left=card.left,top=card.top,card_width=card.right-card.left;
            const auto group_index=indices[i];
            const auto& group=trace.groups[group_index];
            const auto count=group.end-group.begin;
            const bool leader=std::ranges::find(frame().leaders,group.value)!=frame().leaders.end();
            const bool current=current_decision_group()==group_index;
            const auto card_brush=CreateSolidBrush(colors.field);
            const auto border=CreatePen(PS_SOLID,
                current?std::max(2,sp(3)):leader?std::max(1,px(2)):1,
                current?colors.text:leader?colors.accent:colors.outline);
            const auto old_brush=SelectObject(dc,card_brush),old_pen=SelectObject(dc,border);
            RoundRect(dc,card.left,card.top,card.right,card.bottom,px(10),px(10));
            SelectObject(dc,old_pen);SelectObject(dc,old_brush);
            DeleteObject(border);DeleteObject(card_brush);

            auto caption=card;caption.left+=sp(4);caption.right-=sp(4);
            caption.top+=sp(5);caption.bottom=caption.top+sp(20);
            auto old_font=SelectObject(dc,index_font);
            SetTextColor(dc,colors.text);
            const auto headline=wide(std::string(current?"Actual · ":"")+
                (leader?"★ ":"")+"Grupo "+std::to_string(group_index+1));
            DrawTextW(dc,headline.c_str(),-1,&caption,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            const int baseline=top+sp(87);
            const int maximum_height=sp(56);
            const int bar_height=std::max(sp(3),
                MulDiv(maximum_height,static_cast<int>(count),static_cast<int>(greatest)));
            const RECT bar{left+card_width/3,baseline-bar_height,
                left+2*card_width/3,baseline};
            const auto paint=CreateSolidBrush(leader?colors.accent:
                colors.high_contrast?colors.text:blend(colors.field,colors.accent,140));
            FillRect(dc,&bar,paint);DeleteObject(paint);

            auto value_rect=card;value_rect.left+=sp(4);value_rect.right-=sp(4);
            value_rect.top=baseline+sp(3);value_rect.bottom=value_rect.top+sp(20);
            const auto value=wide(simulation_value_display_text(group.value));
            DrawTextW(dc,value.c_str(),-1,&value_rect,
                DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            auto count_rect=card;count_rect.left+=sp(4);count_rect.right-=sp(4);
            count_rect.top=value_rect.bottom;count_rect.bottom=count_rect.top+sp(16);
            const auto quantity=wide("×"+std::to_string(count));
            DrawTextW(dc,quantity.c_str(),-1,&count_rect,
                DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            SelectObject(dc,old_font);
        }
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
        add(array_label_id,prefix+"array-label");
        add(original_order_id,"state/"+std::to_string(scenario)+"/"+input_key+"/original-order");
        result.push_back({prefix+"array",array_text(),canvas,true});
        add(step_id,prefix+"step");add(progress_label_id,prefix+"progress");
        add(explanation_id,prefix+"explanation");
        add(transition_id,prefix+"transition");add(variables_id,prefix+"variables");
        if(completed_groups()){
            // The same model step has two different narrations. Their archive
            // keys must differ so toggling the filter cannot replay cached
            // audio or a previous read/unfinished position for the other mode.
            const auto mode=frequency_candidates_only?"candidates":"all";
            result.push_back({prefix+"frequencies/"+mode,frequency_text(),frequencies,true});
        }
        add(output_label_id,prefix+"output-label");add(output_id,prefix+"output");
        add(result_comparison_id,"state/"+std::to_string(scenario)+"/"+input_key+"/result-comparison");
        add(input_label_id,"input-label");add(error_id,prefix+"error");
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
        else if(id==frequency_filter_id&&completed_groups()){
            frequency_candidates_only=!frequency_candidates_only;
            frequency_scroll=0;pressed_frequency_group.reset();update();publish();
        }
        else if(id==previous_decision_id){if(const auto target=previous_decision())go(*target);}
        else if(id==next_decision_id){if(const auto target=next_decision())go(*target);}
        else if(id==apply_id&&spec.editable)apply_input();
        else if(id==restore_id&&spec.editable)replace_data(original_data(),true);
        else if(id>=group_first_id&&static_cast<std::size_t>(id-group_first_id)<group_buttons.size())go(id-group_first_id);
        return 0;
    }
    static LRESULT CALLBACK input_navigation_proc(HWND edit,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto* self=reinterpret_cast<Impl*>(raw);
        if(message==WM_GETDLGCODE){
            const auto flags=DefSubclassProc(edit,message,wp,lp);
            const auto* key=reinterpret_cast<const MSG*>(lp);
            if(key&&key->message==WM_KEYDOWN&&key->wParam==VK_TAB)
                return flags&~(DLGC_WANTTAB|DLGC_WANTALLKEYS);
            return flags;
        }
        if(message==WM_MOUSEWHEEL){
            if(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL){
                // Track high-resolution wheel deltas per editor. Neither
                // unmodified scrolling nor another simulation is affected.
                self->input_wheel_delta+=GET_WHEEL_DELTA_WPARAM(wp);
                const int notches=self->input_wheel_delta/WHEEL_DELTA;
                self->input_wheel_delta-=notches*WHEEL_DELTA;
                if(!notches)return 0;
                int numerator=0,denominator=0;
                SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),
                    reinterpret_cast<LPARAM>(&denominator));
                const int current=denominator?MulDiv(numerator,100,denominator):100;
                const int next=std::clamp(current+10*notches,50,150);
                if(next!=current&&!set_rich_text_zoom(edit,next))
                    OutputDebugStringW(L"El editor de datos no aceptó la escala RichEdit solicitada.\n");
                return 0;
            }
            self->input_wheel_delta=0;
        }
        if(message==WM_KILLFOCUS)self->input_wheel_delta=0;
        if(message==WM_NCDESTROY)RemoveWindowSubclass(edit,input_navigation_proc,3);
        return DefSubclassProc(edit,message,wp,lp);
    }
    static LRESULT CALLBACK passive_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw) {
        auto* self=reinterpret_cast<Impl*>(raw);
        if(message==WM_GETDLGCODE){const auto* key=reinterpret_cast<const MSG*>(lp);
            const auto flags=DefSubclassProc(item,message,wp,lp);
            if(key&&key->message==WM_KEYDOWN&&key->wParam==VK_TAB)return flags&~(DLGC_WANTTAB|DLGC_WANTALLKEYS);
            return flags;}
        if(self->callbacks.passive_gesture&&passive_message(message)){
            const auto handled=self->callbacks.passive_gesture(item,message,wp,lp);if(handled&&message!=WM_NCDESTROY)return *handled;}
        if(message==WM_MOUSEWHEEL){
            // The parent's reader owns Ctrl+wheel. Ordinary wheel moves
            // scrollable read-only text only until its actual upper/lower
            // scroll limit; forwarding at an edge prevents a stuck page.
            if((GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)||
                !can_scroll_with_wheel(item,GET_WHEEL_DELTA_WPARAM(wp)))
                return SendMessageW(GetParent(item),message,wp,lp);
        }
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
        // The lesson reader captures down/up for panning. Accept a computed
        // target only after a stationary release on its original cell.
        if(message==WM_LBUTTONDOWN){
            self->pressed_target.reset();
            if(!(wp&(MK_SHIFT|MK_CONTROL|MK_RBUTTON|MK_MBUTTON))){
                self->pressed_at={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                self->pressed_target=self->target_at(self->pressed_at);
            }
        }else if(message==WM_MOUSEMOVE&&self->pressed_target){
            if(!(wp&MK_LBUTTON)||std::abs(GET_X_LPARAM(lp)-self->pressed_at.x)>self->px(8)||
                std::abs(GET_Y_LPARAM(lp)-self->pressed_at.y)>self->px(8))self->pressed_target.reset();
        }else if(message==WM_LBUTTONDBLCLK||message==WM_CANCELMODE||message==WM_CAPTURECHANGED)
            self->pressed_target.reset();
        std::optional<std::size_t> clicked_target;
        if(message==WM_LBUTTONUP){
            const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(self->pressed_target&&!(wp&(MK_SHIFT|MK_CONTROL))&&
                self->target_at(point)==self->pressed_target&&
                std::abs(point.x-self->pressed_at.x)<=self->px(8)&&
                std::abs(point.y-self->pressed_at.y)<=self->px(8))clicked_target=self->pressed_target;
            self->pressed_target.reset();
        }
        std::optional<LRESULT> handled;
        if(self->callbacks.passive_gesture&&passive_message(message))
            handled=self->callbacks.passive_gesture(item,message,wp,lp);
        if(clicked_target&&*clicked_target<self->count()){
            self->go(*clicked_target);return 0;
        }
        if(handled&&message!=WM_NCDESTROY)return *handled;
        if(message==WM_SETCURSOR&&LOWORD(lp)==HTCLIENT){
            POINT point{};
            if(GetCursorPos(&point)&&ScreenToClient(item,&point)&&self->target_at(point)){
                SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;
            }
        }
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);self->paint_array(dc);EndPaint(item,&paint);return 0;}
        if(message==WM_PRINTCLIENT){self->paint_array(reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_MOUSEWHEEL)return SendMessageW(GetParent(item),message,wp,lp);
        return DefWindowProcW(item,message,wp,lp);
    }
    static LRESULT CALLBACK frequencies_procedure(HWND item,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(item,GWLP_USERDATA));
        if(message==WM_NCCREATE){
            self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(item,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProcW(item,message,wp,lp);
        if(message==WM_GETDLGCODE){
            const auto* key=reinterpret_cast<const MSG*>(lp);
            if(key&&key->message==WM_KEYDOWN&&key->wParam==VK_TAB)return 0;
            return DLGC_WANTARROWS;
        }
        // Respect the reader's pan gesture: a group is selected only after
        // a stationary, unmodified release on the very same visible card.
        if(message==WM_LBUTTONDOWN){
            self->pressed_frequency_group.reset();
            if(!(wp&(MK_SHIFT|MK_CONTROL|MK_RBUTTON|MK_MBUTTON))){
                self->frequency_pressed_at={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                self->pressed_frequency_group=self->frequency_target_at(self->frequency_pressed_at);
            }
        }else if(message==WM_MOUSEMOVE&&self->pressed_frequency_group){
            if(!(wp&MK_LBUTTON)||
                    std::abs(GET_X_LPARAM(lp)-self->frequency_pressed_at.x)>self->px(8)||
                    std::abs(GET_Y_LPARAM(lp)-self->frequency_pressed_at.y)>self->px(8))
                self->pressed_frequency_group.reset();
        }else if(message==WM_LBUTTONDBLCLK||message==WM_CANCELMODE||
                 message==WM_CAPTURECHANGED)
            self->pressed_frequency_group.reset();
        std::optional<std::size_t> clicked_group;
        if(message==WM_LBUTTONUP){
            const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(self->pressed_frequency_group&&!(wp&(MK_SHIFT|MK_CONTROL))&&
                    self->frequency_target_at(point)==self->pressed_frequency_group&&
                    std::abs(point.x-self->frequency_pressed_at.x)<=self->px(8)&&
                    std::abs(point.y-self->frequency_pressed_at.y)<=self->px(8))
                clicked_group=self->pressed_frequency_group;
            self->pressed_frequency_group.reset();
        }
        std::optional<LRESULT> handled;
        if(self->callbacks.passive_gesture&&passive_message(message))
            handled=self->callbacks.passive_gesture(item,message,wp,lp);
        if(clicked_group){
            const auto destination=self->spec.view==SimulationViewMode::walkthrough?
                self->trace.candidate_frames.at(*clicked_group):*clicked_group;
            self->go(destination);
            return 0;
        }
        if(handled&&message!=WM_NCDESTROY)return *handled;
        if(message==WM_SETCURSOR&&LOWORD(lp)==HTCLIENT){
            POINT point{};
            if(GetCursorPos(&point)&&ScreenToClient(item,&point)&&
                    self->frequency_target_at(point)){
                SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;
            }
        }
        RECT viewport{};GetClientRect(item,&viewport);
        if(message==WM_VSCROLL){
            SCROLLINFO state{sizeof(state),SIF_ALL};
            GetScrollInfo(item,SB_VERT,&state);
            int target=state.nPos;
            switch(LOWORD(wp)){
            case SB_LINEUP:target-=self->sp(45);break;
            case SB_LINEDOWN:target+=self->sp(45);break;
            case SB_PAGEUP:target-=viewport.bottom;break;
            case SB_PAGEDOWN:target+=viewport.bottom;break;
            case SB_TOP:target=0;break;
            case SB_BOTTOM:target=self->frequency_total_height;break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK:target=state.nTrackPos;break;
            default:return 0;
            }
            self->scroll_frequencies(target);return 0;
        }
        if(message==WM_KEYDOWN&&self->frequency_total_height>viewport.bottom){
            int target=self->frequency_scroll;
            switch(wp){
            case VK_UP:target-=self->sp(45);break;
            case VK_DOWN:target+=self->sp(45);break;
            case VK_PRIOR:target-=viewport.bottom;break;
            case VK_NEXT:target+=viewport.bottom;break;
            case VK_HOME:target=0;break;
            case VK_END:target=self->frequency_total_height;break;
            default:return DefWindowProcW(item,message,wp,lp);
            }
            self->scroll_frequencies(target);return 0;
        }
        if(message==WM_MOUSEWHEEL){
            const auto delta=GET_WHEEL_DELTA_WPARAM(wp);
            if((GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)||
                !can_scroll_with_wheel(item,delta))
                return SendMessageW(GetParent(item),message,wp,lp);
            self->scroll_frequencies(self->frequency_scroll-
                delta*self->sp(75)/WHEEL_DELTA);
            return 0;
        }
        if(message==WM_PAINT){PAINTSTRUCT paint{};const auto dc=BeginPaint(item,&paint);
            self->paint_frequencies(dc);EndPaint(item,&paint);return 0;}
        if(message==WM_PRINTCLIENT){self->paint_frequencies(reinterpret_cast<HDC>(wp));return 0;}
        if(message==WM_ERASEBKGND)return 1;
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
                if(!self->layout_valid||requested!=self->width){int required=0;
                    {OutputReadingRestorer reading{*self};required=self->arrange(requested,true);}
                    // A parent's normal placement uses the already measured
                    // width. Only an independent width change needs reflow.
                    const auto callback=self->callbacks.layout_changed;
                    if(callback&&required!=area.bottom-area.top)callback();}
                return 0;}
            if(message==WM_DPICHANGED_AFTERPARENT&&!self->initializing){
                {OutputReadingRestorer reading{*self};self->fonts();self->update();}
                self->publish();return 0;}
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
    Impl::OutputReadingRestorer reading{*impl_};
    impl_->fonts(false,percentage);impl_->update();}
std::size_t SimulationView::step_index()const{return impl_->position;}
std::size_t SimulationView::step_count()const{return impl_->count();}
bool SimulationView::go_to_step(std::size_t index){return impl_->go(index);}
std::optional<RECT> SimulationView::cell_bounds_in_canvas(std::size_t index) const {
    if(index>=impl_->cell_rects.size())return std::nullopt;
    return impl_->cell_rects[index];
}
std::optional<RECT> SimulationView::frequency_card_bounds_in_canvas(
        std::size_t group_index) const {
    return impl_->frequency_bounds_in_canvas(group_index);
}
void SimulationView::reset(){impl_->go(0);}
std::vector<SimulationReadingRegion> SimulationView::reading_regions()const{return impl_->regions();}
} // namespace lgaed::presentation
