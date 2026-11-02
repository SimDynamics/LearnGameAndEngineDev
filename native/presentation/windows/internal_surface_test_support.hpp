#pragma once
#include "presentation/windows/internal_window.hpp"
#include <windows.h>
#include <stdexcept>
#include <string_view>

namespace lgaed::presentation::internal_surface_test {
inline void require_internal_surface(HWND root,HWND surface,bool full_page=true){
    const auto style=GetWindowLongPtrW(surface,GWL_STYLE);
    if(!IsWindow(surface)||GetParent(surface)!=root||GetAncestor(surface,GA_ROOT)!=root
        ||!(style&WS_CHILD)||(style&WS_POPUP)||!IsWindowEnabled(root))
        throw std::runtime_error("Custom app surface escaped its enabled main root");
    RECT client{},bounds{};GetClientRect(root,&client);GetWindowRect(surface,&bounds);
    MapWindowPoints(nullptr,root,reinterpret_cast<POINT*>(&bounds),2);
    if(bounds.left<0||bounds.top<0||bounds.right>client.right||bounds.bottom>client.bottom)
        throw std::runtime_error("Custom app surface extends outside main client bounds");
    if(full_page){
        if(!EqualRect(&client,&bounds)||active_internal_window(root)!=surface)
            throw std::runtime_error("Internal app page does not fill or own the main root");
    }else if(active_internal_window(root)==surface)
        throw std::runtime_error("Anchored mathematical keyboard became a fullscreen app page");
    struct Visible{HWND root;int count=0;} visible{root};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND item,LPARAM context)->BOOL{
        auto& value=*reinterpret_cast<Visible*>(context);wchar_t name[128]{};GetClassNameW(item,name,128);
        if(IsWindowVisible(item)&&(item==value.root||std::wstring_view(name).starts_with(L"LearnGameAndEngineDev.")))++value.count;
        return TRUE;
    },reinterpret_cast<LPARAM>(&visible));
    if(IsWindowVisible(root)&&visible.count!=1)
        throw std::runtime_error("A custom app action created a second visible top-level window");
}
}
