#include "presentation/windows/cpp_fill_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void replace(HWND item,std::string_view value){const auto text=lgaed::presentation::wide(value);SendMessageW(item,EM_SETSEL,0,-1);SendMessageW(item,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));}
}
int main(){
    using namespace lgaed;using namespace presentation;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Code holes QA",WS_OVERLAPPEDWINDOW,0,0,900,650,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
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
            check(hole_font.yHeight==360&&std::wstring_view(hole_font.szFaceName)==L"Consolas",
                "Code-hole text does not share the readable 18-point code font used for its measured template");
            replace(first,"<<");replace(second,"\"Carga lista\"s");
            check(changed>0&&curriculum::evaluate_hole_answers(block,view.answers()).correct,"Independent equivalent hole answers failed through native controls");
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
        }
        DestroyWindow(host);std::cout<<"PASS native C++ inline/drag controls, independent alternatives, Enter, exact drafts and card identities\n";return 0;
    }catch(const std::exception& e){if(IsWindow(host))DestroyWindow(host);std::cerr<<"FAIL C++ fill view: "<<e.what()<<'\n';return 1;}
}
