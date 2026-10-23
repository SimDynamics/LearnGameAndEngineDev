#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <oleacc.h>
#include <tom.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
// TOM legitimately uses styled mathematical Unicode alphabets/digits when
// weight changes. Compare that presentation style only, retaining every other
// character (including prose, punctuation and the structural math markers).
std::wstring math_style_text(std::wstring_view value) {
    std::wstring result;
    for(std::size_t at=0;at<value.size();){
        const auto begin=at++;unsigned code=value[begin];
        if(code>=0xD800&&code<=0xDBFF&&at<value.size()&&value[at]>=0xDC00&&value[at]<=0xDFFF)
            code=0x10000+((code-0xD800)<<10)+(value[at++]-0xDC00);
        const auto unit=value.substr(begin,at-begin);
        if((code>=0x1D400&&code<=0x1D7FF)||code==0x210E){
            wchar_t normalized[16]{};const auto count=NormalizeString(NormalizationKC,unit.data(),static_cast<int>(unit.size()),normalized,16);
            require(count>0,"Cannot compare an actual styled mathematical character");result.append(normalized,static_cast<std::size_t>(count));
        }else result.append(unit);
    }
    return result;
}
struct Host { HWND window = CreateWindowExW(0,L"STATIC",L"Native mathematics QA",WS_OVERLAPPEDWINDOW,
    0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr); ~Host() { DestroyWindow(window); } };
