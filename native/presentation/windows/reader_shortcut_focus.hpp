#pragma once

#include <windows.h>
#include <richedit.h>
#include <cwchar>
#include <initializer_list>

namespace lgaed::presentation::reader_shortcuts {

// Class names alone cannot distinguish an input from the readonly RichEdit
// used to draw a lesson paragraph. The reader supplies its current, live
// narration targets; unregistered readonly editors remain protected.
template<class ReadingText>
bool owns_arrow_keys(HWND target,HWND view,HWND bar,const MSG& message,const ReadingText& is_reading_text) {
    if(!target||target==bar||IsChild(bar,target)||!IsChild(view,target))return false;
    for(auto item=target;item&&item!=view;item=GetParent(item)) {
        wchar_t name[128]{};GetClassNameW(item,name,128);
        if(_wcsnicmp(name,L"RichEdit",8)==0) {
            if((GetWindowLongPtrW(item,GWL_STYLE)&ES_READONLY)&&is_reading_text(item)) {
                CHARRANGE selection{};
                SendMessageW(item,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection));
                // Keep native selection navigation/copying. With just a
                // collapsed caret, curricular prose is a reading surface.
                // Continue checking its parents for interactive containers.
                if(selection.cpMin==selection.cpMax)continue;
            }
            return true;
        }
        if(_wcsicmp(name,L"EDIT")==0||
            _wcsicmp(name,L"LearnGameAndEngineDev.MathExpressionEditor")==0)return true;
        for(const auto kind:{L"ComboBox",L"ComboBoxEx32",L"ComboLBox",L"ListBox",L"SysListView32",L"SysTreeView32",
                L"msctls_trackbar32",L"msctls_updown32",L"ScrollBar",L"ToolbarWindow32"})
            if(_wcsicmp(name,kind)==0)return true;
        if(_wcsicmp(name,L"BUTTON")==0) {
            const auto type=GetWindowLongPtrW(item,GWL_STYLE)&BS_TYPEMASK;
            // Ordinary push/icon buttons (including the reader header) must
            // not disable audio seeking. Choice inputs keep native navigation.
            if(type==BS_RADIOBUTTON||type==BS_AUTORADIOBUTTON||type==BS_CHECKBOX||
                type==BS_AUTOCHECKBOX||type==BS_3STATE||type==BS_AUTO3STATE)return true;
        }
        auto query=message;query.hwnd=item;
        if(SendMessageW(item,WM_GETDLGCODE,message.wParam,reinterpret_cast<LPARAM>(&query))&
            (DLGC_WANTARROWS|DLGC_WANTALLKEYS|DLGC_RADIOBUTTON))return true;
    }
    return false;
}

} // namespace lgaed::presentation::reader_shortcuts
