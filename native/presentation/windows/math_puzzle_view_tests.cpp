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
        // A disabled answer remains the same semantic input even when its
        // native text services are temporarily read-only for dark rendering.
        return _wcsnicmp(name,L"RichEdit",8) != 0 || ((GetWindowLongPtrW(item,GWL_STYLE) & ES_READONLY)&&math_field_keyboard_context(item).empty()); }); return result;
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
        probe.native_ids=GetMenuItemCount(probe.menu)==3&&GetMenuItemInfoW(probe.menu,0,TRUE,&item)&&item.wID==1;
        wchar_t placeholder[80]{};GetMenuStringW(probe.menu,0,placeholder,80,MF_BYPOSITION);
        probe.native_ids=probe.native_ids&&std::wstring_view(placeholder)==L"Seleccionar…"&&GetMenuItemInfoW(probe.menu,1,TRUE,&item)&&item.wID==2;
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
void click_card(HWND item){SendMessageW(item,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));SendMessageW(item,WM_LBUTTONUP,0,MAKELPARAM(4,4));}
HWND region_target(const MathPuzzleView& view,std::string_view key){for(const auto& region:view.reading_regions())if(region.key==key)return region.target;return nullptr;}
HWND caption_target(HWND parent,std::wstring_view caption){for(const auto item:descendants(parent)){wchar_t value[200]{};GetWindowTextW(item,value,200);if(std::wstring_view(value)==caption)return item;}return nullptr;}
std::vector<COLORREF> painted_points(HWND window,const std::vector<POINT>& points){
    RECT bounds{};GetClientRect(window,&bounds);const auto dc=GetDC(window),buffer=CreateCompatibleDC(dc);
    const auto bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);ReleaseDC(window,dc);
    require(buffer&&bitmap,"Cannot inspect mathematical choice card pixels");const auto previous=SelectObject(buffer,bitmap);
    SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
    std::vector<COLORREF> result;for(const auto point:points)result.push_back(GetPixel(buffer,point.x,point.y));
    SelectObject(buffer,previous);DeleteObject(bitmap);DeleteDC(buffer);return result;
}
void android_control_parity_tests(HWND host){
    {
        int expanded=0,resets=0;MathPuzzleCallbacks callbacks;callbacks.fullscreen_requested=[&]{++expanded;};callbacks.reset_requested=[&]{++resets;};
        const auto raw=simple_block(R"({"id":"choice","type":"math_multiple_choice_single","options":[{"id":"first","label":"Una opción con \\(2 + 3 = 5\\)."},{"id":"second","label":"Otra opción"}]})");
        MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","",std::move(callbacks));view.layout(0,0,650);
        const auto expand=caption_target(view.window(),L"Pantalla completa"),reset=caption_target(view.window(),L"Reiniciar");
        require(expand&&reset,"A mathematical card omitted its source fullscreen/reset toolbar actions");
        RECT card_client{},reset_position{},prompt_position{};GetClientRect(view.window(),&card_client);GetWindowRect(reset,&reset_position);
        const auto prompt=region_target(view,"prompt");require(prompt,"The mathematical card lost its authored prompt");GetWindowRect(prompt,&prompt_position);
        MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&reset_position),2);MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&prompt_position),2);
        const auto card_dpi=static_cast<int>(GetDpiForWindow(view.window()));
        require(reset_position.top==MulDiv(8,card_dpi,96)&&reset_position.right==card_client.right-MulDiv(6,card_dpi,96)&&
            prompt_position.left==MulDiv(6,card_dpi,96)&&prompt_position.right==card_client.right-MulDiv(6,card_dpi,96),
            "A mathematical card lost Android's6dp horizontal and8dp top padding");
        SendMessageW(expand,BM_CLICK,0,0);SendMessageW(reset,BM_CLICK,0,0);require(expanded==1&&resets==1,"Mathematical toolbar actions bypassed the existing lesson controller");
        const auto option=region_target(view,"choice/option/first");require(option,"The original rich answer is absent");
        HWND radio=nullptr;for(const auto item:descendants(view.window())){wchar_t name[32]{};GetClassNameW(item,name,32);if(_wcsicmp(name,L"BUTTON")==0&&(GetWindowLongPtrW(item,GWL_STYLE)&BS_TYPEMASK)==BS_RADIOBUTTON){radio=item;break;}}
        require(radio,"The original single-choice control is absent");const auto original=view.answers_json();SetFocus(radio);
        require(view.answers_json()==original&&SendMessageW(radio,BM_GETCHECK,0,0)==BST_UNCHECKED,"Merely focusing a mathematical option selected an answer");
        RECT radio_bounds{},label_bounds{},card_bounds{};GetWindowRect(radio,&radio_bounds);GetWindowRect(option,&label_bounds);
        MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&radio_bounds),2);MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&label_bounds),2);
        UnionRect(&card_bounds,&radio_bounds,&label_bounds);const auto border_gap=MulDiv(3,static_cast<int>(GetDpiForWindow(view.window())),96);
        const std::vector<POINT> card_pixels{{(card_bounds.left+card_bounds.right)/2,card_bounds.top-border_gap},
            {card_bounds.left-border_gap,(card_bounds.top+card_bounds.bottom)/2},
            {(label_bounds.left+label_bounds.right)/2,label_bounds.top}};
        const auto normal_card=painted_points(view.window(),card_pixels);const auto colors=reader_theme::palette(view.window());
        require(normal_card[0]==colors.outline&&normal_card[1]==colors.outline&&normal_card[2]==reader_theme::background(view.window()),
            "A mathematical choice card failed to surround both radio and rich content, or retained an input-like label outline");
        SendMessageW(option,EM_SETSEL,0,0);click_card(option);
        const auto chosen=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"choice").GetNamedArray(L"selected");
        CHARRANGE selection{};SendMessageW(option,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
        require(chosen.Size()==1&&chosen.GetStringAt(0)==L"first","Clicking the rich mathematical option did not select its answer");
        require(selection.cpMin==selection.cpMax,"Clicking the rich mathematical option selected its visible text");
        const auto selected_card=painted_points(view.window(),card_pixels);
        require(selected_card[0]==colors.accent&&selected_card[1]==colors.accent&&selected_card[2]==normal_card[2],
            "Selecting a mathematical option did not accent its whole Android card or altered the rich text's background");
        click_card(option);require(JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"choice").GetNamedArray(L"selected").Size()==0,"A second card click did not toggle the original Android single-choice state");
        const auto before=view.answers_json();view.set_enabled(false);click_card(option);require(view.answers_json()==before&&IsWindowEnabled(expand)&&IsWindowEnabled(reset),"Freezing a choice left its rich body editable or disabled source view/reset actions");
    }
    {
        const auto raw=simple_block(R"({"id":"order","type":"math_ordering_drag","items":[{"id":"a","label":"Uno"},{"id":"b","label":"Dos"},{"id":"c","label":"Tres"}],"initialOrder":["c","b","a"]})");
        MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","");view.layout(0,0,650);
        require(!caption_target(view.window(),L"↑")&&!caption_target(view.window(),L"↓"),"Mathematical cards introduced arrow controls absent from Android");
        require(region_target(view,"order/label")&&caption_target(view.window(),L"Arrastra una tarjeta directamente o toca las tarjetas en el orden deseado."),"The original ordering instruction was replaced by another interaction");
        const auto order_card=GetParent(region_target(view,"order/item/a"));RECT order_area{};GetClientRect(order_card,&order_area);
        const auto order_frame=painted_points(order_card,{{order_area.right/2,0},{MulDiv(5,GetDpiForWindow(order_card),96),0}});
        require(order_frame[0]==reader_theme::palette(order_card).outline&&order_frame[1]==reader_theme::background(order_card),
            "An original mathematical ordering card lost its10dp rounded outline on first presentation");
        click_card(region_target(view,"order/item/a"));const auto saved=view.answers_json();
        MathPuzzleView reopened(host,winrt::to_string(raw.Stringify()),"es",saved);reopened.layout(0,0,650);click_card(region_target(reopened,"order/item/b"));
        auto values=JsonObject::Parse(winrt::to_hstring(reopened.answers_json())).GetNamedObject(L"order").GetNamedArray(L"ids");
        require(values.GetStringAt(0)==L"a"&&values.GetStringAt(1)==L"b"&&values.GetStringAt(2)==L"c","Tap order or its original draft position was lost when reopened");
        const auto identity=reopened.answer_identity_json();click_card(region_target(reopened,"order/item/c"));require(reopened.answer_identity_json()==identity,"Advancing the tap cursor manufactured a different answer identity");
        const auto before=reopened.answers_json();const auto card=region_target(reopened,"order/item/c");SendMessageW(card,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));SendMessageW(card,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);SendMessageW(card,WM_LBUTTONUP,0,MAKELPARAM(4,4));require(reopened.answers_json()==before,"A wheel scroll executed a pending mathematical card tap");
        MathPuzzleView dragged(host,winrt::to_string(raw.Stringify()),"es","");dragged.layout(0,0,650);
        const auto source=region_target(dragged,"order/item/c"),destination=region_target(dragged,"order/item/a");
        RECT destination_bounds{};GetWindowRect(destination,&destination_bounds);
        const auto began=GetTickCount64();SendMessageW(source,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));
        const auto capture=GetCapture();require(capture==GetParent(source),"Dragging a rich mathematical label did not capture its whole original card");
        while(GetTickCount64()-began<20)Sleep(1);
        POINT drop{(destination_bounds.left+destination_bounds.right)/2,(destination_bounds.top+destination_bounds.bottom)/2};ScreenToClient(capture,&drop);
        SendMessageW(capture,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(drop.x,drop.y));SendMessageW(capture,WM_LBUTTONUP,0,MAKELPARAM(drop.x,drop.y));
        values=JsonObject::Parse(winrt::to_hstring(dragged.answers_json())).GetNamedObject(L"order").GetNamedArray(L"ids");
        require(values.GetStringAt(0)==L"b"&&values.GetStringAt(1)==L"a"&&values.GetStringAt(2)==L"c"&&
            region_target(dragged,"order/item/c")==source&&edit_text(source)=="Tres"&&!GetCapture(),
            "A20ms drag through the real captured card failed to move its original rich label or altered its content");
    }
    {
        const auto raw=simple_block(R"({"id":"pairs","type":"math_matching_drag","sources":[{"id":"a","label":"Uno"},{"id":"b","label":"Dos"}],"targets":[{"id":"x","label":"Primero"},{"id":"y","label":"Segundo"}]})");
        MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","");view.layout(0,0,650);const auto a=region_target(view,"pairs/source/a"),b=region_target(view,"pairs/source/b"),x=region_target(view,"pairs/target/x");
        const auto matching_card=GetParent(a);RECT matching_area{};GetClientRect(matching_card,&matching_area);
        const auto matching_frame=painted_points(matching_card,{{matching_area.right/2,0},{MulDiv(5,GetDpiForWindow(matching_card),96),0}});
        require(matching_frame[0]==reader_theme::palette(matching_card).outline&&matching_frame[1]==reader_theme::background(matching_card),
            "An original mathematical matching card lost its10dp rounded outline on first presentation");
        click_card(a);click_card(x);RECT target{},source{};GetWindowRect(x,&target);GetWindowRect(a,&source);require(source.top>=target.bottom,"A matched source was cloned/linked instead of moving its same card under the target");
        click_card(b);click_card(x);auto pairs=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"pairs").GetNamedObject(L"pairs");
        require(pairs.Size()==1&&!pairs.HasKey(L"a")&&pairs.GetNamedString(L"b")==L"x","Matching failed to release the source displaced from an occupied target");
        click_card(x);require(JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"pairs").GetNamedObject(L"pairs").Size()==0,"Clicking a target without a selected source failed to clear its assignment");
        require(region_target(view,"pairs/source/a")==a&&region_target(view,"pairs/source/b")==b,"Matching replaced the native cards and lost their reading associations");
    }
    {
        const auto raw=simple_block(R"({"id":"line","type":"math_number_line","min":-5,"max":5,"step":0.5,"value":1})");
        MathPuzzleView view(host,winrt::to_string(raw.Stringify()),"es","");view.layout(0,0,650);const auto fields=edits(view.window());
        require(fields.size()==1&&region_target(view,"line/position")&&region_target(view,"line/interval-direction"),"The interactive number line omitted its source position field or interval labels");
        replace(fields.front(),"-2.5");auto value=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"line");
        require(value.GetNamedNumber(L"value")==-2.5&&value.GetNamedBoolean(L"closed")&&value.GetNamedString(L"direction")==L"right","The position control/default endpoint did not match the Android number line");
        replace(fields.front()," 1,5 ");value=JsonObject::Parse(wide(view.answers_json())).GetNamedObject(L"line");require(value.GetNamedNumber(L"value")==1.5,"The number-line position rejected the same trimmed decimal-comma input accepted by Android");
        replace(fields.front(),"-");value=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"line");require(!value.HasKey(L"value"),"An incomplete position could silently check its stale previous numeric value");
        MathPuzzleView reopened(host,winrt::to_string(raw.Stringify()),"es",view.answers_json());reopened.layout(0,0,650);require(edit_text(edits(reopened.window()).front())=="-"&&!JsonObject::Parse(winrt::to_hstring(reopened.answers_json())).GetNamedObject(L"line").HasKey(L"value"),"Reopening converted an incomplete position into a valid old answer");
    }
    {
        MathPuzzleView legacy(host,R"({"type":"math_numeric_input","question":"Escribe el resultado.","validator":{"type":"numeric","expected":4}})","es","");legacy.layout(0,0,650);
        require(edits(legacy.window()).size()==1&&JsonObject::Parse(winrt::to_hstring(legacy.answers_json())).HasKey(L"response"),"The Android standalone mathematical contract produced an empty Windows puzzle");
        const auto raw=simple_block(R"({"id":"shape","type":"math_geometry_builder"})");MathPuzzleView geometry(host,winrt::to_string(raw.Stringify()),"es","");geometry.layout(0,0,650);
        require(JsonObject::Parse(winrt::to_hstring(geometry.answers_json())).GetNamedObject(L"shape").GetNamedArray(L"points").Size()==3&&edits(geometry.window()).size()==6,"A geometry builder lost Android's default polygon and its editable vertices");
    }
    std::cout<<"PASS source mathematical toolbar, rich-card choice clicks, tap ordering drafts, unique movable matching, number-line controls/invalid drafts and standalone/geometry defaults\n";
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
    const auto old=SelectObject(buffer,bitmap);SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);GdiFlush();
    auto hash=std::uint64_t{1469598103934665603ULL};const auto* values=static_cast<const std::uint32_t*>(bits);
    for(std::size_t index=0;index<static_cast<std::size_t>(bounds.right)*bounds.bottom;++index){hash^=values[index]&0xFFFFFF;hash*=1099511628211ULL;}
    SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);return hash;
}
JsonObject authored_puzzle(std::string_view relative,unsigned page,std::string_view id){
    const auto lesson=JsonObject::Parse(winrt::to_hstring(read(std::filesystem::path(lgaed::build::source_root)/relative)));
    for(const auto& value:lesson.GetNamedArray(L"pages").GetObjectAt(page).GetNamedArray(L"blocks")){
        const auto block=value.GetObject();if(block.GetNamedString(L"id",L"")==winrt::to_hstring(id))return block;
    }
    throw std::runtime_error("The authored mathematical regression location is absent");
}
std::vector<HWND> class_targets(HWND parent,std::wstring_view name){
    auto result=descendants(parent);std::erase_if(result,[&](HWND item){wchar_t type[100]{};GetClassNameW(item,type,100);return _wcsicmp(type,std::wstring(name).c_str())!=0;});return result;
}
RECT visible_ink_bounds(HWND item){
    RECT area{};GetClientRect(item,&area);const auto dc=GetDC(item),buffer=CreateCompatibleDC(dc);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(item,dc);require(bitmap&&buffer&&pixels,"Cannot inspect actual mathematical answer ink");const auto previous=SelectObject(buffer,bitmap);
    const auto background=reader_theme::background(item);const auto fill=CreateSolidBrush(background);FillRect(buffer,&area,fill);DeleteObject(fill);SendMessageW(item,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);GdiFlush();
    RECT ink{area.right,area.bottom,-1,-1};const auto values=static_cast<const std::uint32_t*>(pixels);
    for(int y=0;y<area.bottom;++y)for(int x=0;x<area.right;++x){const auto pixel=values[static_cast<std::size_t>(y)*area.right+x];const auto difference=std::abs(static_cast<int>((pixel>>16)&255)-GetRValue(background))+std::abs(static_cast<int>((pixel>>8)&255)-GetGValue(background))+std::abs(static_cast<int>(pixel&255)-GetBValue(background));if(difference>=24){ink.left=std::min<LONG>(ink.left,x);ink.top=std::min<LONG>(ink.top,y);ink.right=std::max<LONG>(ink.right,x+1);ink.bottom=std::max<LONG>(ink.bottom,y+1);}}
    SelectObject(buffer,previous);DeleteObject(bitmap);DeleteDC(buffer);require(ink.bottom>ink.top,"The original mathematical answer produced no visible ink");return ink;
}
void initial_text_scale_layout_tests(HWND host){
    const auto block=authored_puzzle("app/src/main/assets/content/es/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json",0,"contar_o_medir");
    const auto source=winrt::to_string(block.Stringify());
    for(const auto scale:{75,100,150})for(const auto width:{850,330}){
        MathPuzzleView before(host,source,"es","");before.set_text_scale(scale);before.layout(0,0,width);
        MathPuzzleView after(host,source,"es","");after.layout(0,0,width);after.set_text_scale(scale);after.layout(0,0,width);
        RECT before_area{},after_area{};GetClientRect(before.window(),&before_area);GetClientRect(after.window(),&after_area);
        require(EqualRect(&before_area,&after_area),"Applying initial mathematical text scale measured an artificial narrow layout and changed the puzzle height");
        const auto original=before.reading_regions(),updated=after.reading_regions();require(original.size()==updated.size(),"Initial mathematical scaling changed the original reading regions");
        for(std::size_t index=0;index<original.size();++index){RECT one{},two{};GetWindowRect(original[index].target,&one);GetWindowRect(updated[index].target,&two);
            require(original[index].key==updated[index].key&&original[index].source_text==updated[index].source_text&&EqualRect(&one,&two),"Initial and subsequent mathematical scaling did not preserve the same text geometry");}
        require(before.answers_json()==after.answers_json(),"Initial mathematical scaling changed a response");
    }
    std::cout<<"PASS initial mathematical scale before layout matches scaling after layout at75/100/150percent and both widths\n";
}
void choice_alignment_tests(HWND host){
    const auto block=authored_puzzle("app/src/main/assets/content/es/math_physics/math_course_01/numeros_y_operaciones/lesson_magnitudes_y_numeracion.json",0,"contar_o_medir");
    MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","");const auto original=view.answers_json();
    for(const auto scale:{75,100,150})for(const auto width:{850,330}){
        view.set_text_scale(scale);view.layout(0,0,width);auto radios=class_targets(view.window(),L"BUTTON");std::erase_if(radios,[](HWND item){return (GetWindowLongPtrW(item,GWL_STYLE)&BS_TYPEMASK)!=BS_RADIOBUTTON;});
        std::ranges::sort(radios,[](HWND a,HWND b){RECT one{},two{};GetWindowRect(a,&one);GetWindowRect(b,&two);return one.top<two.top;});require(radios.size()==3,"The actual introductory choice lost its original radios");
        const auto options=block.GetNamedArray(L"responses").GetObjectAt(0).GetNamedArray(L"options");
        for(unsigned index=0;index<options.Size();++index){const auto body=region_target(view,"contar_o_medir__eleccion/option/"+winrt::to_string(options.GetObjectAt(index).GetNamedString(L"id")));require(body,"The actual introductory option is absent");
            RECT radio{},label{};GetWindowRect(radios[index],&radio);GetWindowRect(body,&label);const auto ink=visible_ink_bounds(body);const auto tolerance=MulDiv(3,static_cast<int>(GetDpiForWindow(body)),96);
            if(std::abs(2*label.top+ink.top+ink.bottom-radio.top-radio.bottom)>2*tolerance)
                throw std::runtime_error("The visible mathematical option text is vertically misaligned with its radio glyph: scale="+std::to_string(scale)
                    +" width="+std::to_string(width)+" option="+std::to_string(index)+" ink="+std::to_string(ink.top)+","+std::to_string(ink.bottom)
                    +" labelY="+std::to_string(label.top)+","+std::to_string(label.bottom)+" radioY="+std::to_string(radio.top)+","+std::to_string(radio.bottom));
        }
        require(view.answers_json()==original,"Aligning mathematical choice text changed a response");
    }
    std::cout<<"PASS actual introductory mathematical option ink centers align with radios at75/100/150percent and wrapped widths without answer changes\n";
}
void representation_parity_tests(HWND host){
    const auto raw=[&](const JsonObject& representation){auto block=simple_block(R"({"id":"value","type":"math_numeric_input"})");JsonArray values;values.Append(representation);block.SetNamedValue(L"representations",values);return winrt::to_string(block.Stringify());};
    const auto pixels=[&](const JsonObject& representation){MathPuzzleView view(host,raw(representation),"es","",canonical_callbacks());view.layout(0,0,700);const auto targets=class_targets(view.window(),L"LearnGameAndEngineDev.MathCanvas");require(targets.size()==1,"The original representation did not retain its one mathematical canvas");RECT area{};GetClientRect(targets.front(),&area);require(std::abs(area.right/1.2-area.bottom)<=1,"The static geometry/Venn viewport lost the original Android aspect ratio");return canvas_pixels(targets.front());};
    std::vector<std::uint64_t> solids;
    for(const auto shape:{"cube","prism","cylinder","cone","sphere"}){JsonObject value;value.SetNamedValue(L"type",JsonValue::CreateStringValue(L"geometry_3d_diagram"));value.SetNamedValue(L"shape",JsonValue::CreateStringValue(wide(shape)));solids.push_back(pixels(value));}
    require(solids[0]==solids[1]&&solids[2]!=solids[0]&&solids[3]!=solids[0]&&solids[4]!=solids[0]&&solids[2]!=solids[3]&&solids[3]!=solids[4],"Cylinder, cone or sphere were replaced by the prism drawing");
    auto venn=JsonObject::Parse(LR"({"type":"venn_diagram","leftLabel":"Pares","rightLabel":"Múltiplos de3","leftOnly":"2\n4\n8\n10","intersection":"6\n12","rightOnly":"3\n9","outside":"1,5,7,11","universeLabel":"Números del1al12"})");
    const auto universe=pixels(venn);venn.SetNamedValue(L"universeLabel",JsonValue::CreateStringValue(L"Otro universo"));require(pixels(venn)!=universe,"The original Venn universe label is omitted");
    auto actual=authored_puzzle("app/src/main/assets/content/es/math_physics/math_geometria_plana/fundamentos_geometricos/lesson_circunferencia_circulo_y_radios.json",1,"distinguir_interior_frontera_y_exterior_del_disco");
    auto callbacks=canonical_callbacks();const auto dispatch=callbacks.expression_request;unsigned layouts=0;JsonObject drawing;
    callbacks.expression_request=[&](std::string_view encoded){const auto request=JsonObject::Parse(wide(encoded));const auto result=dispatch(encoded);if(request.GetNamedString(L"action",L"")==L"diagram_layout"){++layouts;drawing=JsonObject::Parse(wide(result));}return result;};
    MathPuzzleView diagram(host,winrt::to_string(actual.Stringify()),"es","",std::move(callbacks));diagram.layout(0,0,700);const auto canvas=class_targets(diagram.window(),L"LearnGameAndEngineDev.MathCanvas").front();const auto original=diagram.answers_json();const auto filled=canvas_pixels(canvas);
    require(layouts==1&&drawing.GetNamedArray(L"circles").Size()==1&&drawing.GetNamedArray(L"circles").GetObjectAt(0).GetNamedBoolean(L"filled"),"Windows did not consume the original Euclidean disk layout");
    require(drawing.GetNamedArray(L"points").Size()==4&&drawing.GetNamedArray(L"labels").Size()==4,"The original Euclidean point names were discarded or generated twice");
    diagram.layout(0,0,700);(void)canvas_pixels(canvas);require(layouts==1,"Repainting unchanged Euclidean geometry invokes another layout/model process");
    diagram.layout(0,0,400);require(layouts==2&&diagram.answers_json()==original,"Resizing an original Euclidean drawing changed its answer or did not update its layout");
    const auto representation=actual.GetNamedArray(L"representations").GetObjectAt(0);auto boundary=JsonObject::Parse(representation.Stringify());auto circles=boundary.GetNamedArray(L"circles");auto circle=circles.GetObjectAt(0);circle.SetNamedValue(L"filled",JsonValue::CreateBooleanValue(false));circles.SetAt(0,circle);boundary.SetNamedValue(L"circles",circles);
    MathPuzzleView outline(host,raw(boundary),"es","",canonical_callbacks());outline.layout(0,0,700);require(canvas_pixels(class_targets(outline.window(),L"LearnGameAndEngineDev.MathCanvas").front())!=filled,"A declared Euclidean disk still renders only its boundary");
    const auto locked_raw=simple_block(R"({"id":"plot","type":"math_coordinate_graph_builder","bounds":{"xMin":-5,"xMax":5,"yMin":-5,"yMax":5},"snap":2,"points":[{"x":0,"y":0,"draggable":false},{"x":1,"y":1,"draggable":true}]})");
    MathPuzzleView locked(host,winrt::to_string(locked_raw.Stringify()),"es","");locked.layout(0,0,700);const auto fields=edits(locked.window());require(fields.size()==4&&!IsWindowEnabled(fields[0])&&!IsWindowEnabled(fields[1])&&IsWindowEnabled(fields[2])&&IsWindowEnabled(fields[3]),"An authored fixed graph point exposes editable coordinates");
    locked.set_enabled(false);locked.set_enabled(true);require(!IsWindowEnabled(fields[0])&&IsWindowEnabled(fields[2]),"Review re-enabled an authored fixed graph point");
    if(!reader_theme::palette(fields[0]).high_contrast)for(const auto input:{fields[0],fields[1]}){RECT area{};GetClientRect(input,&area);const auto value=edit_text(input);const auto answers=locked.answers_json();require((GetWindowLongPtrW(input,GWL_STYLE)&ES_READONLY)&&painted_points(input,{{area.right/2,std::max(1L,area.bottom-8)}}).front()==reader_theme::background(input),"An initially fixed graph coordinate lost its native read-only protection or original dark surface");SendMessageW(input,WM_CHAR,L'9',1);require(edit_text(input)==value&&locked.answers_json()==answers,"An authored fixed graph coordinate accepted a character");}
    const auto points=JsonObject::Parse(wide(locked.answers_json())).GetNamedObject(L"plot").GetNamedArray(L"points");require(points.GetObjectAt(0).GetNamedNumber(L"x")==0&&points.GetObjectAt(1).GetNamedNumber(L"x")==2,"Graph snapping was anchored to the minimum instead of the original zero-based grid");
    auto numberline=JsonObject::Parse(LR"({"type":"math_number_line","min":-10,"max":10,"step":0.01,"value":2,"mode":"read_value"})");
    const auto line_pixels=[&](const JsonObject& source){MathPuzzleView value(host,raw(source),"es","");value.layout(0,0,700);return canvas_pixels(class_targets(value.window(),L"LearnGameAndEngineDev.MathCanvas").front());};
    const auto hidden_value=line_pixels(numberline);numberline.SetNamedValue(L"showValueLabel",JsonValue::CreateBooleanValue(false));numberline.SetNamedValue(L"closed",JsonValue::CreateBooleanValue(true));require(line_pixels(numberline)==hidden_value,"The read-value number line revealed its answer or omitted Android's default closed marker");
    numberline.SetNamedValue(L"showValueLabel",JsonValue::CreateBooleanValue(true));require(line_pixels(numberline)!=hidden_value,"The explicit number-line value label remains omitted");
    numberline.SetNamedValue(L"showValueLabel",JsonValue::CreateBooleanValue(false));numberline.SetNamedValue(L"closed",JsonValue::CreateBooleanValue(false));require(line_pixels(numberline)!=hidden_value,"An authored open marker is indistinguishable from the original closed marker");
    const auto partial=simple_block(R"({"id":"fields","type":"math_multi_input","segments":[{"text":"x ="},{"field":"x"}],"fields":[{"id":"x","type":"numeric"},{"id":"extra","type":"numeric","label":"Dato adicional","unit":"cm"},{"id":"fixed","type":"fixed","text":"Referencia conservada"}]})");
    MathPuzzleView template_view(host,winrt::to_string(partial.Stringify()),"es","");template_view.layout(0,0,700);require(edits(template_view.window()).size()==2&&region_target(template_view,"fields/field/extra")&&caption_target(template_view.window(),L"cm")&&caption_target(template_view.window(),L"Referencia conservada"),"A partial mathematical template hid its extra original fields, labels, units or fixed reference");
    auto graph=JsonObject::Parse(LR"({"type":"math_coordinate_graph","bounds":{"xMin":-10,"xMax":10,"yMin":-5,"yMax":5},"gridStep":1,"points":[{"x":-2,"y":-1},{"x":2,"y":-1},{"x":0,"y":2}],"mode":"polygon","showLabels":false})");
    const auto dense_grid=line_pixels(graph);graph.SetNamedValue(L"gridStep",JsonValue::CreateNumberValue(2));require(line_pixels(graph)!=dense_grid,"A Cartesian representation ignores its authored grid step");
    const auto hidden_coordinates=line_pixels(graph);graph.SetNamedValue(L"showLabels",JsonValue::CreateBooleanValue(true));require(line_pixels(graph)!=hidden_coordinates,"An authored graph's coordinate labels remain omitted");
    graph.SetNamedValue(L"showLabels",JsonValue::CreateBooleanValue(false));graph.SetNamedValue(L"mode",JsonValue::CreateStringValue(L"scatter"));require(line_pixels(graph)!=hidden_coordinates,"A polygon was reduced to unrelated unconnected points");
    std::cout<<"PASS original geometry shapes/Venn labels, canonical Euclidean fill/point-label layout cache and fixed-point zero-based graph controls\n";
}
void rich_content_layout_tests(HWND host){
    const auto build=[](const JsonObject& representation){auto block=simple_block(R"({"id":"answer","type":"math_numeric_input"})");JsonArray values;values.Append(representation);block.SetNamedValue(L"representations",values);return winrt::to_string(block.Stringify());};
    const auto table=JsonObject::Parse(LR"({"type":"math_table","headers":["Magnitud conservada","Valor inicial","Valor final","Comprobación"],"rows":[["Longitud horizontal","12","24","2 × 12 = 24"],["Área total","144","576","4 × 144 = 576"]]})");
    MathPuzzleView table_view(host,build(table),"es","");table_view.layout(0,0,330);const auto panels=class_targets(table_view.window(),L"LearnGameAndEngineDev.MathContent");require(panels.size()==1,"The original table is missing its one horizontal content container");
    const auto first=caption_target(table_view.window(),L"Magnitud conservada"),second=caption_target(table_view.window(),L"Valor inicial");require(first&&second,"A table heading disappeared");
    RECT first_before{},second_before{};GetWindowRect(first,&first_before);GetWindowRect(second,&second_before);require(first_before.right<=second_before.left&&first_before.right-first_before.left>=MulDiv(70,static_cast<int>(GetDpiForWindow(first)),96),"A table compressed or overlapped its original columns");
    CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(first,EM_SETSEL,0,1);SendMessageW(first,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));SendMessageW(first,EM_SETSEL,0,0);require(format.dwEffects&CFE_BOLD,"The original table header lost its bold typeface");
    SCROLLINFO scroll{sizeof(scroll),SIF_ALL};if(!GetScrollInfo(panels.front(),SB_HORZ,&scroll)||scroll.nMax+1<=static_cast<int>(scroll.nPage)){RECT area{};GetClientRect(panels.front(),&area);throw std::runtime_error("An overflowing table lost its actual horizontal scroll range: client="+std::to_string(area.right)+", maximum="+std::to_string(scroll.nMax)+", page="+std::to_string(scroll.nPage)+", firstColumn="+std::to_string(first_before.right-first_before.left));}const auto original=table_view.answers_json();SendMessageW(panels.front(),WM_HSCROLL,SB_LINERIGHT,0);RECT first_after{};GetWindowRect(first,&first_after);require(first_after.left<first_before.left&&first_after.top==first_before.top&&table_view.answers_json()==original,"Horizontal table scrolling changed data or failed to move its same native cells");
    table_view.layout(0,0,2000);require(!(GetWindowLongPtrW(panels.front(),GWL_STYLE)&WS_HSCROLL),"A fitting table kept an unnecessary horizontal scrollbar");
    const auto formula=JsonObject::Parse(LR"({"type":"math_formula","latex":"\\frac{123456789}{987654321} + \\frac{123456789}{987654321} + \\frac{123456789}{987654321} + \\frac{123456789}{987654321}","display":"block"})");
    MathPuzzleView formula_view(host,build(formula),"es","");formula_view.layout(0,0,330);const auto formula_panels=class_targets(formula_view.window(),L"LearnGameAndEngineDev.MathContent");require(formula_panels.size()==1&&GetScrollInfo(formula_panels.front(),SB_HORZ,&scroll)&&scroll.nMax+1>static_cast<int>(scroll.nPage),"A wide block formula wrapped/clipped instead of preserving Android's horizontal viewport");
    auto inline_formula=JsonObject::Parse(LR"({"type":"math_formula","latex":"x + 1","display":"inline"})");MathPuzzleView inline_view(host,build(inline_formula),"es","");inline_view.layout(0,0,330);require(class_targets(inline_view.window(),L"LearnGameAndEngineDev.MathContent").empty(),"An inline formula acquired a block scroll container");
    HWND inline_target=nullptr;for(const auto& region:inline_view.reading_regions())if(region.source_text=="x + 1")inline_target=region.target;require(inline_target,"The inline formula lost its exact original narration source");format={};format.cbSize=sizeof(format);SendMessageW(inline_target,EM_SETSEL,0,1);SendMessageW(inline_target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));require(format.yHeight==240,"An inline formula lost Android's16sp text size");
    const auto ordering=simple_block(R"({"id":"order","type":"math_ordering_drag","initialOrder":["b","a"],"items":[{"id":"a","label":"Sólo accesibilidad","content":[{"type":"text","text":"La tarjeta conserva su contenido"},{"type":"math_formula","latex":"\\frac{1}{2}"},{"type":"math_table","headers":["Parte","Valor"],"rows":[["Primera","1/2"]]}],"explanation":"Su explicación original"},{"id":"b","label":"Segunda tarjeta"}]})");
    MathPuzzleView ordered(host,winrt::to_string(ordering.Stringify()),"es","");ordered.layout(0,0,650);const auto content=region_target(ordered,"order/item/a"),fraction=region_target(ordered,"order/item/a/content/1");require(content&&fraction&&native_math_text_mapping(fraction,"\\frac{1}{2}").has_value(),"An enriched ordering card was flattened or replaced by its accessibility label");
    for(const auto& region:ordered.reading_regions())require(region.source_text!="Sólo accesibilidad","An enriched ordering card displayed its alternative accessibility label as duplicate content");
    const auto original_fraction=fraction;click_card(fraction);require(JsonObject::Parse(wide(ordered.answers_json())).GetNamedObject(L"order").GetNamedArray(L"ids").GetStringAt(0)==L"a"&&region_target(ordered,"order/item/a/content/1")==original_fraction,"A nested formula did not move its whole original ordering card or replaced its source-to-word mapping");
    const auto matching=simple_block(R"({"id":"pairs","type":"math_matching_drag","sources":[{"id":"a","label":"Alternativa accesible","content":[{"type":"math_formula","latex":"\\frac{1}{2}"}]}],"targets":[{"id":"x","label":"Una mitad"}]})");
    MathPuzzleView matched(host,winrt::to_string(matching.Stringify()),"es","");matched.layout(0,0,650);const auto source=region_target(matched,"pairs/source/a"),target=region_target(matched,"pairs/target/x"),empty=caption_target(matched.window(),L"Sin asignar");require(source&&target&&empty&&(GetWindowLongPtrW(empty,GWL_STYLE)&WS_VISIBLE),"An unmatched target omitted Android's original empty-slot label");
    click_card(source);click_card(target);require(!(GetWindowLongPtrW(empty,GWL_STYLE)&WS_VISIBLE)&&region_target(matched,"pairs/source/a")==source,"Matching a rich card retained its empty-slot label or replaced its same mathematical content");
    click_card(target);require(GetWindowLongPtrW(empty,GWL_STYLE)&WS_VISIBLE,"Clearing a matching target failed to restore its original empty-slot label");
    std::cout<<"PASS source table column geometry/header styles, real horizontal scroll, block/inline formula layout and movable enriched ordering/matching cards\n";
}
void rich_option_content_tests(HWND host){
    const auto verify=[&](const JsonObject& block,std::string_view response_id,unsigned count,unsigned graphs){
        MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","");view.layout(0,0,850);
        auto toggles=class_targets(view.window(),L"BUTTON");std::erase_if(toggles,[](HWND item){const auto type=GetWindowLongPtrW(item,GWL_STYLE)&BS_TYPEMASK;return type!=BS_RADIOBUTTON&&type!=BS_CHECKBOX;});
        std::ranges::sort(toggles,[](HWND a,HWND b){RECT one{},two{};GetWindowRect(a,&one);GetWindowRect(b,&two);return one.top<two.top;});require(toggles.size()==count,"A rich option lost its original radio/checkbox");
        require(class_targets(view.window(),L"LearnGameAndEngineDev.MathCanvas").size()==graphs,"A labelled rich option discarded an authored graph or substituted a plain label");
        JsonObject response;for(const auto& value:block.GetNamedArray(L"responses"))if(value.GetObject().GetNamedString(L"id")==winrt::to_hstring(response_id))response=value.GetObject();
        require(response.HasKey(L"options"),"The real rich option response is absent");const auto options=response.GetNamedArray(L"options");
        for(unsigned index=0;index<options.Size();++index){const auto option=options.GetObjectAt(index);const auto key=std::string(response_id)+"/option/"+winrt::to_string(option.GetNamedString(L"id"));const auto body=region_target(view,key);require(body,"A rich option lost its original narratable body");
            // Android aligns the complete rich column with its radio. A
            // formula/table retains its own inner padding inside that column.
            auto column=body;while(GetParent(column)!=view.window()&&GetParent(column))column=GetParent(column);
            RECT radio{},label{},column_bounds{};GetWindowRect(toggles[index],&radio);GetWindowRect(body,&label);GetWindowRect(column,&column_bounds);
            require(column_bounds.left>=radio.right&&column_bounds.top==radio.top&&column_bounds.bottom>radio.top&&label.left>=column_bounds.left&&label.top>=column_bounds.top&&label.bottom<=column_bounds.bottom,"A rich answer was placed below its radio instead of beside it");
            for(const auto& region:view.reading_regions())if(region.target==body){ReadingShade shade(body,region.source_text);require(shade.matches(body,region.source_text),"A rich option lost its original-source word shading projection");shade.read_through(0);shade.read_through(wide(region.source_text).size());shade.restore();}
            CHARRANGE selection{};SendMessageW(body,EM_SETSEL,0,0);click_card(body);SendMessageW(body,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
            const auto selected=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(winrt::to_hstring(response_id)).GetNamedArray(L"selected");
            bool found=false;for(const auto& value:selected)found=found||value.GetString()==option.GetNamedString(L"id");require(found&&selection.cpMin==selection.cpMax,"A rich content click selected text instead of its answer");
        }
        const auto saved=view.answers_json();MathPuzzleView reopened(host,winrt::to_string(block.Stringify()),"es",saved);reopened.layout(0,0,850);require(reopened.answers_json()==saved,"Rich option rendering changed restored answers or the other fields of its compound puzzle");
    };
    const auto single=authored_puzzle("app/src/main/assets/content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json",1,"single_rich_option");verify(single,"choice",3,2);
    const auto multiple=authored_puzzle("app/src/main/assets/content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json",2,"multi_rich_option");verify(multiple,"choices",4,0);
    {MathPuzzleView table(host,winrt::to_string(multiple.Stringify()),"es","");table.layout(0,0,850);require(caption_target(table.window(),L"Parte")&&caption_target(table.window(),L"Total")&&!caption_target(table.window(),L"Proporción: 3 de 6"),"An option's accessibility label replaced its original visible table");}
    const auto programming=authored_puzzle("app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json",4,"cpp_fundamentals__repeticion_y_terminacion__despacho_y_recuperacion_de_recorridos__enumerar_y_justificar_el_tablero");verify(programming,"justificacion_del_recorrido",3,0);
    std::cout<<"PASS seven authored labelled rich options retain formulas/tables/graphs and original shading; programming compound radios align with rich bodies, click and reopen without answer changes\n";
}
void field_column_parity_tests(HWND host){
    const auto block=simple_block(R"({"id":"fields","type":"math_multi_input","layout":"table","headers":["Primera magnitud","Segunda magnitud","Tercera magnitud"],"rows":[[{"field":"a"},{"field":"b"},{"field":"c"}]],"fields":[{"id":"a","label":"Longitud","unit":"cm"},{"id":"b","label":"Tiempo","unit":"s"},{"id":"c","label":"Cantidad"},{"id":"d","label":"Campo adicional"}]})");
    MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es",R"({"fields":{"type":"fields","values":{"a":"12","b":"3","c":"4","d":"7"}}})");view.layout(0,0,330);
    const auto fields=edits(view.window());require(fields.size()==4,"A source table/partial template discarded a field");
    const auto field=[&](std::wstring_view id){for(const auto item:fields)if(JsonObject::Parse(wide(math_field_keyboard_context(item))).GetNamedString(L"id")==id)return item;return HWND{};};const auto a=field(L"a"),b=field(L"b"),c=field(L"c"),d=field(L"d");require(a&&b&&c&&d,"A source field lost its original semantic identity");
    RECT first{},second{},caption{},unit{};GetWindowRect(a,&first);GetWindowRect(b,&second);GetWindowRect(region_target(view,"fields/field/a"),&caption);GetWindowRect(region_target(view,"fields/field/a/unit"),&unit);
    require(first.right-first.left==MulDiv(150,static_cast<int>(GetDpiForWindow(host)),96)&&second.left>=first.right&&caption.bottom<=first.top&&unit.top>=first.bottom,"Field tables did not retain their original150dp columns with labels and units around each input");
    require(GetNextDlgTabItem(view.window(),a,FALSE)==b&&GetNextDlgTabItem(view.window(),b,FALSE)==c&&GetNextDlgTabItem(view.window(),c,FALSE)==d,"Mathematical field focus did not follow the original visual/source order");
    // RichEdit's complete story can include its virtual final paragraph mark.
    // Derive that native CP boundary from an unmodified peer, rather than
    // confusing the two visible characters with a two-CP complete selection.
    const auto original=view.answers_json();const auto value=edit_text(a);const auto baseline=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|ES_AUTOHSCROLL,0,0,150,48,host,nullptr,GetModuleHandleW(nullptr),nullptr);require(baseline,"Cannot observe the native single-line selection boundary");set_edit_text(baseline,value);SendMessageW(baseline,EM_SETSEL,0,-1);CHARRANGE complete{};SendMessageW(baseline,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&complete));DestroyWindow(baseline);
    if(GetFocus()==a)SetFocus(b);SetFocus(a);CHARRANGE selection{};SendMessageW(a,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));require(GetFocus()==a&&complete.cpMin==0&&complete.cpMax>=static_cast<LONG>(wide(value).size())&&selection.cpMin==complete.cpMin&&selection.cpMax==complete.cpMax&&edit_text(a)==value&&view.answers_json()==original,"Entering a mathematical field did not select its existing value without modifying the answer");
    HWND table=nullptr;for(const auto panel:class_targets(view.window(),L"LearnGameAndEngineDev.MathContent")){SCROLLINFO scroll{sizeof(scroll),SIF_ALL};if(GetScrollInfo(panel,SB_HORZ,&scroll)&&scroll.nMax+1>static_cast<int>(scroll.nPage)){table=panel;break;}}
    require(table,"A150dp field table compressed its columns instead of allowing real horizontal scrolling");SendMessageW(table,WM_HSCROLL,SB_LINERIGHT,0);RECT shifted{};GetWindowRect(a,&shifted);require(shifted.left<first.left&&view.answers_json()==original,"Scrolling a field table changed its data or failed to move the same input");
    const auto inline_block=simple_block(R"({"id":"equation","type":"math_equation_fill_blanks","segments":[{"text":"12 + "},{"field":"n"},{"text":" = 20"}],"fields":[{"id":"n","label":"Sumando","unit":"cm"}]})");MathPuzzleView inline_view(host,winrt::to_string(inline_block.Stringify()),"es","");inline_view.layout(0,0,200);
    const auto input=edits(inline_view.window()).front();require(!caption_target(inline_view.window(),L"Sumando")&&caption_target(inline_view.window(),L"cm"),"An inline field displayed its accessibility label or dropped its unit");
    const auto anchor=region_target(inline_view,"equation/field/n");require(anchor&&GetParent(input)==anchor,"An inline hole lost its original semantic narration anchor");
    std::cout<<"PASS source field columns,150dp horizontal tables, labels/units, partial templates, inline semantic holes and select-all-on-focus\n";
}
void mathematical_input_hint_tests(HWND host){
    const auto block=simple_block(R"({"id":"number","type":"math_numeric_input"})");
    for(const auto& [language,hint]:std::vector<std::pair<std::string,std::string>>{{"es","Respuesta"},{"en","Answer"},{"fr","Réponse"}}){
        MathPuzzleView view(host,winrt::to_string(block.Stringify()),language,"");view.layout(0,0,650);const auto field=edits(view.window()).front();SetFocus(field);SendMessageW(field,EM_SETSEL,0,0);require(GetFocus()==field,"The mathematical hint fixture did not obtain real editing focus");const auto modified=SendMessageW(field,EM_GETMODIFY,0,0),undo=SendMessageW(field,EM_CANUNDO,0,0);const auto visible=canvas_pixels(field);
        set_edit_hint(field,"");const auto empty=canvas_pixels(field);require(visible!=empty&&edit_text(field).empty()&&GetWindowTextLengthW(field)==0&&SendMessageW(field,EM_GETMODIFY,0,0)==modified&&SendMessageW(field,EM_CANUNDO,0,0)==undo,"The original mathematical answer hint is invisible, became document text or changed Undo/Modify");
        set_edit_hint(field,hint);if(!reader_theme::palette(field).high_contrast)require(canvas_pixels(field)!=visible,"The mathematical hint retained the generic C++ placeholder color instead of Android's#AEB7C6");
        set_edit_hint(field,hint,RGB(174,183,198));require(canvas_pixels(field)==visible,"A mathematical answer field lost its original localized hint and color");
        SendMessageW(field,WM_CHAR,L'7',1);const auto typed=canvas_pixels(field);require(edit_text(field)=="7"&&SendMessageW(field,EM_CANUNDO,0,0),"Typing into the hinted mathematical field changed its value or lost normal Undo");set_edit_hint(field,"");require(canvas_pixels(field)==typed,"The mathematical hint remained over the actual answer");set_edit_hint(field,hint,RGB(174,183,198));SendMessageW(field,WM_UNDO,0,0);require(edit_text(field).empty(),"A mathematical hint altered native Undo instead of restoring the empty answer");require(GetFocus()==field,"Undo or hint drawing moved the mathematical editing focus");const auto restored=canvas_pixels(field);if(restored!=visible){CHARRANGE selection{};SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(field,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));std::cerr<<"INFO focused mathematical hint language="<<language<<" before="<<visible<<" after_undo="<<restored<<" selection="<<selection.cpMin<<","<<selection.cpMax<<" default_height="<<format.yHeight<<" default_color="<<format.crTextColor<<"\n";}require(restored==visible,"The original mathematical hint did not return in the same focused empty state after Undo");
    }
    std::cout<<"PASS original localized mathematical hints, painted-only empty state, unchanged document/Modify/Undo and normal typing/Undo restoration\n";
}
void mathematical_input_ink_tests(HWND host){
    const auto block=simple_block(R"({"id":"number","type":"math_numeric_input","label":"Resultado","validator":{"type":"numeric","expected":600}})");
    MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","");view.layout(0,0,650);const auto field=edits(view.window()).front();require(edit_text(field).empty(),"The numerical ink fixture is not initially empty");
    SetFocus(field);for(const auto character:{L'6',L'0'})SendMessageW(field,WM_CHAR,character,1);require(edit_text(field)=="60","Normal numeric keystrokes changed their original value");
    const auto expected=reader_theme::foreground(field);CHARFORMAT2W insertion{};insertion.cbSize=sizeof(insertion);SendMessageW(field,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&insertion));require((insertion.dwMask&CFM_COLOR)&&!(insertion.dwEffects&CFE_AUTOCOLOR)&&insertion.crTextColor==expected,"The mathematical RichEdit insertion format retained automatic black text");
    SendMessageW(field,EM_SETSEL,0,2);CHARFORMAT2W typed{};typed.cbSize=sizeof(typed);SendMessageW(field,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&typed));require(!(typed.dwEffects&CFE_AUTOCOLOR)&&typed.crTextColor==expected,"Physical-style WM_CHAR insertion did not inherit the original mathematical text color");SendMessageW(field,EM_SETSEL,2,2);
    RECT bounds{};GetClientRect(field,&bounds);const std::vector<POINT> background_point{{bounds.right/2,std::max(1L,bounds.bottom-8)}};const auto background=reader_theme::background(field);require(painted_points(field,background_point).front()==background,"An editable numerical field did not retain its mathematical surface");
    const auto field_colors=reader_theme::palette(field);if(!field_colors.high_contrast)require(background==RGB(28,28,28),"A simple mathematical input lost Android's original#1C1C1C card background");
    const auto field_dpi=static_cast<int>(GetDpiForWindow(field));const std::vector<POINT> border_points{{bounds.right/2,0},{0,bounds.bottom/2},{MulDiv(5,field_dpi,96),0}};
    const auto border=painted_points(field,border_points);require(border[0]==field_colors.accent&&border[1]==field_colors.accent&&border[2]==background,
        "The simple mathematical input lost its permanent accent outline or original10dp corner radius");
    CHARRANGE painted_selection{};SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&painted_selection));require(painted_selection.cpMin==2&&painted_selection.cpMax==2&&edit_text(field)=="60",
        "Painting the original mathematical field outline moved the native caret or changed its value");
    const auto visible_ink=[&]{POINT first{},end{};SendMessageW(field,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);SendMessageW(field,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),2);const auto dc=GetDC(field);const auto before=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(field,WM_GETFONT,0,0)));TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,before);ReleaseDC(field,dc);std::vector<POINT> points;for(int y=std::max(1L,first.y);y<std::min(bounds.bottom-4,first.y+metrics.tmHeight);++y)for(int x=std::max(1L,first.x);x<std::min(bounds.right-4,end.x);++x)points.push_back({x,y});const auto pixels=painted_points(field,points);return std::ranges::count_if(pixels,[&](COLORREF color){return color!=background&&GetRValue(color)>128&&GetGValue(color)>128&&GetBValue(color)>128;});};
    if(!reader_theme::palette(field).high_contrast)require(visible_ink()>8,"The actual numeric glyphs were painted black on the dark mathematical input");
    const auto answers=view.answers_json();const auto undo_available=SendMessageW(field,EM_CANUNDO,0,0);view.set_enabled(false);require(!IsWindowEnabled(field)&&(GetWindowLongPtrW(field,GWL_STYLE)&WS_DISABLED)&&edit_text(field)=="60"&&view.answers_json()==answers&&painted_points(field,background_point).front()==background,"Completing a numerical response changed its value or replaced its dark field with a disabled system background");if(!reader_theme::palette(field).high_contrast){require(visible_ink()>8,"A disabled numerical field lost its legible original glyph color");require(GetWindowLongPtrW(field,GWL_STYLE)&ES_READONLY,"The disabled mathematical field did not retain native read-only protection");SendMessageW(field,WM_CHAR,L'9',1);require(edit_text(field)=="60"&&view.answers_json()==answers,"A disabled read-only mathematical field accepted a character or changed its response");}
    require(painted_points(field,border_points)==border,"Completing a mathematical answer changed its original accent outline or corner surface");
    view.set_enabled(true);require(IsWindowEnabled(field)&&!(GetWindowLongPtrW(field,GWL_STYLE)&ES_READONLY)&&edit_text(field)=="60"&&painted_points(field,background_point).front()==background&&SendMessageW(field,EM_CANUNDO,0,0)==undo_available,"Re-enabling a mathematical field changed its original read-only state, data, undo availability or surface");
    MathPuzzleView reopened(host,winrt::to_string(block.Stringify()),"es",answers);reopened.layout(0,0,650);const auto restored=edits(reopened.window()).front();SendMessageW(restored,EM_SETSEL,0,2);typed={};typed.cbSize=sizeof(typed);SendMessageW(restored,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&typed));require(edit_text(restored)=="60"&&typed.crTextColor==reader_theme::foreground(restored)&&!(typed.dwEffects&CFE_AUTOCOLOR),"A restored numerical response reverted to automatic black text");SendMessageW(restored,EM_SETREADONLY,TRUE,0);reopened.set_enabled(false);reopened.set_enabled(true);require((GetWindowLongPtrW(restored,GWL_STYLE)&ES_READONLY)&&edit_text(restored)=="60","Restoring the original enabled state discarded a pre-existing native read-only restriction");
    std::cout<<"PASS numerical WM_CHAR insertion/restoration color, actual legible glyphs, disabled/enabled native surface and unchanged responses\n";
}
void automatic_mathematics_tests(HWND host){
    const std::string original="🙂 La mitad es 1/2, el punto (1,2) y la potencia x². Conserva `std::cout` y 2 × 3 = 6; texto posterior.";
    auto block=simple_block(R"({"id":"n","type":"math_numeric_input"})");JsonObject representation;representation.SetNamedValue(L"type",JsonValue::CreateStringValue(L"text"));representation.SetNamedValue(L"text",JsonValue::CreateStringValue(wide(original)));JsonArray representations;representations.Append(representation);block.SetNamedValue(L"representations",representations);
    MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","",canonical_callbacks());view.layout(0,0,700);HWND target=nullptr;for(const auto& region:view.reading_regions())if(region.source_text==original)target=region.target;require(target,"Automatic formula presentation lost its original narration source");
    const auto mapping=native_math_text_mapping(target,original);require(mapping&&mapping->built_up&&mapping->source_to_native.size()==wide(original).size()+1&&std::ranges::is_sorted(mapping->source_to_native),"The original automatic formula detector lost its UTF16 source-to-native projection");
    const auto visible=math_style_text(mapping->native_text);require(visible.find(L"std::cout")!=visible.npos&&visible.find(L'×')!=visible.npos&&visible.find(L';')!=visible.npos&&visible.find(L"texto posterior")!=visible.npos,"Automatic typesetting changed code, an original multiplication/punctuation sign or surrounding prose");
    ReadingShade shade(target,original);require(shade.matches(target,original),"Implicit fractions, coordinates and powers broke original-source word shading");shade.read_through(0);shade.read_through(wide(original).find(L"1/2")+1);shade.read_through(wide(original).size());shade.restore();
    // WinRT JSON collections retain COM identity: mutate an independent block
    // for the plain-text exception, leaving the later Auto fixture unchanged.
    auto excluded_block=JsonObject::Parse(block.Stringify());auto plain=JsonObject::Parse(representation.Stringify());plain.SetNamedValue(L"mathMode",JsonValue::CreateStringValue(L"text"));excluded_block.GetNamedArray(L"representations").SetAt(0,plain);MathPuzzleView excluded(host,winrt::to_string(excluded_block.Stringify()),"es","",canonical_callbacks());excluded.layout(0,0,700);for(const auto& region:excluded.reading_regions())if(region.source_text==original)require(!native_math_text_mapping(region.target,original),"mathMode:text no longer preserves the original plain-text exception");
    const auto before=edit_text(target);bool rejected=false;try{const std::vector<NativeMathFragment> invalid{{1,2,"1",false}};(void)set_native_math_fragments(target,original,invalid);}catch(const std::invalid_argument&){rejected=true;}require(rejected&&edit_text(target)==before,"A split-surrogate formula range mutated the existing visible source");
    const auto request=canonical_callbacks().expression_request;
    MathPuzzleView late(host,winrt::to_string(block.Stringify()),"es","");late.layout(0,0,700);late.set_text_scale(150);const auto late_input=edits(late.window()).front();replace(late_input,"12");SendMessageW(late_input,EM_SETSEL,0,1);HWND late_label=nullptr;for(const auto& region:late.reading_regions())if(region.source_text==original)late_label=region.target;require(late_label&&!native_math_text_mapping(late_label,original),"A missing canonical worker unexpectedly fabricated implicit formula ranges");const auto late_saved=late.answers_json();late.set_expression_request(request);CHARRANGE late_selection{};SendMessageW(late_input,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&late_selection));int numerator=0,denominator=0;SendMessageW(late_label,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));const auto late_mapping=native_math_text_mapping(late_label,original);const bool late_ok=late_mapping&&late_mapping->built_up&&edits(late.window()).front()==late_input&&late.answers_json()==late_saved&&edit_text(late_input)=="12"&&late_selection.cpMin==0&&late_selection.cpMax==1&&numerator*100==denominator*150;
    require(late_ok,"Connecting the canonical worker after window creation replaced a field, changed its draft/caret/scale or left Auto formulas unapplied");
    const std::vector<lgaed::curriculum::Emphasis> emphasis{{"1/2",0,"bold"},{"texto posterior.",0,"italic"}};
    require(set_original_math_text(target,original,"auto",request,emphasis),"The reusable lesson renderer did not apply the original implicit-formula policy");
    const auto prose_mapping=native_math_text_mapping(target,original);require(prose_mapping&&prose_mapping->built_up,"Lesson Auto presentation lost its original-source narration mapping");
    for(const auto& entry:emphasis){const auto at=original.find(entry.text);const auto first=wide(std::string_view(original).substr(0,at)).size(),last=wide(std::string_view(original).substr(0,at+entry.text.size())).size();CHARRANGE selected{static_cast<LONG>(prose_mapping->source_to_native.at(first)),static_cast<LONG>(prose_mapping->source_to_native.at(last))};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));CHARFORMAT2W style{};style.cbSize=sizeof(style);SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));require(entry.style=="bold"?(style.dwEffects&CFE_BOLD)!=0:(style.dwEffects&CFE_ITALIC)!=0,"Implicit formula projection discarded source-owned bold or italic emphasis");}
    ReadingShade emphasized_shade(target,original);require(emphasized_shade.matches(target,original),"TOM emphasis changed UTF16 ownership and disconnected word shading");emphasized_shade.read_through(wide(std::string_view(original).substr(0,original.find("1/2")+3)).size());emphasized_shade.read_through(wide(original).size());emphasized_shade.restore();require(native_math_text_mapping(target,original).has_value(),"Reading emphasized formulas invalidated their original-source projection");
    for(const auto role:{RichTextRole::prompt,RichTextRole::heading,RichTextRole::lesson_title})require(set_original_math_text(target,"Relación 6 × 100 = 600","auto",request,{},role)&&native_math_text_mapping(target,"Relación 6 × 100 = 600"),"The original Auto formula policy was not reusable by lesson titles, headings and notes");
    require(!set_original_math_text(target,original,"text",request,emphasis)&&!native_math_text_mapping(target,original)&&edit_text(target).find("1/2")!=std::string::npos,"Switching an existing lesson control to mathMode:text retained stale native math ownership");
    std::cout<<"PASS original automatic fractions/coordinates/powers, original operators/prose/C++, UTF16 source mapping, word shading and explicit plain-text exceptions\n";
}
void rigid_angle_copy_tests(HWND host){
    const auto figure=[](std::string vertex,std::string first,std::string second,double x,double y,double angle,double first_distance,double second_distance){
        JsonObject result,diagram;JsonArray points,objects;
        const auto point=[&](const std::string& id,double px,double py){JsonObject value;value.SetNamedValue(L"id",JsonValue::CreateStringValue(wide(id)));value.SetNamedValue(L"x",JsonValue::CreateNumberValue(px));value.SetNamedValue(L"y",JsonValue::CreateNumberValue(py));JsonArray labels;labels.Append(JsonValue::CreateStringValue(wide(id)));value.SetNamedValue(L"labels",labels);points.Append(value);};
        point(vertex,x,y);point(first,x+std::cos(angle)*first_distance,y+std::sin(angle)*first_distance);point(second,x+std::cos(angle+1)*second_distance,y+std::sin(angle+1)*second_distance);
        for(const auto& [id,endpoint]:std::vector<std::pair<std::string,std::string>>{{"first",first},{"second",second}}){JsonObject item;item.SetNamedValue(L"id",JsonValue::CreateStringValue(wide(id)));item.SetNamedValue(L"kind",JsonValue::CreateStringValue(L"ray"));JsonArray defining;defining.Append(JsonValue::CreateStringValue(wide(vertex)));defining.Append(JsonValue::CreateStringValue(wide(endpoint)));item.SetNamedValue(L"through",defining);item.SetNamedValue(L"contains",defining);objects.Append(item);}
        diagram.SetNamedValue(L"points",points);diagram.SetNamedValue(L"objects",objects);result.SetNamedValue(L"diagram",diagram);result.SetNamedValue(L"vertex",JsonValue::CreateStringValue(wide(vertex)));result.SetNamedValue(L"sides",JsonArray::Parse(LR"(["first","second"])"));result.SetNamedValue(L"region",JsonValue::CreateStringValue(L"counterclockwise"));return result;};
    auto validator=JsonObject::Parse(LR"({"type":"rigid_angle_copy","initial":{"x":7,"y":6,"rotation":0.2},"relation":"congruent","positionTolerance":0.1,"directionTolerance":0.01})");validator.SetNamedValue(L"reference",figure("O","A","B",1,2,0,4,5));validator.SetNamedValue(L"copy",figure("V","C","D",0,0,.7,6,3));
    auto block=simple_block(R"({"id":"copy","type":"math_rigid_angle_copy"})");auto response=block.GetNamedArray(L"responses").GetObjectAt(0);response.SetNamedValue(L"validator",validator);const auto raw=winrt::to_string(block.Stringify());
    auto callbacks=canonical_callbacks();const auto dispatch=callbacks.expression_request;JsonObject drawing;callbacks.expression_request=[&](std::string_view request){const auto reply=dispatch(request);drawing=JsonObject::Parse(wide(reply));return reply;};
    MathPuzzleView view(host,raw,"es","",callbacks);view.layout(0,0,850);const auto canvas=class_targets(view.window(),L"LearnGameAndEngineDev.MathCanvas").front();const auto initial=view.answers_json();
    require(drawing.GetNamedArray(L"points").Size()==6&&drawing.GetNamedArray(L"arrows").Size()==4&&drawing.GetNamedArray(L"regions").Size()==2,"A rigid-angle copy substituted a graph or dropped the original rays, marks or regions");
    const auto moving=caption_target(view.window(),L"Mover copia"),rotating=caption_target(view.window(),L"Girar copia"),panning=caption_target(view.window(),L"Vista"),reset=caption_target(view.window(),L"Restablecer copia inicial");require(moving&&rotating&&panning&&reset,"A rigid-angle panel omitted an original mode or reset action");
    const auto drag=[&](POINT from,POINT to){SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(from.x,from.y));SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(to.x,to.y));SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(to.x,to.y));};
    const auto fixed=[&]{JsonArray result;for(const auto& value:drawing.GetNamedArray(L"points"))if(winrt::to_string(value.GetObject().GetNamedString(L"id")).starts_with("reference:"))result.Append(value);return result.Stringify();};const auto reference=fixed();
    const auto before_pixels=canvas_pixels(canvas);drag({300,250},{320,260});auto moved=JsonObject::Parse(wide(view.answers_json())).GetNamedObject(L"copy");require(view.answers_json()!=initial&&moved.GetNamedNumber(L"rotation")==.2&&fixed()==reference&&canvas_pixels(canvas)!=before_pixels,"Dragging did not translate the whole exact copy while keeping its reference and rotation fixed");
    SendMessageW(rotating,BM_CLICK,0,0);const auto center=drawing.GetNamedObject(L"copyCenter");const auto cx=static_cast<int>(std::lround(center.GetNamedNumber(L"x"))),cy=static_cast<int>(std::lround(center.GetNamedNumber(L"y")));drag({cx+80,cy},{cx+75,cy+20});const auto rotated=JsonObject::Parse(wide(view.answers_json())).GetNamedObject(L"copy");require(rotated.GetNamedNumber(L"x")==moved.GetNamedNumber(L"x")&&rotated.GetNamedNumber(L"y")==moved.GetNamedNumber(L"y")&&rotated.GetNamedNumber(L"rotation")!=moved.GetNamedNumber(L"rotation")&&fixed()==reference,"Rotating a rigid copy moved its vertex/reference or failed to preserve its original shape");
    SendMessageW(panning,BM_CLICK,0,0);const auto saved=view.answers_json();drag({300,250},{330,260});require(view.answers_json()==saved,"View mode modified a rigid-angle answer");
    MathPuzzleView reopened(host,raw,"es",saved,callbacks);reopened.layout(0,0,850);require(reopened.answers_json()==saved,"Reopening a rigid-angle attempt changed its original placement");
    SendMessageW(canvas,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(30,30));const auto expanded=GetParent(canvas);require(expanded!=view.window()&&GetParent(rotating)==expanded&&GetParent(reset)==expanded,"Expanding a rigid-angle figure detached its editing/reset controls");SendMessageW(reset,BM_CLICK,0,0);require(view.answers_json()==initial,"Expanded reset did not restore the original initial copy");SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);require(GetParent(canvas)==view.window()&&GetParent(rotating)==view.window()&&view.answers_json()==initial,"Closing an expanded rigid panel lost controls or changed its placement");
    view.set_enabled(false);require(!IsWindowEnabled(canvas)&&!IsWindowEnabled(moving)&&!IsWindowEnabled(rotating)&&!IsWindowEnabled(panning)&&!IsWindowEnabled(reset),"An evaluated rigid-angle response remained editable");
    std::cout<<"PASS original rigid-angle geometry, fixed reference, whole-copy translation/rotation, view mode, reset, saved placement, fullscreen controls and solved-state freeze\n";
}
RECT rectangle_fill_bounds(HWND canvas,SIZE* outline_size=nullptr){
    RECT bounds{};GetClientRect(canvas,&bounds);const auto dc=GetDC(canvas),buffer=CreateCompatibleDC(dc);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(canvas,dc);
    require(buffer&&bitmap&&pixels,"Cannot inspect the real slider preview rectangle");const auto previous=SelectObject(buffer,bitmap);SendMessageW(canvas,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);GdiFlush();
    const auto colors=reader_theme::palette(canvas);const auto blend=[](BYTE foreground,BYTE background){return (foreground*85+background*170)/255;};
    const auto fill=RGB(blend(GetRValue(colors.accent),GetRValue(colors.canvas)),blend(GetGValue(colors.accent),GetGValue(colors.canvas)),blend(GetBValue(colors.accent),GetBValue(colors.canvas)));
    const auto expected=(static_cast<std::uint32_t>(GetRValue(fill))<<16)|(static_cast<std::uint32_t>(GetGValue(fill))<<8)|GetBValue(fill);const auto values=static_cast<const std::uint32_t*>(pixels);
    RECT content{bounds.right,bounds.bottom,-1,-1};for(int y=0;y<bounds.bottom;++y)for(int x=0;x<bounds.right;++x)if((values[static_cast<std::size_t>(y)*bounds.right+x]&0xFFFFFF)==expected){content.left=std::min<LONG>(content.left,x);content.top=std::min<LONG>(content.top,y);content.right=std::max<LONG>(content.right,x+1);content.bottom=std::max<LONG>(content.bottom,y+1);}
    if(outline_size){
        const auto stroke=(static_cast<std::uint32_t>(GetRValue(colors.accent))<<16)|(static_cast<std::uint32_t>(GetGValue(colors.accent))<<8)|GetBValue(colors.accent);
        const auto axis=[&](bool horizontal){std::vector<int> ink;const auto count=horizontal?bounds.right:bounds.bottom;
            for(int at=0;at<count;++at){const auto x=horizontal?at:(content.left+content.right)/2,y=horizontal?(content.top+content.bottom)/2:at;
                if(x>=0&&x<bounds.right&&y>=0&&y<bounds.bottom&&(values[static_cast<std::size_t>(y)*bounds.right+x]&0xFFFFFF)==stroke)ink.push_back(at);}
            require(ink.size()>=2,"The actual slider rectangle lost one of its outline edges");std::size_t first=0,last=ink.size()-1;
            while(first+1<ink.size()&&ink[first+1]==ink[first]+1)++first;
            while(last>0&&ink[last-1]==ink[last]-1)--last;
            require(first<last,"The actual slider outline does not enclose a filled rectangle");
            // Compare the vector edges, not the interior brush footprint:
            // constant-width strokes do not scale with k. GDI Rectangle also
            // excludes its right/bottom coordinate (Microsoft Rectangle docs).
            return static_cast<LONG>(std::lround((ink[last]+ink.back()-ink.front()-ink[first])/2.0+1));};
        outline_size->cx=axis(true);outline_size->cy=axis(false);
    }
    SelectObject(buffer,previous);DeleteObject(bitmap);DeleteDC(buffer);require(content.right>content.left&&content.bottom>content.top,"The parameter preview has no actual filled rectangle");return content;
}
void slider_and_coordinate_draft_tests(HWND host){
    {
        const auto block=authored_puzzle("app/src/main/assets/content/es/math_physics/math_course_01/porcentajes_escalas_modelos/lesson_escalas_y_dimensiones.json",4,"slider");
        MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","");view.layout(0,0,650);
        const auto canvases=class_targets(view.window(),L"LearnGameAndEngineDev.MathCanvas"),sliders=class_targets(view.window(),TRACKBAR_CLASSW);
        require(canvases.size()==1&&sliders.size()==1,"The actual uniform scale activity lost its preview or parameter");
        RECT visual{},control{};GetWindowRect(canvases.front(),&visual);GetWindowRect(sliders.front(),&control);require(visual.bottom<=control.top,"The parameter preview is below its controls instead of the original Android order");
        SIZE initial_size{},scaled_size{};(void)rectangle_fill_bounds(canvases.front(),&initial_size);SendMessageW(sliders.front(),TBM_SETPOS,TRUE,2);SendMessageW(view.window(),WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(sliders.front()));
        (void)rectangle_fill_bounds(canvases.front(),&scaled_size);
        require(std::abs(scaled_size.cx-3*initial_size.cx)<=2&&std::abs(scaled_size.cy-3*initial_size.cy)<=2,
            "The authored scaleParameter did not multiply both original base dimensions against a fixed preview scale");
        require(caption_target(view.window(),L"Factor uniforme: 3"),"A parameter label did not retain Android's combined name and current value");
        const auto saved=view.answers_json();MathPuzzleView reopened(host,winrt::to_string(block.Stringify()),"es",saved);reopened.layout(0,0,650);
        require(reopened.answers_json()==saved,"Reopening a parameter activity changed its saved values");
    }
    {
        const auto block=authored_puzzle("app/src/main/assets/content/es/math_physics/math_course_01/racionales_y_decimales/lesson_multiplicar_fracciones.json",2,"dimensiones_racionales");
        MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","");view.layout(0,0,650);auto sliders=class_targets(view.window(),TRACKBAR_CLASSW);
        std::ranges::sort(sliders,[](HWND a,HWND b){RECT one{},two{};GetWindowRect(a,&one);GetWindowRect(b,&two);return one.top<two.top;});require(sliders.size()==2,"The real fractional dimension activity lost a parameter");
        for(const auto [at,position]:{std::pair{0,2},std::pair{1,4}}){SendMessageW(sliders[at],TBM_SETPOS,TRUE,position);SendMessageW(view.window(),WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(sliders[at]));}
        const auto box=rectangle_fill_bounds(class_targets(view.window(),L"LearnGameAndEngineDev.MathCanvas").front());
        require(std::abs(static_cast<double>(box.right-box.left)/(box.bottom-box.top)-1.5/2.5)<.04,"The authored fractional width/height preview distorted its actual dimensions");
    }
    {
        const auto block=simple_block(R"({"id":"line","type":"math_slider_interaction","parameters":[{"id":"m","label":"Pendiente","min":-2,"max":2,"step":1,"value":0},{"id":"b","label":"Ordenada","min":-2,"max":2,"step":1,"value":0}],"visual":{"type":"graph","slopeParameter":"m","interceptParameter":"b"}})");
        MathPuzzleView view(host,winrt::to_string(block.Stringify()),"es","");view.layout(0,0,650);const auto canvas=class_targets(view.window(),L"LearnGameAndEngineDev.MathCanvas").front();const auto original=canvas_pixels(canvas);
        auto sliders=class_targets(view.window(),TRACKBAR_CLASSW);std::ranges::sort(sliders,[](HWND a,HWND b){RECT one{},two{};GetWindowRect(a,&one);GetWindowRect(b,&two);return one.top<two.top;});
        SendMessageW(sliders.front(),TBM_SETPOS,TRUE,4);SendMessageW(view.window(),WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(sliders.front()));
        require(canvas_pixels(canvas)!=original,"A graph parameter changed its value but left the actual line preview unchanged");
    }
    {
        const auto block=authored_puzzle("app/src/main/assets/content/es/math_physics/math_course_01/arithmetic_fundamentals/lesson_math_puzzle_sandbox.json",10,"point_and_coordinate");
        const auto raw=winrt::to_string(block.Stringify());MathPuzzleView view(host,raw,"es","");view.layout(0,0,650);
        auto fields=edits(view.window());const auto position=[](HWND a,HWND b){RECT one{},two{};GetWindowRect(a,&one);GetWindowRect(b,&two);return one.top!=two.top?one.top<two.top:one.left<two.left;};std::ranges::sort(fields,position);
        require(fields.size()==3,"The real graph-and-coordinate activity lost an original answer field");replace(fields[0]," 2,0 ");replace(fields[1],"3");replace(fields[2],"2");
        lgaed::windows::NarrationWorker worker(std::filesystem::path(lgaed::build::source_root)/"out/narration/runtime.json");
        const auto evaluate=[&](const MathPuzzleView& source){return JsonObject::Parse(winrt::to_hstring(worker.validate_math(raw,source.answers_json(),"es")));};
        require(evaluate(view).GetNamedBoolean(L"correct"),"A complete original graph response rejected decimal-comma coordinate input");
        const auto valid=JsonObject::Parse(winrt::to_hstring(view.answers_json())).GetNamedObject(L"point").GetNamedArray(L"points").Stringify();replace(fields[0],"-");
        const auto invalid=view.answers_json();const auto saved=JsonObject::Parse(winrt::to_hstring(invalid)).GetNamedObject(L"point");
        require(saved.GetNamedArray(L"points").Stringify()==valid&&saved.GetNamedObject(L"interactionDraft").GetNamedArray(L"coords").GetObjectAt(0).GetNamedString(L"x")==L"-",
            "An incomplete coordinate erased its exact draft or the last valid plotted point");
        require(!evaluate(view).GetNamedBoolean(L"evaluable"),"An incomplete coordinate incorrectly accepted the last valid plotted answer");
        MathPuzzleView reopened(host,raw,"es",invalid);reopened.layout(0,0,650);auto restored=edits(reopened.window());std::ranges::sort(restored,position);
        require(edit_text(restored[0])=="-"&&reopened.answers_json()==invalid&&!evaluate(reopened).GetNamedBoolean(L"evaluable"),"Reopening lost a partial graph draft or made its old point evaluable");
        replace(restored[0],"2");require(evaluate(reopened).GetNamedBoolean(L"correct"),"Correcting a partial coordinate did not recover the original valid response");
    }
    std::cout<<"PASS authored slider preview order, fixed scale/base dimensions, fractional proportions, live graph parameters and exact graph drafts with real bridge rejection/recovery\n";
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
    const auto nearby=MAKELPARAM(bounds.right/2+MulDiv(24,static_cast<int>(GetDpiForWindow(canvas)),96),bounds.bottom/2);
    SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,nearby);require(GetCapture()==canvas,"The graph rejected Android's 32dp point-selection radius");SendMessageW(canvas,WM_LBUTTONUP,0,nearby);
    require(view.answers_json()==before,"A point-selection click moved a graph point without a drag");
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
void mathematical_labels_keep_their_content_locale(HWND host){
    const std::string prompt="The main idea uses std::cout.";
    const std::string label="The main idea calls std::cout.";
    const std::string formula="The main idea: std::cout and \\(main+1\\).";
    const auto format=[&](HWND target,const std::string& original,std::size_t byte){
        const auto unit=wide(std::string_view(original).substr(0,byte)).size();
        const auto mapping=native_math_text_mapping(target,original);
        const auto at=mapping?mapping->source_to_native.at(unit):unit;
        CHARRANGE before{},selected{static_cast<LONG>(at),static_cast<LONG>(at+1)};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
        SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));CHARFORMAT2W ink{};ink.cbSize=sizeof(ink);
        SendMessageW(target,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&ink));SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));return ink;
    };
    for(const auto locale:{"en","fr"}){
        auto raw=simple_block(R"({"id":"number","type":"math_numeric_input","label":"The main idea calls std::cout."})");
        raw.SetNamedValue(L"prompt",JsonValue::CreateStringValue(wide(prompt)));
        JsonObject representation;representation.SetNamedValue(L"type",JsonValue::CreateStringValue(L"text"));representation.SetNamedValue(L"text",JsonValue::CreateStringValue(wide(formula)));
        JsonArray representations;representations.Append(representation);raw.SetNamedValue(L"representations",representations);
        int changes=0;MathPuzzleCallbacks callbacks;callbacks.changed=[&]{++changes;};
        MathPuzzleView view(host,winrt::to_string(raw.Stringify()),locale,"",std::move(callbacks));view.layout(0,0,700);view.set_text_scale(125);
        const auto fields=edits(view.window());require(fields.size()==1,"The locale fixture did not create its original numeric answer");
        const auto input=fields.front();replace(input,"12.5");CHARRANGE answer_selection{1,3};SendMessageW(input,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&answer_selection));
        std::vector<std::pair<HWND,std::string>> labels;
        for(const auto& source:{prompt,label,formula}){HWND target=nullptr;for(const auto& region:view.reading_regions())if(region.source_text==source)target=region.target;
            require(target,"A locale-sensitive mathematical prompt/label lost its reading region");labels.emplace_back(target,source);}
        const auto inline_mapping=native_math_text_mapping(labels.back().first,formula);
        require(inline_mapping&&inline_mapping->built_up,"The locale fixture did not exercise its actual inline native formula");
        const auto check_labels=[&]{for(const auto& [target,source]:labels){
            const auto plain=format(target,source,0),main=format(target,source,source.find("main")),code=format(target,source,source.find("std::cout"));
            require((main.dwEffects&CFE_AUTOBACKCOLOR)&&main.crTextColor==plain.crTextColor&&std::wstring_view(main.szFaceName)==plain.szFaceName,
                "English/French ordinary main prose acquired a Spanish technical background, ink or font");
            require(((code.dwEffects&CFE_AUTOBACKCOLOR)!=0)==reader_theme::palette(target).high_contrast,
                "An English/French std::cout mention lost its actual technical background");
            if(source==formula)require(format(target,source,source.find("\\(")+2).dwEffects&CFE_AUTOBACKCOLOR,
                "A protected formula acquired the programming underlay");
        }};
        check_labels();const auto saved=view.answers_json(),identity=view.answer_identity_json();const auto original_changes=changes;
        std::vector<CHARFORMAT2W> before;for(const auto& [target,source]:labels){before.push_back(format(target,source,source.find("std::cout")));CHARRANGE selection{1,3};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));}
        int layouts=0;view.set_expression_request([&](std::string_view request){const auto query=JsonObject::Parse(wide(request));
            require(query.GetNamedString(L"action")==L"text_layout","Installing the locale fixture requested unrelated mathematics work");++layouts;return std::string(R"({"fragments":[]})");});
        require(layouts>=2,"The late mathematics callback did not reformat the actual prompt and label");check_labels();
        for(std::size_t index=0;index<labels.size();++index){const auto& [target,source]=labels[index];const auto after=format(target,source,source.find("std::cout"));
            CHARRANGE selection{};SendMessageW(target,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));int numerator=0,denominator=0;
            SendMessageW(target,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            require(after.crBackColor==before[index].crBackColor&&after.crTextColor==before[index].crTextColor&&
                (after.dwEffects&CFE_AUTOBACKCOLOR)==(before[index].dwEffects&CFE_AUTOBACKCOLOR)&&selection.cpMin==1&&selection.cpMax==3&&
                denominator>0&&numerator*100==denominator*125,"Refreshing expression support changed the localized background, ink, selection or text zoom");
        }
        CHARRANGE selected{};SendMessageW(input,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
        require(edits(view.window()).front()==input&&edit_text(input)=="12.5"&&selected.cpMin==answer_selection.cpMin&&selected.cpMax==answer_selection.cpMax&&
            view.answers_json()==saved&&view.answer_identity_json()==identity&&changes==original_changes,
            "Refreshing localized mathematical labels replaced an answer field or changed its original draft/caret/state");
    }
    std::cout<<"PASS English/French mathematical prose and std::cout backgrounds retain content locale, protected formula, native inputs, caret and state after late empty-fragment support\n";
}
}