std::string read(const std::filesystem::path& path) {
    const auto bytes = lgaed::state::read_bytes(path,8 * 1024 * 1024); return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
std::vector<HWND> descendants(HWND parent) {
    std::vector<HWND> result; EnumChildWindows(parent,[](HWND item,LPARAM values) -> BOOL { reinterpret_cast<std::vector<HWND>*>(values)->push_back(item); return TRUE; },reinterpret_cast<LPARAM>(&result)); return result;
}
std::vector<HWND> edits(HWND parent) {
    auto result = descendants(parent); std::erase_if(result,[](HWND item) { wchar_t name[80]{}; GetClassNameW(item,name,80);
        return _wcsnicmp(name,L"RichEdit",8) != 0 || (GetWindowLongPtrW(item,GWL_STYLE) & ES_READONLY); }); return result;
}
void replace(HWND field,std::string value) {
    SendMessageW(field,EM_SETSEL,0,-1); SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(wide(value).c_str()));
}
LRESULT CALLBACK wheel_counter_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR value) {
    if (message == WM_MOUSEWHEEL) { ++*reinterpret_cast<int*>(value); return 0; }
    return DefSubclassProc(window,message,wparam,lparam);
}
struct MenuProbe { bool initialized=false,native_ids=false,accessible=false,measured=false; HMENU menu=nullptr; };
LRESULT CALLBACK menu_probe_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR value) {
    auto& probe=*reinterpret_cast<MenuProbe*>(value);
    if(message==WM_INITMENUPOPUP){probe.initialized=true;probe.menu=reinterpret_cast<HMENU>(wp);
        MENUITEMINFOW item{};item.cbSize=sizeof(item);item.fMask=MIIM_ID|MIIM_DATA|MIIM_FTYPE;
        probe.native_ids=GetMenuItemCount(probe.menu)==2&&GetMenuItemInfoW(probe.menu,0,TRUE,&item)&&item.wID==1;
        if(reader_theme::palette(window).high_contrast){probe.accessible=!(item.fType&MFT_OWNERDRAW);probe.measured=true;}
        else if(item.fType&MFT_OWNERDRAW){const auto data=reinterpret_cast<const MSAAMENUINFO*>(item.dwItemData);
            probe.accessible=data&&data->dwMSAASignature==MSAA_MENU_SIG&&std::wstring(data->pszWText)==L"Largo & claro";
            MEASUREITEMSTRUCT measured{};measured.CtlType=ODT_MENU;measured.itemID=item.wID;measured.itemData=item.dwItemData;
            probe.measured=SendMessageW(window,WM_MEASUREITEM,0,reinterpret_cast<LPARAM>(&measured))&&measured.itemHeight>0&&measured.itemWidth>0;
        }
        PostMessageW(window,WM_CANCELMODE,0,0);
    }
    return DefSubclassProc(window,message,wp,lp);
}
void CALLBACK cancel_menu_timer(HWND window,UINT,UINT_PTR timer,DWORD){KillTimer(window,timer);EndMenu();}
JsonObject simple_block(std::string response) {
    return JsonObject::Parse(winrt::to_hstring("{\"type\":\"math_puzzle\",\"prompt\":\"Pregunta visible\",\"representations\":[],\"responses\":[" + response + "]}"));
}
MathPuzzleCallbacks canonical_callbacks(MathPuzzleCallbacks events={}){
    events.expression_request=[](std::string_view request){static lgaed::windows::NarrationWorker worker(std::filesystem::path(lgaed::build::source_root)/"out/narration/runtime.json");return worker.math_editor(request);};
    return events;
}
void tom_probe(HWND parent) {
    const auto module = LoadLibraryW(L"Msftedit.dll"); require(module != nullptr,"Missing system RichEdit");
    const auto edit = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | ES_MULTILINE,0,0,500,200,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(edit != nullptr,"Cannot create the TOM capability probe");
    set_edit_text(edit,"1/2+x^2"); Microsoft::WRL::ComPtr<IRichEditOle> rich;
    SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
    Microsoft::WRL::ComPtr<ITextDocument2> document;
    const auto queried = rich ? rich.As(&document) : E_NOINTERFACE;
    std::cout << "INFO System Msftedit ITextDocument2 HRESULT=" << std::hex << queried << std::dec;
    if (document) {
        Microsoft::WRL::ComPtr<ITextRange2> range; const auto ranged = document->Range2(0,static_cast<LONG>(GetWindowTextLengthW(edit)),range.GetAddressOf());
        const auto built = SUCCEEDED(ranged) ? range->BuildUpMath(0) : ranged;
        std::cout << "; BuildUpMath=" << std::hex << built << std::dec << "; native text=" << edit_text(edit);
    }
    std::cout << " (capability evidence, not an assumed typesetting pass)\n";
    document.Reset(); rich.Reset(); DestroyWindow(edit); FreeLibrary(module);
}
std::uint64_t canvas_pixels(HWND canvas){
    RECT bounds{};GetClientRect(canvas,&bounds);const auto dc=GetDC(canvas),buffer=CreateCompatibleDC(dc);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);require(bitmap&&bits&&buffer,"Cannot inspect the real mathematical viewport pixels");ReleaseDC(canvas,dc);
    const auto old=SelectObject(buffer,bitmap);SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
    auto hash=std::uint64_t{1469598103934665603ULL};const auto* values=static_cast<const std::uint32_t*>(bits);
    for(std::size_t index=0;index<static_cast<std::size_t>(bounds.right)*bounds.bottom;++index){hash^=values[index]&0xFFFFFF;hash*=1099511628211ULL;}
    SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);return hash;
}
void canvas_viewport_tests(HWND host){
    auto raw=simple_block(R"json({"id":"plot","type":"math_coordinate_graph_builder","bounds":{"xMin":-5,"xMax":5,"yMin":-5,"yMax":5},"snap":1,"points":[{"x":0,"y":0,"draggable":true}]})json");
    MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","");view.layout(0,0,700);
    HWND canvas=nullptr,expand=nullptr;for(const auto item:descendants(view.window())){wchar_t name[90]{};GetClassNameW(item,name,90);if(std::wstring_view(name)==L"LearnGameAndEngineDev.MathCanvas")canvas=item;
        wchar_t caption[100]{};GetWindowTextW(item,caption,100);if(std::wstring_view(caption)==L"Ampliar representación")expand=item;}
    require(canvas&&expand,"A real mathematical diagram omitted its accessible expand icon");
    const auto before=view.answers_json();const auto original_parent=GetParent(canvas);RECT inline_bounds{};GetWindowRect(canvas,&inline_bounds);MapWindowPoints(nullptr,original_parent,reinterpret_cast<POINT*>(&inline_bounds),2);
    RECT control_bounds{};GetWindowRect(expand,&control_bounds);MapWindowPoints(nullptr,original_parent,reinterpret_cast<POINT*>(&control_bounds),2);
    require(inline_bounds.right-inline_bounds.left==inline_bounds.bottom-inline_bounds.top&&control_bounds.bottom-control_bounds.top==MulDiv(48,static_cast<int>(GetDpiForWindow(canvas)),96)&&control_bounds.bottom==inline_bounds.top,"Inline graph did not retain Android's square aspect ratio and separate contiguous 48dp expand row");
    SendMessageW(expand,BM_CLICK,0,0);const auto frame=GetParent(canvas);wchar_t class_name[90]{};GetClassNameW(frame,class_name,90);
    require(frame!=original_parent&&std::wstring_view(class_name)==L"LearnGameAndEngineDev.MathCanvasFullscreen"&&GetDlgItem(frame,6401),"Expand did not move the same mathematical canvas into its real fullscreen viewport with an X exit");
    require(view.answers_json()==before,"Opening a mathematical viewport changed a response");
    RECT bounds{};GetClientRect(canvas,&bounds);const auto center=MAKELPARAM(bounds.right/2,bounds.bottom/2);const auto normal=canvas_pixels(canvas);
    SendMessageW(canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,center);const auto enlarged=canvas_pixels(canvas);
    require(enlarged!=normal&&GetParent(canvas)==frame&&view.answers_json()==before,"Double-click did not enlarge the real representation, or exited fullscreen or changed its response");
    SendMessageW(canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,center);require(canvas_pixels(canvas)==normal&&view.answers_json()==before,"A second double-click failed to restore the exact original mathematical view");
    SendMessageW(canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,center);
    const int inset=MulDiv(38,static_cast<int>(GetDpiForWindow(canvas)),96);const auto target=MAKELPARAM(bounds.right/2+static_cast<int>((bounds.right-2*inset)*2/10),bounds.bottom/2);
    SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,center);SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,target);SendMessageW(canvas,WM_LBUTTONUP,0,target);
    const auto changed=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"plot").GetNamedArray(L"points").GetObjectAt(0);
    require(changed.GetNamedNumber(L"x")==1&&changed.GetNamedNumber(L"y")==0,"Dragging a zoomed mathematical point did not use the inverse viewport transform");
    const auto moved=view.answers_json();SendMessageW(GetDlgItem(frame,6401),BM_CLICK,0,0);
    RECT restored{};GetWindowRect(canvas,&restored);MapWindowPoints(nullptr,original_parent,reinterpret_cast<POINT*>(&restored),2);
    require(!IsWindow(frame)&&GetParent(canvas)==original_parent&&EqualRect(&inline_bounds,&restored)&&view.answers_json()==moved,"Closing fullscreen lost the original canvas, layout or edited mathematical state");
    view.set_enabled(false);require(IsWindowEnabled(expand),"A checked response disabled visual inspection of its representation");SendMessageW(expand,BM_CLICK,0,0);const auto review=GetParent(canvas);
    require(review!=original_parent&&GetDlgItem(review,6401),"A checked mathematical response cannot be inspected in fullscreen");SendMessageW(GetDlgItem(review,6401),WM_KEYDOWN,VK_ESCAPE,0);
    require(GetParent(canvas)==original_parent&&!IsWindow(review)&&view.answers_json()==moved,"Escape did not close the reviewed mathematical viewport without changing its answer");
    const auto line_raw=simple_block(R"json({"id":"line","type":"math_number_line","min":0,"max":10,"value":2,"step":1})json");
    MathPuzzleView number_line(host,winrt::to_string(line_raw.Stringify()),"es","");number_line.layout(0,0,700);
    HWND line=nullptr,line_expand=nullptr;for(const auto item:descendants(number_line.window())){wchar_t name[90]{},caption[100]{};GetClassNameW(item,name,90);GetWindowTextW(item,caption,100);if(std::wstring_view(name)==L"LearnGameAndEngineDev.MathCanvas")line=item;if(std::wstring_view(caption)==L"Ampliar representación")line_expand=item;}
    require(line&&line_expand,"A number line lost its real expand surface");RECT line_bounds{};GetClientRect(line,&line_bounds);
    require(std::abs(line_bounds.bottom-static_cast<int>(std::lround(line_bounds.right/2.4)))<=1,"Inline number line did not retain Android's 2.4 aspect ratio");
    const auto line_before=number_line.answers_json();const auto line_parent=GetParent(line);const auto inline_at=MAKELPARAM(line_bounds.right*3/4,line_bounds.bottom/2);
    const auto double_click=[](HWND target,LPARAM at){SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,at);SendMessageW(target,WM_LBUTTONUP,0,at);SendMessageW(target,WM_LBUTTONDBLCLK,MK_LBUTTON,at);SendMessageW(target,WM_LBUTTONUP,0,at);};
    double_click(line,inline_at);const auto line_frame=GetParent(line);
    require(line_frame!=line_parent&&number_line.answers_json()==line_before,"The first click of double-click expansion changed the number-line answer");
    GetClientRect(line,&line_bounds);const auto line_at=MAKELPARAM(line_bounds.right*3/4,line_bounds.bottom/2);const auto original_pixels=canvas_pixels(line);
    double_click(line,line_at);require(GetParent(line)==line_frame&&number_line.answers_json()==line_before&&canvas_pixels(line)!=original_pixels,"A full double-click sequence changed a number-line answer instead of only zooming");
    double_click(line,line_at);require(number_line.answers_json()==line_before&&canvas_pixels(line)==original_pixels,"Double-click reset changed the number-line answer or failed to restore its viewport");
    SendMessageW(GetDlgItem(line_frame,6401),BM_CLICK,0,0);
    SendMessageW(line,WM_LBUTTONDOWN,MK_LBUTTON,inline_at);SendMessageW(line,WM_LBUTTONUP,0,inline_at);
    require(number_line.answers_json()!=line_before,"Input-neutral zoom disabled ordinary immediate number-line selection");
    std::cout<<"PASS same-canvas mathematical fullscreen, source expand/X icons, pixel-verified double-click zoom/reset, inverse drag projection, Escape and retained responses\n";
}
void structured_input_tests(HWND host){
    const auto raw=simple_block(R"json({"id":"formula","type":"math_expression_input","inputMode":"structured","keyboard":"ALGEBRA","validator":{"expected":"\\frac{1}{2}"}})json");
    int changes=0,submitted=0,focus_events=0,back_requests=0;
    auto callbacks=canonical_callbacks({[&]{++changes;},[&](std::string){++submitted;},{},{},[&](HWND,bool){++focus_events;}});
    callbacks.back_requested=[&]{++back_requests;};
    MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","",std::move(callbacks));view.layout(0,0,650);
    HWND editor=nullptr;for(const auto item:descendants(view.window())){wchar_t name[90]{};GetClassNameW(item,name,90);if(std::wstring_view(name)==L"LearnGameAndEngineDev.MathExpressionEditor")editor=item;}
    require(editor&&edits(view.window()).empty(),"An explicitly structured response was flattened into an ordinary edit field");
    require(changes==0&&focus_events==0&&inspect_math_expression_editor(editor).latex.empty(),"Restoring a structured field submitted a change, focused a field or displayed the expected answer");
    const auto context=JsonObject::Parse(winrt::to_hstring(math_field_keyboard_context(editor)));
    require(context.GetNamedBoolean(L"structured")&&context.GetNamedString(L"keyboard")==L"ALGEBRA","The structured keyboard lost the original curricular mode");
    require(set_math_expression_latex(editor,R"(\frac{1}{2}+x^{2})"),"The canonical service could not restore a real fraction and power");
    const auto initial=inspect_math_expression_editor(editor);require(initial.complete&&initial.expression_json.find("fraction")!=std::string::npos&&initial.expression_json.find("scripts")!=std::string::npos,"A structured field did not contain the original fraction/power AST");
    const auto identity=view.answer_identity_json(),saved=view.answers_json();const auto initial_height=math_expression_editor_height(editor);
    require(perform_math_expression_key(editor,"left"),"The structured cursor could not use canonical left navigation");
    const auto moved=view.answers_json();require(moved!=saved&&view.answer_identity_json()==identity,"Moving the structured caret was lost or manufactured a different evaluated answer");
    const auto cursor=inspect_math_expression_editor(editor).cursor_json;view.set_text_scale(150);
    require(math_expression_editor_height(editor)>initial_height&&view.answer_identity_json()==identity&&inspect_math_expression_editor(editor).cursor_json==cursor,"Scaling a structured field changed its value/cursor or failed to resize the recursive expression");
    const auto persisted=view.answers_json();MathPuzzleView reopened(host,winrt::to_string(raw.Stringify()),"es",persisted,canonical_callbacks());reopened.layout(0,0,650);
    require(reopened.answer_identity_json()==identity&&reopened.answers_json()==persisted,"Reopening lost the original structured answer or its caret draft");
    const auto before_escape=inspect_math_expression_editor(editor);const auto changes_before_escape=changes;
    MSG escape{};escape.hwnd=editor;escape.message=WM_KEYDOWN;escape.wParam=VK_ESCAPE;
    require((SendMessageW(editor,WM_GETDLGCODE,VK_ESCAPE,reinterpret_cast<LPARAM>(&escape))&DLGC_WANTALLKEYS)!=0,"The regression no longer exercises the real structured keyboard route");
    SendMessageW(editor,WM_KEYDOWN,VK_ESCAPE,0);
    const auto after_escape=inspect_math_expression_editor(editor);
    require(back_requests==1&&changes==changes_before_escape&&submitted==0&&after_escape.draft_json==before_escape.draft_json&&after_escape.cursor_json==before_escape.cursor_json&&view.answers_json()==persisted,
        "Escape from a structured field lost Back or changed its canonical draft, caret or answer");
    SendMessageW(editor,WM_KEYDOWN,VK_RETURN,0);require(submitted==1,"Single-line structured Enter did not use the existing Comprobar action");
    view.set_enabled(false);require(!IsWindowEnabled(editor),"An evaluated structured answer remained editable");
    std::cout<<"PASS canonical structured math inputs: no hidden answers, initialization guard, recursive AST, cursor drafts, stable answer identity, scaling/reopen, Escape and Enter\n";
}
void calculator_field_tests(HWND host){
    const auto raw=JsonObject::Parse(LR"({"type":"math_puzzle","prompt":"Calcula.","responses":[{"id":"with","type":"math_numeric_input","calculator":true},{"id":"without","type":"math_numeric_input"},{"id":"structured","type":"math_expression_input","inputMode":"structured","calculator":true}]})");
    int opened=0,changes=0,submits=0;HWND target=nullptr;
    auto access=canonical_callbacks({[&]{++changes;},[&](std::string){++submits;},{},{},{},[&](HWND field){++opened;target=field;}});
    MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","",std::move(access));view.layout(0,0,650);
    auto fields=edits(view.window());require(fields.size()==2,"The calculator opener flattened a structured field or added a separate input");
    HWND first=nullptr,second=nullptr;for(const auto field:fields){const auto config=JsonObject::Parse(winrt::to_hstring(math_field_keyboard_context(field)));if(config.GetNamedString(L"id")==L"with")first=field;else if(config.GetNamedString(L"id")==L"without")second=field;}
    require(first&&second,"The calculator field lost its original contextual identity");const auto buttons=descendants(first);require(buttons.size()==1&&descendants(second).empty(),"Calculator actions were exposed in fields without curricular permission");
    HWND structured=nullptr;for(const auto field:descendants(view.window())){wchar_t name[96]{};GetClassNameW(field,name,96);if(std::wstring_view(name)==L"LearnGameAndEngineDev.MathExpressionEditor")structured=field;}
    require(structured&&descendants(structured).empty(),"The native field added a calculator opener to an AST input that Android excludes");
    const auto original=view.answers_json();RECT bounds{},button_bounds{};GetClientRect(first,&bounds);GetWindowRect(buttons.front(),&button_bounds);MapWindowPoints(nullptr,first,reinterpret_cast<POINT*>(&button_bounds),2);
    require(button_bounds.left>=0&&button_bounds.right<=bounds.right&&button_bounds.top>=0&&button_bounds.bottom<=bounds.bottom,"The original calculator icon changed field dimensions instead of staying inside it");
    SendMessageW(buttons.front(),BM_CLICK,0,0);require(opened==1&&target==first&&view.answers_json()==original&&changes==0&&submits==0,"Opening the calculator submitted, changed or prepopulated a mathematical answer");
    require(apply_math_calculator_result(target,R"(\frac{1}{2})"),"A valid calculator result could not be returned to its original simple target");
    CHARRANGE caret{};SendMessageW(first,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&caret));const auto answer=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"with");
    require(answer.GetNamedString(L"value")==LR"(\frac{1}{2})"&&caret.cpMin==GetWindowTextLengthW(first)&&caret.cpMax==caret.cpMin&&changes==1&&submits==0,"Returning from the calculator changed original LaTeX, moved the caret or checked the answer");
    require(apply_math_calculator_result(structured,R"(\sqrt{2})")&&inspect_math_expression_editor(structured).latex==R"(\sqrt{2})","Structured calculator target compatibility did not use the canonical AST setter");
    view.set_enabled(false);const auto saved=view.answers_json();SendMessageW(buttons.front(),BM_CLICK,0,0);
    require(opened==1&&!apply_math_calculator_result(first,"7")&&view.answers_json()==saved,"A checked/disabled answer could still be changed through its calculator icon or stale result");
    std::cout<<"PASS source calculator:true field icon, exact target and LaTeX/caret return, structured exclusion, no submit or hidden answers, disabled-state protection\n";
}
void passive_mouse_tests(HWND host) {
    const auto raw=JsonObject::Parse(LR"json({"type":"math_puzzle","prompt":"Texto pasivo con una fórmula: \\(2 + 3 = 5\\).","responses":[
        {"id":"number","type":"math_numeric_input","label":"Resultado"},
        {"id":"choice","type":"math_multiple_choice_single","options":[{"id":"a","label":"Opción visible"}]},
        {"id":"order","type":"math_ordering_drag","items":[{"id":"one","label":"Uno"},{"id":"two","label":"Dos"}],"initialOrder":["one","two"]},
        {"id":"match","type":"math_matching_drag","sources":[{"id":"a","label":"Tarjeta"}],"targets":[{"id":"b","label":"Pareja"}]},
        {"id":"plot","type":"math_coordinate_graph_builder","bounds":{"xMin":-5,"xMax":5,"yMin":-5,"yMax":5},"points":[{"x":0,"y":0,"draggable":true}]}
    ]})json");
    struct Event {HWND origin;UINT message;WPARAM keys;LPARAM point;};
    std::vector<Event> events;bool consume=true;
    MathPuzzleCallbacks access;
    access.passive_gesture=[&](HWND origin,UINT message,WPARAM keys,LPARAM point)->std::optional<LRESULT>{
        events.push_back({origin,message,keys,point});
        return consume?std::optional<LRESULT>{77}:std::nullopt;
    };
    HWND old_prompt=nullptr,old_host=nullptr;
    {
        MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","",std::move(access));view.layout(0,0,650);old_host=view.window();
        std::vector<HWND> cards;
        for(const auto& region:view.reading_regions()) {
            if(region.key=="prompt")old_prompt=region.target;
            if(region.key.starts_with("order/item/")||region.key.starts_with("match/source/")||region.key.starts_with("match/target/"))cards.push_back(region.target);
        }
        require(old_prompt&&cards.size()==4,"Passive gesture fixture lost its text or interactive cards");
        const auto original=view.answers_json();
        const auto local=MAKELPARAM(static_cast<WORD>(-7),23);
        for(const auto origin:{old_prompt,old_host}) {
            events.clear();
            for(const auto message:{WM_LBUTTONDOWN,WM_MOUSEMOVE,WM_LBUTTONUP,WM_LBUTTONDBLCLK,WM_CANCELMODE,WM_CAPTURECHANGED})
                require(SendMessageW(origin,message,MK_LBUTTON,local)==77,"A passive math origin did not honor its host gesture decision");
            require(events.size()==6&&std::ranges::all_of(events,[&](const Event& event){return event.origin==origin&&event.keys==MK_LBUTTON&&event.point==local;}),
                "Passive mathematics changed gesture origins, local coordinates or mouse keys");
        }
        require(view.answers_json()==original,"Delegating passive mouse gestures changed a mathematical response");
        events.clear();
        for(const auto item:descendants(view.window())) {
            wchar_t name[96]{};GetClassNameW(item,name,96);
            const bool editor=_wcsnicmp(name,L"RichEdit",8)==0&&!(GetWindowLongPtrW(item,GWL_STYLE)&ES_READONLY);
            const bool button=_wcsicmp(name,L"BUTTON")==0;
            const bool canvas=std::wstring_view(name)==L"LearnGameAndEngineDev.MathCanvas";
            if(button){SendMessageW(item,WM_MOUSEMOVE,0,MAKELPARAM(3,3));continue;}
            if(editor||canvas||std::ranges::find(cards,item)!=cards.end()) {
                SendMessageW(item,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));
                SendMessageW(item,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(3,3));
                SendMessageW(item,WM_LBUTTONUP,0,MAKELPARAM(3,3));
            }
        }
        require(events.empty(),"A mathematical control, editor, canvas or draggable card delegated its own gesture to the lesson");
        view.set_enabled(false);
        for(const auto card:cards){SendMessageW(card,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));SendMessageW(card,WM_LBUTTONUP,0,MAKELPARAM(2,2));}
        require(events.empty(),"Freezing cards unexpectedly converted their gestures into page navigation");
        SendMessageW(old_prompt,WM_KEYDOWN,VK_LEFT,0);
        require(events.empty(),"The passive math callback intercepted a non-mouse editing event");
        consume=false;
        SendMessageW(old_prompt,EM_SETSEL,0,0);
        require(SendMessageW(old_prompt,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4))!=77,"An unhandled passive click bypassed the original RichEdit procedure");
        SendMessageW(old_prompt,WM_LBUTTONUP,0,MAKELPARAM(4,4));
        require(std::ranges::any_of(events,[&](const Event& event){return event.origin==old_prompt&&event.message==WM_LBUTTONDOWN;}),"A host could not decline a passive mouse gesture");
        consume=true;events.clear();
    }
    require(std::ranges::any_of(events,[&](const Event& event){return event.origin==old_prompt&&event.message==WM_NCDESTROY;})&&
        std::ranges::any_of(events,[&](const Event& event){return event.origin==old_host&&event.message==WM_NCDESTROY;}),
        "Destroying mathematics omitted origin-specific gesture cleanup");
    std::cout<<"PASS passive mathematics mouse delegation: original subclass/coordinates, optional handling, no answer mutation, interactive isolation and origin lifecycle cleanup\n";
}
}

