#pragma once
#include <windows.h>
#include <cstdint>

namespace lgaed::presentation::editor_render {
inline constexpr wchar_t scope_property[]=L"lgaed.editor-render-depth";
inline constexpr UINT committed_message=WM_APP+0x2e1;
inline bool active(HWND window){return GetPropW(window,scope_property)!=nullptr;}
// TOM freezes native TextServices, not a custom gutter's WM_PRINTCLIENT paint.
// Share a nested UI-thread transaction without changing WS_VISIBLE, focus,
// document contents, selection or the native Undo contract.
class Scope {
    HWND window;HANDLE previous;bool entered;
public:
    explicit Scope(HWND target):window(target),previous(GetPropW(target,scope_property)),
        entered(SetPropW(target,scope_property,reinterpret_cast<HANDLE>(reinterpret_cast<std::uintptr_t>(previous)+1))!=FALSE){}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    ~Scope(){
        if(!entered||!IsWindow(window)||!active(window))return;
        if(previous)SetPropW(window,scope_property,previous);
        else{RemovePropW(window,scope_property);SendMessageW(window,committed_message,0,0);}
    }
};
}