int main(int argc,char** argv) {
    std::cout << std::unitbuf;
    try {
        lgaed::windows::ensure_winrt_apartment(); INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES}; InitCommonControlsEx(&common); Host host;
        require(host.window != nullptr,"Cannot create isolated QA host"); tom_probe(host.window);
        if(argc>1&&std::string_view(argv[1])=="--locale-technical"){mathematical_labels_keep_their_content_locale(host.window);return 0;}
        canvas_viewport_tests(host.window);
        structured_input_tests(host.window);
        android_control_parity_tests(host.window);
        mathematical_input_hint_tests(host.window);
        mathematical_input_ink_tests(host.window);
        calculator_field_tests(host.window);
        initial_text_scale_layout_tests(host.window);
        choice_alignment_tests(host.window);
        representation_parity_tests(host.window);
        rich_content_layout_tests(host.window);
        rich_option_content_tests(host.window);
        field_column_parity_tests(host.window);
        rigid_angle_copy_tests(host.window);
        automatic_mathematics_tests(host.window);
        mathematical_labels_keep_their_content_locale(host.window);
        slider_and_coordinate_draft_tests(host.window);
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
            RECT unit_area{},input_area{};GetWindowRect(region_target(view,"number/unit"),&unit_area);GetWindowRect(fields.front(),&input_area);
            require(unit_area.bottom<=input_area.top,"The numeric unit moved below its input instead of preserving Android's visible reading order");
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
            auto regions = view.reading_regions();const auto first=region_target(view,"ordered/item/a");
            require(first != nullptr,"Ordering has no original tappable card");
            const auto original_order = view.answers_json(); view.set_enabled(false);
            for (const auto& region : regions) if (region.key.find("/item/") != std::string::npos)
                require(IsWindowEnabled(region.target),"Freezing an order disabled its narratable card labels");
            click_card(first);require(view.answers_json() == original_order,"A frozen order still accepted a move");
            view.set_enabled(true);click_card(first);
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