int main(int argc,char** argv) {
    std::cout << std::unitbuf;
    try {
        lgaed::windows::ensure_winrt_apartment(); INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES}; InitCommonControlsEx(&common); Host host;
        require(host.window != nullptr,"Cannot create isolated QA host"); tom_probe(host.window);
        canvas_viewport_tests(host.window);
        structured_input_tests(host.window);
        calculator_field_tests(host.window);
        passive_mouse_tests(host.window);
        if (argc > 1 && std::string_view(argv[1]) == "--visual") {
            const auto module = LoadLibraryW(L"Msftedit.dll");
            const auto formula = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY,20,20,800,180,host.window,nullptr,GetModuleHandleW(nullptr),nullptr);
            const auto inline_math = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY,20,220,800,160,host.window,nullptr,GetModuleHandleW(nullptr),nullptr);
            const auto plain = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY,20,420,800,140,host.window,nullptr,GetModuleHandleW(nullptr),nullptr);
            for (const auto item : {formula,inline_math,plain}) reader_theme::apply(item,reader_theme::Surface::card);
            set_native_math_text(formula,R"(\frac{1}{2}+x^{3}=\frac{7}{8})");
            const std::vector<lgaed::curriculum::Emphasis> emphasis{{"Negrita",0,"bold"},{"cursiva",0,"italic"}};
            set_native_math_inline_text(inline_math,"Negrita, cursiva y `std::cout`: \\(\\frac{12}{300}+x^2\\). Texto final.",emphasis);
            set_rich_text(plain,"Control de texto normal para comparar: 1/2 + x^3 = 7/8.\nPulsa Escape para cerrar esta ventana QA.");
            ShowWindow(host.window,SW_SHOWNORMAL); UpdateWindow(host.window);
            for (const auto item : {formula,inline_math,plain}) { RECT bounds{}; GetClientRect(item,&bounds);
                std::cout << "INFO visible=" << IsWindowVisible(item) << " bounds=" << bounds.right << 'x' << bounds.bottom
                          << " editstyle=" << SendMessageW(item,EM_GETEDITSTYLEEX,0,0) << " text=" << edit_text(item) << '\n'; }
            MSG message{};
            while (IsWindow(host.window) && GetMessageW(&message,nullptr,0,0) > 0) {
                if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) break;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            for (const auto item : {formula,inline_math,plain}) if (IsWindow(item)) DestroyWindow(item);
            FreeLibrary(module); return 0;
        }
        {
            const auto module = LoadLibraryW(L"Msftedit.dll");
            const auto edit = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY,0,0,600,150,host.window,nullptr,GetModuleHandleW(nullptr),nullptr);
            const std::string source = R"(\frac{1}{2}+x^{3})";
            require(set_native_math_text(edit,source),"System TOM2 did not build up the mathematical fraction and power");
            const auto mapping = native_math_text_mapping(edit,source); require(mapping && mapping->built_up,"Native math lost its original-source mapping");
            require(std::ranges::is_sorted(mapping->source_to_native) && mapping->source_to_native.size() == wide(source).size() + 1,"Mathematical source ranges are not monotonic UTF16 boundaries");
            require(mapping->native_text.find(L"frac") == std::wstring::npos && mapping->native_text.find(L'^') == std::wstring::npos,"Native math displayed LaTeX commands or an unbuilt power");
            const auto numerator = mapping->source_to_native[source.find('1')],denominator = mapping->source_to_native[source.find('2')];
            const auto base = mapping->source_to_native[source.find('x')],power = mapping->source_to_native[source.find('3')];
            ShowWindow(host.window,SW_SHOWNOACTIVATE); RedrawWindow(edit,nullptr,nullptr,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            struct MathObject { LONG type,argument; };
            const auto inline_object = [&](std::size_t position) {
                Microsoft::WRL::ComPtr<IRichEditOle> rich; SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
                Microsoft::WRL::ComPtr<ITextDocument2> document; require(rich && SUCCEEDED(rich.As(&document)),"Built math lost TOM2");
                Microsoft::WRL::ComPtr<ITextRange2> range; require(SUCCEEDED(document->Range2(static_cast<LONG>(position),static_cast<LONG>(position),range.GetAddressOf())),"A built mathematical token has no actual range");
                LONG type = 0,alignment = 0,character = 0,first = 0,second = 0,count = 0,style = 0,columns = 0,level = 0;
                require(range->GetInlineObject(&type,&alignment,&character,&first,&second,&count,&style,&columns,&level) == S_OK,"TOM2 cannot inspect its actual inline object");
                return MathObject{type,count};
            };
            const auto top = inline_object(numerator),bottom = inline_object(denominator),baseline = inline_object(base),superscript = inline_object(power);
            require(top.type == tomFraction && bottom.type == tomFraction && top.argument == 0 && bottom.argument == 1,
                "Mapped numerator and denominator are not the distinct arguments of an actual native fraction");
            require(baseline.type == tomSuperscript && superscript.type == tomSuperscript && baseline.argument == 0 && superscript.argument == 1,
                "Mapped base and exponent are not the distinct arguments of an actual native power");
            ReadingShade shade(edit,source); require(shade.matches(edit,source),"ReadingShade cannot match the original source to built native mathematics");
            shade.read_through(0); shade.read_through(source.find('2')); shade.read_through(source.find('1'));
            shade.read_through(source.size()); shade.restore();
            require(native_math_text_mapping(edit,source).has_value(),"Shading changed the native mathematical structure or its original source");
            ShowWindow(host.window,SW_HIDE); DestroyWindow(edit); FreeLibrary(module);
        }
        std::cout << "PASS Native TOM2 fraction/power objects and original-source shading ranges\n";
        {
            const auto module = LoadLibraryW(L"Msftedit.dll"); const auto edit = CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | ES_MULTILINE | ES_READONLY,0,0,600,160,host.window,nullptr,GetModuleHandleW(nullptr),nullptr);
            reader_theme::apply(edit,reader_theme::Surface::card);
            const std::string source = "Énfasis y `std::cout`: \\(\\frac{1}{2}\\) y final.";
            const std::vector<lgaed::curriculum::Emphasis> emphasis{{"Énfasis",0,"bold"},{"final",0,"italic"}};
            require(set_native_math_inline_text(edit,source,emphasis),"Inline math did not use native built-up representation");
            const auto mapping = native_math_text_mapping(edit,source); require(mapping.has_value(),"Mixed prose/code/math lost its original-source projection");
            require(mapping->native_text.find(L'`') == std::wstring::npos && mapping->native_text.find(L"frac") == std::wstring::npos,"Inline math exposed formatting delimiters");
            const auto format_at = [&](std::wstring_view fragment) {
                const auto at = mapping->native_text.find(fragment); require(at != std::wstring::npos,"An original prose/code fragment disappeared");
                CHARRANGE range{static_cast<LONG>(at),static_cast<LONG>(at + 1)}; SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
                CHARFORMAT2W value{}; value.cbSize = sizeof(value); SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&value)); return value;
            };
            const auto bold = format_at(L"Énfasis"),italic = format_at(L"final"),code = format_at(L"std");
            require(bold.yHeight==270&&italic.yHeight==270&&code.yHeight==270,
                "Mixed mathematical prose/code did not retain Android's 18sp base");
            require((bold.dwEffects & CFE_BOLD) && (italic.dwEffects & CFE_ITALIC),"Native mathematical projection dropped original bold or italic emphasis");
            require(std::wstring_view(bold.szFaceName) == L"Roboto" && std::wstring_view(italic.szFaceName) == L"Roboto" && std::wstring_view(code.szFaceName) == reader_theme::code_font_face(),"Inline math replaced preceding/following prose or code typefaces");
            require(code.crTextColor == (reader_theme::palette().high_contrast ? reader_theme::palette().text : RGB(171,141,227)),"Inline math lost the existing C++ syntax color");
            ReadingShade shade(edit,source); require(shade.matches(edit,source),"Mixed prose/code/math cannot use original narration offsets");
            shade.read_through(0); shade.read_through(wide("Énfasis y `std::cout`: \\(\\frac{1}").size()); shade.read_through(0); shade.read_through(wide(source).size());
            require(format_at(L"std").crTextColor == code.crTextColor,"Completing inline math did not restore original C++ ink");
            shade.restore();
            require(set_native_math_inline_text(edit,source,emphasis,RichTextRole::heading),
                "A mathematical heading was flattened instead of being built at its native base size");
            const auto heading_mapping=native_math_text_mapping(edit,source);
            require(heading_mapping&&heading_mapping->built_up&&heading_mapping->source==source
                &&math_style_text(heading_mapping->native_text)==math_style_text(mapping->native_text),
                "Changing mathematical typography changed its original-source projection or fraction structure");
            for(const auto [digit,argument]:{std::pair{L'1',0L},std::pair{L'2',1L}}){
                const auto source_at=wide(source).find(digit);
                const auto native_at=heading_mapping->source_to_native.at(source_at);
                Microsoft::WRL::ComPtr<IRichEditOle> rich;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
                Microsoft::WRL::ComPtr<ITextDocument2> document;require(rich&&SUCCEEDED(rich.As(&document)),"A resized mathematical heading lost TOM2");
                Microsoft::WRL::ComPtr<ITextRange2> range;require(SUCCEEDED(document->Range2(static_cast<LONG>(native_at),static_cast<LONG>(native_at),range.GetAddressOf())),
                    "The heading's original numeral no longer maps to a real native argument");
                LONG type=0,alignment=0,character=0,first=0,second=0,count=0,style=0,columns=0,level=0;
                require(range->GetInlineObject(&type,&alignment,&character,&first,&second,&count,&style,&columns,&level)==S_OK
                    &&type==tomFraction&&count==argument,"Bold typography flattened or reversed the heading's numerator/denominator");
            }
            CHARRANGE heading_start{0,1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&heading_start));
            CHARFORMAT2W heading_format{};heading_format.cbSize=sizeof(heading_format);
            SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&heading_format));
            require(heading_format.yHeight==345&&(heading_format.dwEffects&CFE_BOLD),
                "A mathematical heading lost its independent Android23sp bold hierarchy");
            DestroyWindow(edit); FreeLibrary(module);
        }
        std::cout << "PASS Inline mathematical ranges preserve original bold/italic, C++ spans and mixed-source shading\n";
        {
            int changes = 0,submits = 0; std::string submitted;
            const auto raw = simple_block(R"({"id":"number","type":"math_numeric_input","label":"Resultado","unit":"cm","validator":{"expected":"DO_NOT_SHOW"}})");
            raw.SetNamedValue(L"prompt",JsonValue::CreateStringValue(L"Pregunta con \\(\\frac{1}{2}\\) visible"));
            MathPuzzleView view(host.window,winrt::to_string(raw.Stringify()),"es","",{[&] { ++changes; },[&](std::string answers) { ++submits; submitted = std::move(answers); },{}});
            view.layout(0,0,560); const auto fields = edits(view.window()); require(fields.size() == 1,"Numeric response is not a single real native edit");
            replace(fields.front(),"-2.50");
            require(changes == 1 && JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"number").GetNamedString(L"value") == L"-2.50","Input changes lost the original typed numeric text");
            SendMessageW(fields.front(),WM_KEYDOWN,VK_RETURN,1); SendMessageW(fields.front(),WM_CHAR,L'\r',1);
            require(submits == 1 && submitted == view.answers_json() && edit_text(fields.front()) == "-2.50","Enter failed to submit the current answer or inserted a line");
            bool unit = false; for (const auto& region : view.reading_regions()) {
                require(region.source_text.find("DO_NOT_SHOW") == std::string::npos,"An expected answer entered a readable control");
                unit = unit || region.source_text == "Unidad: cm";
            }
            require(unit,"The unit's full visible label is not narratable");
            const auto original_answer=view.answers_json();const auto original_changes=changes;
            const auto natural_height=view.height_for_width(560);
            view.set_text_scale(150);int numerator=0,denominator=0;
            SendMessageW(fields.front(),EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            require(denominator>0&&numerator*100==150*denominator&&view.height_for_width(560)>=natural_height,
                "Math response text did not scale through actual native zoom and measured layout");
            require(view.answers_json()==original_answer&&changes==original_changes&&edits(view.window()).front()==fields.front(),
                "Scaling mathematics recreated an answer control or modified its answer");
            view.set_text_scale(75);view.set_text_scale(100);
            const auto prompt = view.reading_regions().front().target;
            const auto prompt_mapping=native_math_text_mapping(prompt,view.reading_regions().front().source_text);
            require(prompt_mapping&&prompt_mapping->built_up,
                "Making the mathematical prompt bold destroyed its real native fraction/source mapping");
            {
                const auto prompt_source=view.reading_regions().front().source_text;
                ReadingShade prompt_shade(prompt,prompt_source);require(prompt_shade.matches(prompt,prompt_source),
                    "The bold prompt's final math projection cannot support original-source word shading");
                prompt_shade.read_through(0);prompt_shade.read_through(wide(prompt_source).size());
                require(native_math_text_mapping(prompt,prompt_source).has_value(),
                    "Progressive shading changed the bold prompt's real fraction/source projection");
            }
            CHARFORMAT2W prompt_type{};prompt_type.cbSize=sizeof(prompt_type);
            SendMessageW(prompt,EM_SETSEL,0,-1);SendMessageW(prompt,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&prompt_type));
            require(prompt_type.yHeight==270&&(prompt_type.dwEffects&CFE_BOLD),
                "A mathematics prompt did not preserve its Android-derived 18-sp body size and bold weight");
            int wheels = 0;
            SetWindowSubclass(host.window,wheel_counter_proc,51,reinterpret_cast<DWORD_PTR>(&wheels));
            const auto before_wheel = view.answers_json();
            SendMessageW(prompt,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
            SendMessageW(fields.front(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);
            RemoveWindowSubclass(host.window,wheel_counter_proc,51);
            require(wheels == 2 && view.answers_json() == before_wheel,"Wheel on autosized math prose/input did not reach the lesson or modified the answer");
            CHARRANGE first{0,1}; SendMessageW(prompt,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&first));
            CHARFORMAT2W before{}; before.cbSize = sizeof(before); SendMessageW(prompt,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&before));
            view.set_enabled(false);
            CHARFORMAT2W after{}; after.cbSize = sizeof(after); SendMessageW(prompt,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&after));
            require(IsWindowEnabled(prompt) && !IsWindowEnabled(fields.front()) && before.crTextColor == after.crTextColor && before.dwEffects == after.dwEffects,
                "Freezing a correct response disabled or recolored its narratable prose");
            view.set_enabled(true);
            const auto saved = view.answers_json();
            MathPuzzleView restored(host.window,winrt::to_string(raw.Stringify()),"es",saved); require(restored.answers_json() == saved,"A native answer draft changed when restored");
            view.reset(); require(JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"number").GetNamedString(L"value",L"").empty(),"Reset retained a stale numeric answer");
        }
        std::cout << "PASS Native single-line input/Enter, exact answer persistence, labels and no expected-answer exposure\n";
        {
            const auto raw = simple_block(R"({"id":"ordered","type":"math_ordering_drag","items":[{"id":"a","label":"Uno"},{"id":"b","label":"Dos"}],"initialOrder":["b","a"]})");
            MathPuzzleView view(host.window,winrt::to_string(raw.Stringify()),"es",""); view.layout(0,0,560);
            auto regions = view.reading_regions(); HWND up = nullptr;
            for (const auto item : descendants(view.window())) { wchar_t name[80]{}; GetWindowTextW(item,name,80); if (std::wstring_view(name) == L"↑") up = item; }
            require(up != nullptr,"Ordering has no accessible move control");
            const auto original_order = view.answers_json(); view.set_enabled(false);
            for (const auto& region : regions) if (region.key.find("/item/") != std::string::npos)
                require(IsWindowEnabled(region.target),"Freezing an order disabled its narratable card labels");
            SendMessageW(up,BM_CLICK,0,0); require(view.answers_json() == original_order,"A frozen order still accepted a move");
            view.set_enabled(true); SendMessageW(up,BM_CLICK,0,0);
            const auto order = JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"ordered").GetNamedArray(L"ids");
            require(order.GetStringAt(0) == L"a" && order.GetStringAt(1) == L"b","The visible ordering operation did not change its own MathAnswerJson");
            const auto before = view.answers_json(); SendMessageW(view.window(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
            require(view.answers_json() == before,"Mouse wheel reordered a mathematical card");
        }
        std::cout << "PASS Real ordering controls, original initial order and wheel does not drag\n";
        {
            const auto raw = simple_block(R"({"id":"pairs","type":"math_matching_drag","sources":[{"id":"a","label":"Uno"},{"id":"b","label":"Dos"}],"targets":[{"id":"x","label":"Primero"},{"id":"y","label":"Segundo"}]})");
            MathPuzzleView view(host.window,winrt::to_string(raw.Stringify()),"es",""); view.layout(0,0,560);
            HWND a = nullptr,b = nullptr,x = nullptr,y = nullptr;
            for (const auto& region : view.reading_regions()) {
                if (region.key == "pairs/source/a") a = region.target;
                else if (region.key == "pairs/source/b") b = region.target;
                else if (region.key == "pairs/target/x") x = region.target;
                else if (region.key == "pairs/target/y") y = region.target;
            }
            require(a && b && x && y,"Matching did not render its own original source and target cards");
            const auto click = [](HWND item) { SendMessageW(item,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4)); SendMessageW(item,WM_LBUTTONUP,0,MAKELPARAM(4,4)); };
            click(a); click(x);
            require(JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"pairs").GetNamedObject(L"pairs").GetNamedString(L"a",L"") == L"x",
                "Selecting visible matching cards did not create the selected pair");
            const auto saved = view.answers_json(); view.set_enabled(false);
            for (const auto item : {a,b,x,y}) require(IsWindowEnabled(item),"Freezing matching disabled its narratable labels");
            click(b); click(y); require(view.answers_json() == saved,"A frozen matching response accepted a new pair");
            view.set_enabled(true); click(b); click(y);
            require(JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"pairs").GetNamedObject(L"pairs").GetNamedString(L"b",L"") == L"y",
                "Re-enabling matching did not restore the existing interaction");
        }
        std::cout << "PASS Matching cards retain readable labels and freeze only answer interaction\n";
        {
            const auto raw=simple_block(R"json({"id":"structured","type":"math_structured_answer","fields":[{"id":"pick","type":"dropdown","options":[{"id":"first","label":"Largo & claro"},{"id":"second","label":"Otra opción"}]}]})json");
            MathPuzzleView view(host.window,winrt::to_string(raw.Stringify()),"es","");view.layout(0,0,560);
            HWND picker=nullptr;for(const auto item:descendants(view.window())){wchar_t caption[64]{};GetWindowTextW(item,caption,64);if(std::wstring_view(caption)==L"Seleccionar…")picker=item;}
            require(picker!=nullptr,"The structured mathematical field lost its real dropdown button");
            const auto original=view.answers_json();MenuProbe probe;require(SetWindowSubclass(view.window(),menu_probe_proc,52,reinterpret_cast<DWORD_PTR>(&probe)),"Observe the actual mathematical menu owner");
            require(SetTimer(view.window(),53,100,cancel_menu_timer),"Bound the isolated native menu interaction");
            SendMessageW(picker,BM_CLICK,0,0);KillTimer(view.window(),53);RemoveWindowSubclass(view.window(),menu_probe_proc,52);
            require(probe.initialized&&probe.native_ids&&probe.accessible&&probe.measured,"The mathematical dropdown bypassed native menu styling, owner measurement or literal accessible labels");
            require(!IsMenu(probe.menu)&&view.answers_json()==original,"Cancelling the actual dropdown leaked its menu or changed a mathematical answer");
        }
        std::cout << "PASS Mathematical dropdown uses native menu styling, literal accessible labels and unchanged cancelled answers\n";
        std::map<std::string,std::string> examples;
        std::size_t blocks = 0;
        const auto root = std::filesystem::path(lgaed::build::source_root) / "app/src/main/assets/content/es";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || !entry.path().filename().wstring().starts_with(L"lesson_") || entry.path().extension() != L".json") continue;
            const auto lesson = JsonObject::Parse(winrt::to_hstring(read(entry.path())));
            for (const auto& page_value : lesson.GetNamedArray(L"pages")) for (const auto& block_value : page_value.GetObject().GetNamedArray(L"blocks")) {
                const auto block = block_value.GetObject(); if (block.GetNamedString(L"type",L"") != L"math_puzzle") continue;
                std::unique_ptr<MathPuzzleView> component;
                try { component = std::make_unique<MathPuzzleView>(host.window,winrt::to_string(block.Stringify()),"es","",canonical_callbacks()); }
                catch (const winrt::hresult_error& error) { throw std::runtime_error(entry.path().string() + ":" + winrt::to_string(block.GetNamedString(L"id",L"")) + ": " + winrt::to_string(error.message())); }
                auto& view = *component;
                for (const auto width : {400,800}) { const auto height = view.height_for_width(width); require(height > 0 && height < 20000,"A real math composition has invalid layout bounds"); view.layout(0,0,width); }
                const auto saved = view.answers_json(); require(JsonObject::Parse(winrt::to_hstring(saved)).Size() == block.GetNamedArray(L"responses").Size(),"A real authored response was omitted");
                for (const auto& value : block.GetNamedArray(L"responses")) examples.try_emplace(winrt::to_string(value.GetObject().GetNamedString(L"type")),winrt::to_string(block.GetNamedString(L"id",L"")));
                ++blocks;
            }
        }
        require(examples.size() == 13,"The real composition inventory did not cover all thirteen current response types");
        std::cout << "PASS " << blocks << " original Spanish math compositions: all " << examples.size() << " response families construct and resize without curriculum mutation\n";
        std::cout << "Native math control tests PASS; construction is not a claim of complete visual or acoustical equivalence.\n";
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << "FAIL WinRT: " << winrt::to_string(error.message()) << '\n'; }
      catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; }
    return 1;
}
