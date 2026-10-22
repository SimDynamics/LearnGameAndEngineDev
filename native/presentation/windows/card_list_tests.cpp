#include "presentation/windows/card_list.hpp"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr UINT activated=WM_APP+601;
constexpr int list_id=4105;
struct Result { unsigned launches=0,changed=0,draws=0,measured=0;bool wrong_draw=false;HWND list=nullptr; };
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
std::vector<HWND> buttons(HWND parent){std::vector<HWND> result;for(auto child=GetWindow(parent,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))result.push_back(child);return result;}
int offset(HWND window){SCROLLINFO info{sizeof(info),SIF_POS};GetScrollInfo(window,SB_VERT,&info);return info.nPos;}
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* result=reinterpret_cast<Result*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){result=static_cast<Result*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(result));}
    if(result){
        if(message==activated){++result->launches;return 0;}
        if(message==WM_COMMAND&&LOWORD(wp)==list_id&&HIWORD(wp)==LBN_SELCHANGE){++result->changed;return 0;}
        if(message==WM_MEASUREITEM){auto& measure=*reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if(measure.CtlID==list_id){++result->measured;measure.itemHeight=measure.itemID==1?800:500;return TRUE;}
        }
        if(message==WM_DRAWITEM){const auto& draw=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if(draw.CtlID==list_id){++result->draws;
                if(draw.CtlType!=ODT_LISTBOX||draw.hwndItem!=result->list||draw.itemID>=8||draw.rcItem.left!=0||draw.rcItem.top!=0)result->wrong_draw=true;
                FillRect(draw.hDC,&draw.rcItem,GetSysColorBrush(COLOR_WINDOW));return TRUE;}
        }
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
int main(){
    HWND host=nullptr;
    try{
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW type{};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=host_proc;type.lpszClassName=L"LgaedCardListTests";
        check(RegisterClassW(&type)!=0,"Cannot register card QA host");Result result;
        host=CreateWindowExW(0,type.lpszClassName,L"Native card list QA",WS_OVERLAPPEDWINDOW,0,0,1000,850,nullptr,nullptr,type.hInstance,&result);
        check(host!=nullptr,"Cannot create card QA host");
        const auto list=lgaed::presentation::create_card_list(host,list_id,activated);result.list=list;
        MoveWindow(list,15,15,750,600,TRUE);
        for(unsigned i=0;i<8;++i){const auto text=L"Curso "+std::to_wstring(i+1)+L" · título íntegro";check(SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()))==i,"Adding a native card changed index/order");}
        check(result.measured==8&&SendMessageW(list,LB_GETITEMHEIGHT,1,0)==800,"Cards were not measured before top-index restoration");
        const auto font=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));SendMessageW(list,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        for(unsigned i=0;i<8;++i)check(SendMessageW(list,LB_SETITEMHEIGHT,i,i==1?800:500)==0,"A card taller than 255 pixels was rejected");
        ShowWindow(host,SW_SHOWNOACTIVATE);RedrawWindow(list,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);pump();
        check(SendMessageW(list,LB_GETCOUNT,0,0)==8&&SendMessageW(list,LB_GETITEMHEIGHT,0,0)==500&&SendMessageW(list,LB_GETITEMHEIGHT,1,0)==800,"Large native row heights were truncated");
        RECT first{},second{};SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first));SendMessageW(list,LB_GETITEMRECT,1,reinterpret_cast<LPARAM>(&second));
        check(first.bottom-first.top==500&&second.bottom-second.top==800&&second.top==first.bottom,"Card geometry does not match actual pixel heights");
        const auto cards=buttons(list);check(cards.size()==8,"Cards are not real native child buttons");
        for(const auto card:cards){wchar_t name[32]{},caption[100]{};GetClassNameW(card,name,32);GetWindowTextW(card,caption,100);
            check(_wcsicmp(name,L"BUTTON")==0&&std::wstring(caption).find(L"título íntegro")!=std::wstring::npos,"A card lost its accessible native caption");
            check(SendMessageW(card,WM_GETFONT,0,0)==reinterpret_cast<LRESULT>(font),"Font was not propagated to native cards");
            RECT actual{};GetClientRect(card,&actual);
            check(actual.bottom==(std::wstring(caption).starts_with(L"Curso 2 ")?800:500),"Native BUTTON geometry was clipped to the list-box height limit");
        }
        check(result.draws>0&&!result.wrong_draw,"Native owner drawing lost its catalog index or local coordinate system");
        SendMessageW(list,LB_SETCURSEL,3,0);pump();check(result.launches==0&&SendMessageW(list,LB_GETCURSEL,0,0)==3,"Programmatic selection activated a card");
        SendMessageW(list,LB_SETTOPINDEX,0,0);
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,35));SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(40,35));pump();
        check(result.launches==1&&SendMessageW(list,LB_GETCURSEL,0,0)==0,"Clicking the card did not activate exactly its visible item");
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(50,300));SendMessageW(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(50,100));SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(50,100));pump();
        check(offset(list)==200&&result.launches==1,"Dragging did not scroll in pixels or accidentally activated content");
        const auto before=offset(list);SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);pump();
        check(offset(list)>before&&result.launches==1,"Mouse wheel activated content or failed to scroll");
        SendMessageW(list,LB_SETTOPINDEX,1,0);check(SendMessageW(list,LB_GETTOPINDEX,0,0)==1&&offset(list)==500,"Top-index persistence lost its actual item boundary");
        SendMessageW(list,LB_SETITEMHEIGHT,0,800);SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first));
        check(first.bottom-first.top==800&&SendMessageW(list,LB_GETTOPINDEX,0,0)==1&&offset(list)==800,
            "Remeasuring earlier cards changed the restored top item or clipped its height");
        SendMessageW(list,LB_SETTOPINDEX,0,0);SendMessageW(list,WM_KEYDOWN,VK_NEXT,1);pump();
        check(offset(list)>0&&result.launches==1,"PageDown must scroll within a card taller than the viewport");
        SendMessageW(list,WM_KEYDOWN,VK_END,1);pump();check(SendMessageW(list,LB_GETCURSEL,0,0)==7&&result.launches==1,"End did not select the last card without activation");
        SendMessageW(list,WM_KEYDOWN,VK_HOME,1);SendMessageW(list,WM_KEYDOWN,VK_DOWN,1);pump();
        check(SendMessageW(list,LB_GETCURSEL,0,0)==1&&result.launches==1,"Keyboard arrows activated instead of selecting a card");
        SendMessageW(list,WM_KEYDOWN,VK_RETURN,1);pump();check(result.launches==2,"Enter did not activate exactly once");
        const auto selected=SendMessageW(list,LB_GETCURSEL,0,0);SendMessageW(list,LB_GETITEMRECT,static_cast<WPARAM>(selected),reinterpret_cast<LPARAM>(&second));
        const auto hit=SendMessageW(list,LB_ITEMFROMPOINT,0,MAKELPARAM(30,std::max(0L,second.top)+10));
        check(!HIWORD(hit)&&LOWORD(hit)==selected,"Visible hit testing lost original card ownership");
        SendMessageW(cards.front(),BM_CLICK,0,0);pump();check(result.launches==3,"Native accessibility button activation did not reach the catalog");
        SendMessageW(list,LB_RESETCONTENT,0,0);pump();check(SendMessageW(list,LB_GETCOUNT,0,0)==0&&SendMessageW(list,LB_GETCURSEL,0,0)==LB_ERR&&buttons(list).empty(),"Reset retained stale native controls or selection");
        DestroyWindow(host);host=nullptr;std::cout<<"PASS native BUTTON cards: 500/800px height, accessibility, pixel scrolling, selection, wheel/drag and keyboard\n";return 0;
    }catch(const std::exception& error){if(host)DestroyWindow(host);std::cerr<<"FAIL native card list: "<<error.what()<<'\n';return 1;}
}
