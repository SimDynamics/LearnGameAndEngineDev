#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
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
            SendMessageW(GetDlgItem(view.window(),2002),BM_CLICK,0,0);
            SendMessageW(GetDlgItem(view.window(),102),WM_LBUTTONUP,0,0);
            check(curriculum::evaluate_drag_answers(block,view.slots()).correct,"Click-to-place did not share the real drag slots");
            CppFillView reopened(host,block,view.response_json(),[]{},[]{});
            check(reopened.slots()==view.slots(),"Drag slot identities changed after reopening");
            check(view.height_for_width(180)>0,"Narrow code card layout failed");
            MoveWindow(view.window(),0,0,180,500,FALSE);const auto unwrapped=view.height_for_width(180);const auto saved=view.response_json();
            view.set_wrap(true);check(view.height_for_width(180)>unwrapped&&view.response_json()==saved,
                "Drag-fill code did not reuse the same wrapping layout without changing card identities");
            view.set_wrap(false);check(view.height_for_width(180)==unwrapped,"Drag-fill unwrapping did not restore its authored source rows");
        }
        DestroyWindow(host);std::cout<<"PASS native C++ inline/drag controls, real Tab/Shift+Tab blank navigation, wrap/number margin, independent alternatives, Enter, exact drafts and card identities\n";return 0;
    }catch(const std::exception& e){if(IsWindow(host))DestroyWindow(host);std::cerr<<"FAIL C++ fill view: "<<e.what()<<'\n';return 1;}
}
