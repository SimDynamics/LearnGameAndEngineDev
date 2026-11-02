#pragma once
#include <windows.h>

namespace lgaed::presentation {
// App pages and editors share the root HWND. The logical owner remains alive;
// closing the child restores that exact page and its current controls.
[[nodiscard]] HWND create_internal_window(HWND owner,const wchar_t* class_name,
    const wchar_t* title,void* creation_data,DWORD extended_style=WS_EX_CONTROLPARENT);
[[nodiscard]] HWND active_internal_window(HWND root);
void layout_internal_windows(HWND root);
// Custom app choice dialogs use a child and a local dialog loop. Native OS
// pickers (e.g. ChooseColorW) retain their documented Windows implementation.
[[nodiscard]] INT_PTR show_internal_dialog(HWND owner,const DLGTEMPLATE*,DLGPROC,LPARAM);
void end_internal_dialog(HWND dialog,INT_PTR result);
// Application notices and confirmations use the same internal navigation.
// The flag/result values match the supported MB_OK / MB_OKCANCEL / MB_YESNO
// forms so existing confirmation policies keep their conservative defaults.
int internal_message_box(HWND owner,const wchar_t* text,const wchar_t* title,UINT flags);
}
