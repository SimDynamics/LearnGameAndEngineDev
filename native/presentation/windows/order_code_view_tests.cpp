#include "presentation/windows/order_code_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void click(HWND row){SendMessageW(row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));SendMessageW(row,WM_LBUTTONUP,0,MAKELPARAM(10,10));}
}
int main(){using namespace lgaed::presentation;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto host=CreateWindowExW(0,L"STATIC",L"Code order QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,800,640,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try{
        const std::vector<std::string> source{"std::cout << \"Acceso autorizado\";","#include <iostream>","return 0;\n}","int main() {"};
        int changes=0;OrderCodeView view(host,source,{0,1,2,3},[&]{++changes;});
        MoveWindow(view.window(),10,10,700,view.height_for_width(700),TRUE);
        check(GetDlgItem(view.window(),100)&&GetDlgItem(view.window(),103),"Missing original fragment rows");
        click(GetDlgItem(view.window(),101));click(GetDlgItem(view.window(),103));
        check(view.order()==std::vector<std::size_t>({1,3,0,2}),"Click ordering differs from Android nextTapPosition");
        const auto regions=view.reading_regions();check(regions[0].second==source[1]&&regions[1].second==source[3],"Narration did not follow visible row order");
        CHARRANGE unselected{};const auto first=GetDlgItem(view.window(),101);
        SendMessageW(first,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&unselected));
        SendMessageW(first,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(12,8));
        CHARRANGE after_double{};SendMessageW(first,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_double));
        check(after_double.cpMin==after_double.cpMax,"Double click selected fragment text instead of retaining row interaction");
        SendMessageW(GetDlgItem(view.window(),102),WM_SYSKEYDOWN,VK_UP,0);
        check(view.order()==std::vector<std::size_t>({1,3,2,0}),"Accessible reorder lost stable source identity");
        view.reset();const auto origin=GetDlgItem(view.window(),100),target=GetDlgItem(view.window(),103);
        RECT a{},b{};GetWindowRect(origin,&a);GetWindowRect(target,&b);
        SendMessageW(origin,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));Sleep(25);
        SendMessageW(origin,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(12,b.top-a.top+8));
        SendMessageW(origin,WM_LBUTTONUP,0,MAKELPARAM(12,b.top-a.top+8));
        check(view.order()==std::vector<std::size_t>({1,2,3,0}),"Actual drag did not move the stable fragment to its target");
        const auto before=view.order();SendMessageW(origin,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);
        check(view.order()==before,"Mouse wheel started a reorder");
        const auto wheel_row=GetDlgItem(view.window(),103);
        SendMessageW(wheel_row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));
        SendMessageW(wheel_row,WM_MOUSEWHEEL,MAKEWPARAM(MK_LBUTTON,-WHEEL_DELTA),0);
        SendMessageW(wheel_row,WM_LBUTTONUP,0,MAKELPARAM(12,8));
        check(view.order()==before,"A wheel inside a pending mouse press became a click-to-order");
        view.set_enabled(false);click(wheel_row);check(view.order()==before&&IsWindowEnabled(wheel_row),"Solved ordering changed or dimmed its original syntax text");
        view.set_enabled(true);view.set_wrap(true);const auto narrow=view.height_for_width(180);view.set_wrap(false);
        check(narrow>view.height_for_width(180),"Line wrapping did not use the actual resized text geometry");
        view.set_text_scale(150);check(view.order()==before&&changes>=4,"Presentation changed authored fragment order");
        check(view.reading_regions()[0].second==source[before[0]],"Source text changed during wrapping/zoom");
        view.reset();for(const auto i:{3,2,1,0})click(GetDlgItem(view.window(),100+i));
        const auto incorrect=view.order();view.restart_tap_sequence();
        check(view.order()==incorrect,"Retry reset destroyed the student's existing arrangement");
        click(GetDlgItem(view.window(),101));check(view.order().front()==1,"An incorrect complete click sequence could not be retried without erasing the draft");
        std::cout<<"PASS code ordering: original rows, click order, real drag, wheel exclusion, keyboard, reading order, wrap, scale, solved state\n";
        DestroyWindow(host);return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL code ordering: "<<error.what()<<'\n';DestroyWindow(host);return 1;}
}
