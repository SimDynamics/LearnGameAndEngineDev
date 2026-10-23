#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <array>
#include <algorithm>
#include <limits>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void replace(HWND item,std::string_view value){const auto text=lgaed::presentation::wide(value);SendMessageW(item,EM_SETSEL,0,-1);SendMessageW(item,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));}
void physical_tab(HWND host,HWND field,bool reverse=false){
    std::array<BYTE,256> previous{},keys{};check(GetKeyboardState(previous.data()),"Cannot read this test thread's keyboard state");keys=previous;
    keys[VK_SHIFT]=reverse?BYTE{0x80}:BYTE{0};check(SetKeyboardState(keys.data()),"Cannot provide the Shift modifier to this test thread");
    MSG key{field,WM_KEYDOWN,VK_TAB,1};
    if(!IsDialogMessageW(host,&key)){TranslateMessage(&key);DispatchMessageW(&key);}
    SendMessageW(field,WM_CHAR,L'\t',1);SendMessageW(field,WM_KEYUP,VK_TAB,LPARAM{1}<<31);
    SetKeyboardState(previous.data());
    MSG queued{};while(PeekMessageW(&queued,nullptr,0,0,PM_REMOVE))if(queued.message!=WM_QUIT){TranslateMessage(&queued);DispatchMessageW(&queued);}
}
RECT bounds(HWND item,HWND parent=nullptr){RECT result{};GetWindowRect(item,&result);if(parent)MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&result),2);return result;}
POINT center(HWND item){const auto rect=bounds(item);return {(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};}
void drag_to(HWND source,POINT destination,bool wheel=false){
    RECT area{};GetClientRect(source,&area);const auto started=GetTickCount64();SendMessageW(source,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(area.right/2,area.bottom/2));
    const auto captured=GetCapture();check(captured!=nullptr,"A real code-token press did not capture its drag target");
    while(GetTickCount64()-started<20)Sleep(1);
    auto point=destination;ScreenToClient(captured,&point);
    if(wheel)SendMessageW(captured,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(destination.x,destination.y));
    SendMessageW(captured,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x,point.y));
    SendMessageW(captured,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
    check(GetCapture()==nullptr,"Finishing or rejecting a code-token gesture retained capture");
}
COLORREF ink(HWND edit,LONG start,LONG end){CHARRANGE before{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
    SendMessageW(edit,EM_SETSEL,start,end);CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&before));check(!(format.dwEffects&CFE_AUTOCOLOR),"A code token reverted to automatic system ink");return format.crTextColor;}
std::vector<COLORREF> raster(HWND item){RECT area{};GetClientRect(item,&area);const auto screen=GetDC(item),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,std::max(1L,area.right),std::max(1L,area.bottom));ReleaseDC(item,screen);
    check(dc&&bitmap,"Cannot create code-token rendering surface");const auto before=SelectObject(dc,bitmap);FillRect(dc,&area,reader_theme::brush(reader_theme::Surface::card));
    SendMessageW(item,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN);GdiFlush();std::vector<COLORREF> pixels;
    for(int y=0;y<area.bottom;++y)for(int x=0;x<area.right;++x)pixels.push_back(GetPixel(dc,x,y));SelectObject(dc,before);DeleteObject(bitmap);DeleteDC(dc);return pixels;}
void fixed_template_parity(HWND host){
    using namespace lgaed;curriculum::Block model;model.type=curriculum::BlockType::inline_fill_code;
    model.code_template="    std::cout << [];";model.answers={"1"};
    CppFillView view(host,model,"",[]{},[]{});const auto source=GetDlgItem(view.window(),1000),field=GetDlgItem(view.window(),100),suffix=GetDlgItem(view.window(),1002);
    check(source&&field&&suffix,"The real inline template lost a fixed segment or its authored field");replace(field,"1");SendMessageW(field,EM_SETSEL,1,1);
    SendMessageW(source,EM_SETSEL,2,5);const auto before=view.editor(field)->document();const auto saved=view.response_json();
    for(const auto scale:{100,75,150,100}){view.set_text_scale(scale);MoveWindow(view.window(),0,0,850,view.height_for_width(850),TRUE);
        POINTL first{},end{};SendMessageW(source,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first),0);
        SendMessageW(source,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),static_cast<LPARAM>(wide("    std::cout << ").size()));
        const auto rect=bounds(source),answer=bounds(field);check(rect.right-rect.left==end.x-first.x&&answer.left==rect.right,
            "An inline fixed segment reserves raw uncompressed spaces or extra pixels beyond its real compacted glyph advance");
        if(!reader_theme::palette().high_contrast)check(ink(source,0,1)==RGB(225,230,235)&&ink(suffix,0,1)==RGB(225,230,235),
            "Fixed template whitespace/punctuation inherited drag-token ink instead of Android's original source ink");
        CHARRANGE selected{};SendMessageW(source,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));const auto document=view.editor(field)->document();
        check(selected.cpMin==2&&selected.cpMax==5&&view.response_json()==saved&&document.text==before.text&&
            document.selection_start==before.selection_start&&document.selection_end==before.selection_end&&GetDlgItem(view.window(),100)==field,
            "Measuring a compact fixed segment changed its selection, the real inline editor/caret or answer draft");
    }
    const auto original=edit_text(source);SendMessageW(source,WM_CHAR,L'x',1);check((GetWindowLongPtrW(source,GWL_STYLE)&ES_READONLY)&&edit_text(source)==original,
        "The source-width correction changed the readonly template into editable answer data");
}
void drag_parity(HWND host){
    using namespace lgaed;using namespace presentation;
    curriculum::Block model;model.type=curriculum::BlockType::drag_fill_code;model.code_template="[] [] []";
    model.choices={"int","std::cout << \"Listo\";","return 0;",";"};int changes=0;
    CppFillView view(host,model,"",[&]{++changes;},[]{});const int width=720;MoveWindow(view.window(),0,0,width,view.height_for_width(width),TRUE);
    const auto bank=view.palette_window();check(bank&&GetParent(bank)==view.window(),"The original code-token bank was not retained inside the normal card");
    std::array<HWND,4> cards{},labels{};for(int i=0;i<4;++i){cards[i]=GetDlgItem(bank,2000+i);labels[i]=GetDlgItem(cards[i],4000+i);check(cards[i]&&labels[i],"A choice lost its stable native button or syntax-bearing child");
        check(edit_text(labels[i])==model.choices[static_cast<std::size_t>(i)]&&(GetWindowLongPtrW(labels[i],GWL_STYLE)&ES_READONLY),"A code token changed source characters or became an editable label");
        CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(labels[i],EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&format));
        check(format.yHeight==270&&std::wstring_view(format.szFaceName)==reader_theme::code_font_face(),"A first-open drag token lost the original18-sp monospace font");
        check(IsWindowVisible(cards[i]),"An unused physical token was missing from the bank");
        if(i)check(bounds(cards[i]).top>=bounds(cards[i-1]).bottom,"The normal Android token bank was changed into a horizontal/wrapping grid");}
    if(!reader_theme::palette().high_contrast){
        check(ink(labels[0],0,3)==RGB(171,141,227)&&ink(labels[1],0,3)==RGB(171,141,227)&&ink(labels[2],0,6)==RGB(231,155,186)&&ink(labels[2],7,8)==RGB(224,151,109),"Drag tokens lost first-open C++ keyword/identifier/literal syntax colors");
        const auto pixels=raster(cards[0]);const auto rect=bounds(cards[0]);check(std::ranges::count(pixels,reader_theme::palette(cards[0]).accent)>2&&std::ranges::count(pixels,RGB(25,25,25))>20,"The real token card omitted its route-accent border or Android low surface");
        (void)rect;
    }
    if(!reader_theme::palette().high_contrast)check(ink(GetDlgItem(view.window(),1001),0,1)==RGB(225,230,235),"A fixed drag-template separator inherited the token-only base ink");
    const auto hole0=GetDlgItem(view.window(),100),hole1=GetDlgItem(view.window(),101),hole2=GetDlgItem(view.window(),102);
    const auto dpi=static_cast<int>(GetDpiForWindow(view.window()));const auto empty=bounds(hole0);check(empty.right-empty.left==MulDiv(18,dpi,96)&&empty.bottom-empty.top==MulDiv(22,dpi,96)&&edit_text(hole0).empty(),"An empty drag slot lost its authored18x22 size or leaked a placeholder into code");
    const auto unchanged=view.response_json();drag_to(labels[0],center(hole0),true);check(view.response_json()==unchanged&&changes==0,"Mouse-wheel scrolling started, completed or fell back to a code-token placement");
    drag_to(labels[0],center(hole0));check(view.slots()[0]==0&&!IsWindowVisible(cards[0])&&changes==1&&edit_text(hole0)=="int","Dragging the real colored bank label did not place exactly one token in its slot");
    SendMessageW(cards[1],BM_CLICK,0,0);check(view.slots()[1]==1&&!IsWindowVisible(cards[1]),"A bank click failed to fill the first empty source slot without duplication");
    drag_to(hole0,center(hole1));check(view.slots()[0]==1&&view.slots()[1]==0&&edit_text(hole0)==model.choices[1]&&edit_text(hole1)==model.choices[0],"Dragging an occupied slot failed to swap its physical token with the destination");
    drag_to(labels[2],center(hole0));check(view.slots()[0]==2&&IsWindowVisible(cards[1])&&!IsWindowVisible(cards[2]),"A bank drop onto an occupied slot did not return the displaced token to the bank");
    drag_to(hole0,center(bank));check(view.slots()[0]==std::numeric_limits<std::size_t>::max()&&IsWindowVisible(cards[2]),"Returning an occupied token to its bank lost its identity or left the old slot filled");
    SendMessageW(cards[3],BM_CLICK,0,0);check(view.slots()[0]==3,"A bank tap did not use the first empty slot in source order");
    SendMessageW(hole0,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));SendMessageW(hole0,WM_LBUTTONUP,0,MAKELPARAM(2,2));
    check(view.slots()[0]==std::numeric_limits<std::size_t>::max()&&IsWindowVisible(cards[3]),"Tapping a filled token did not return the same choice to the bank");
    const auto saved=view.response_json();const auto source=edit_text(labels[1]);SendMessageW(labels[1],EM_SETSEL,2,5);SendMessageW(labels[1],WM_CHAR,L'x',1);check(edit_text(labels[1])==source,"A display-only code token accepted text editing");
    view.set_text_scale(150);(void)view.height_for_width(width);int numerator=0,denominator=0;SendMessageW(labels[1],EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
    CHARRANGE selection{};SendMessageW(labels[1],EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));check(denominator>0&&numerator*100==150*denominator&&selection.cpMin==2&&selection.cpMax==5&&view.response_json()==saved&&edit_text(labels[1])==source,"Scaling the shared drag bank lost original source, syntax-view selection, identities or150% zoom");
    const auto normal_height=view.height_for_width(width);view.set_palette_host(host);const auto tray_height=view.palette_height_for_width(220);MoveWindow(bank,0,400,220,tray_height,TRUE);
    check(GetParent(bank)==host&&view.palette_window()==bank&&GetDlgItem(bank,2001)==cards[1]&&view.height_for_width(width)<normal_height&&view.response_json()==saved,"Fullscreen created a token-bank copy, kept it in the scrolling body or changed its draft");
    SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE};GetScrollInfo(bank,SB_HORZ,&range);check(range.nMax-range.nMin+1>static_cast<int>(range.nPage),"The single-band fullscreen bank lost actual horizontal overflow");
    check(bounds(cards[1]).top==bounds(cards[2]).top&&bounds(cards[2]).top==bounds(cards[3]).top,"Fullscreen tokens wrapped instead of retaining the original horizontal tray");
    view.set_palette_host();check(GetParent(bank)==view.window()&&view.palette_window()==bank&&view.height_for_width(width)==normal_height&&view.response_json()==saved,"Leaving fullscreen failed to restore the same vertical token bank and draft");
    const auto before_disabled=raster(hole1);view.set_enabled(false);check(!IsWindowEnabled(hole1),"A solved drag token remained enabled");
    check((reader_theme::palette().high_contrast||raster(hole1)==before_disabled)&&view.response_json()==saved,"Disabling a solved token replaced its native dark syntax presentation or changed code");
    view.set_enabled(true);view.reset();check(std::ranges::all_of(view.slots(),[](auto value){return value==std::numeric_limits<std::size_t>::max();})&&std::ranges::all_of(cards,[](auto card){return IsWindowVisible(card)!=FALSE;}),"Reset did not return all original physical choices to their bank");
    (void)hole2;
}

}
int main(){
    using namespace lgaed;using namespace presentation;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Code holes QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,650,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try{
        check(host!=nullptr,"Cannot create QA host");
        curriculum::Block block;block.type=curriculum::BlockType::inline_fill_code;
        block.code_template="#include <iostream>\nint main() {\n    std::cout [] [];\n}\n";
        block.answers={"<<","\"Carga lista\""};block.answer_alternatives={{"<<"},{"\"Carga lista\"","\"Carga lista\"s"}};
        int changed=0,submitted=0;
        {
            CppFillView view(host,block,"",[&]{++changed;},[&]{++submitted;});
            check(view.answers()==std::vector<std::string>({"",""}),"Hidden expected answers appeared in the fields");
            auto first=GetDlgItem(view.window(),100),second=GetDlgItem(view.window(),101);
            check(first&&second,"Authored holes did not become independent fields");
            CHARFORMAT2W hole_font{};hole_font.cbSize=sizeof(hole_font);
            SendMessageW(first,EM_GETCHARFORMAT,SCF_DEFAULT,reinterpret_cast<LPARAM>(&hole_font));
            check(hole_font.yHeight==270&&std::wstring_view(hole_font.szFaceName)==reader_theme::code_font_face(),
                "Code-hole text does not share the Android 18-sp code font used for its measured template");
            replace(first,"<<");replace(second,"\"Carga lista\"s");
            check(changed>0&&curriculum::evaluate_hole_answers(block,view.answers()).correct,"Independent equivalent hole answers failed through native controls");
            MoveWindow(view.window(),0,0,350,500,FALSE);(void)view.height_for_width(350);
            SendMessageW(first,EM_SETSEL,1,1);SendMessageW(second,EM_SETSEL,2,2);SetFocus(first);
            const auto before_tab=view.response_json();const auto before_changes=changed;
            physical_tab(host,first);check(GetFocus()==second&&view.response_json()==before_tab&&changed==before_changes,
                "Physical Tab did not move to the next real blank without inserting indentation or changing answers");
            CHARRANGE second_cursor{};SendMessageW(second,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&second_cursor));
            check(second_cursor.cpMin==2&&second_cursor.cpMax==2,"Tab navigation lost the destination blank's existing cursor");
            physical_tab(host,second);check(GetFocus()==first,"Tab did not cycle through the actual inline blanks");
            physical_tab(host,first,true);check(GetFocus()==second&&view.response_json()==before_tab,
                "Shift+Tab did not navigate backwards without modifying either blank");
            SendMessageW(first,WM_KEYDOWN,VK_RETURN,1);SendMessageW(first,WM_KEYUP,VK_RETURN,1);
            check(submitted==1&&view.answers()[0]=="<<","Single-line Enter did not submit exactly once");
            const auto saved=view.response_json();
            const auto document=view.editor(first)->document();const auto height=view.height_for_width(560);
            view.set_text_scale(150);int numerator=0,denominator=0;SendMessageW(first,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
            check(denominator>0&&numerator*100==150*denominator&&view.height_for_width(560)>height,"Code-hole text or its measured line heights did not scale");
            const auto scaled_document=view.editor(first)->document();
            check(view.response_json()==saved&&scaled_document.text==document.text&&scaled_document.selection_start==document.selection_start&&scaled_document.selection_end==document.selection_end,
                "Scaling code holes changed original answers, selection or field identities");
            view.set_text_scale(75);view.set_text_scale(100);
            CppFillView reopened(host,block,saved,[]{},[]{});
            check(reopened.answers()==view.answers(),"Hole drafts did not survive serialization");
            check(view.height_for_width(350)>0&&view.height_for_width(850)>0,"Code holes could not resize");
            MoveWindow(view.window(),0,0,180,500,FALSE);const auto unwrapped=view.height_for_width(180);
            SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE};GetScrollInfo(view.window(),SB_HORZ,&scroll);
            check(scroll.nPage>0&&scroll.nMax-scroll.nMin+1>static_cast<int>(scroll.nPage),
                "Unwrapped inline source did not expose its actual horizontal overflow");
            const auto wrap_document=view.editor(first)->document();const auto wrap_saved=view.response_json();
            view.set_wrap(true);const auto wrapped=view.height_for_width(180);scroll={sizeof(scroll),SIF_RANGE|SIF_PAGE};GetScrollInfo(view.window(),SB_HORZ,&scroll);
            check(wrapped>unwrapped&&scroll.nMax-scroll.nMin+1<=static_cast<int>(scroll.nPage)&&
                GetDlgItem(view.window(),100)==first&&GetDlgItem(view.window(),101)==second&&view.response_json()==wrap_saved,
                "Inline wrap did not reflow real source/holes, clear horizontal overflow or preserve existing fields and answers");
            const auto wrapped_document=view.editor(first)->document();
            check(wrapped_document.text==wrap_document.text&&wrapped_document.selection_start==wrap_document.selection_start&&
                wrapped_document.selection_end==wrap_document.selection_end,"Wrapping inline code changed its cursor or editing document");
            const auto plain=GetDlgItem(view.window(),1000);RECT plain_bounds{};GetWindowRect(plain,&plain_bounds);MapWindowPoints(nullptr,view.window(),reinterpret_cast<POINT*>(&plain_bounds),2);
            check(plain_bounds.left>10,"Code holes still omit their fixed logical-line number margin");
            const auto screen=GetDC(view.window()),memory=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,180,wrapped);ReleaseDC(view.window(),screen);
            check(memory&&bitmap,"Cannot create the real code-hole line-number drawing surface");const auto previous=SelectObject(memory,bitmap);
            RECT raster{0,0,180,wrapped};FillRect(memory,&raster,reader_theme::brush(reader_theme::Surface::card));
            SendMessageW(view.window(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            unsigned number_ink=0;const auto background=reader_theme::background(view.window());
            for(int y=0;y<std::min(wrapped,48);++y)for(int x=0;x<plain_bounds.left-10;++x)if(GetPixel(memory,x,y)!=background)++number_ink;
            SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);
            check(number_ink>5,"The logical source line number was not actually drawn in the code-hole margin");
            view.set_wrap(false);check(view.height_for_width(180)==unwrapped&&view.response_json()==wrap_saved,
                "Disabling inline wrap did not restore source-line geometry and unchanged answers");
            view.set_enabled(false);check(!IsWindowEnabled(first),"Solved holes remained editable");
            view.reset();check(view.answers()==std::vector<std::string>({"",""}),"Explicit reset retained an old answer");
        }
        block.type=curriculum::BlockType::drag_fill_code;block.code_template="int []() {\n    std::cout [] \"Listo\"[]\n}";
        block.choices={"main","<<",";",";"};block.correct_order={0,1,2};
        {
            CppFillView view(host,block,R"({"slots":[0,1,3],"unrelated":"preserved by owner"})",[]{},[]{});
            check(curriculum::evaluate_drag_answers(block,view.slots()).correct,"Duplicate-text cards were confused with different answers");
            check(view.editor(GetDlgItem(view.window(),100))==nullptr,"A drag slot became a free text editor");
            SendMessageW(GetDlgItem(view.window(),102),WM_KEYDOWN,VK_BACK,1);
            check(!curriculum::evaluate_drag_answers(block,view.slots()).correct,"Removing a card retained the old correct answer");
            SendMessageW(GetDlgItem(view.palette_window(),2002),BM_CLICK,0,0);
            check(curriculum::evaluate_drag_answers(block,view.slots()).correct,"A bank tap did not fill the first actual empty slot");
            CppFillView reopened(host,block,view.response_json(),[]{},[]{});
            check(reopened.slots()==view.slots(),"Drag slot identities changed after reopening");
            check(view.height_for_width(180)>0,"Narrow code card layout failed");
            MoveWindow(view.window(),0,0,180,500,FALSE);const auto unwrapped=view.height_for_width(180);const auto saved=view.response_json();
            view.set_wrap(true);check(view.height_for_width(180)>unwrapped&&view.response_json()==saved,
                "Drag-fill code did not reuse the same wrapping layout without changing card identities");
            view.set_wrap(false);check(view.height_for_width(180)==unwrapped,"Drag-fill unwrapping did not restore its authored source rows");
        }
        fixed_template_parity(host);
        drag_parity(host);
        DestroyWindow(host);std::cout<<"PASS native C++ inline/drag controls, real Tab/Shift+Tab blank navigation, wrap/number margin, independent alternatives, Enter, exact drafts and card identities\n";return 0;
    }catch(const std::exception& e){if(IsWindow(host))DestroyWindow(host);std::cerr<<"FAIL C++ fill view: "<<e.what()<<'\n';return 1;}
}
