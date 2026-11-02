#include "presentation/windows/internal_window.hpp"
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
std::function<void()> timer_action;
std::exception_ptr timer_error;
bool root_closing=false;
int restored_during_close=0;
LRESULT CALLBACK restoration_probe(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(root_closing&&((message==WM_SHOWWINDOW&&wp)||message==WM_SETFOCUS))++restored_during_close;
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,restoration_probe,1);
    return DefSubclassProc(window,message,wp,lp);
}
LRESULT CALLBACK root_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_SIZE)lgaed::presentation::layout_internal_windows(window);
    if(message==WM_TIMER&&wp==1){KillTimer(window,1);try{timer_action();}catch(...){timer_error=std::current_exception();if(const auto active=lgaed::presentation::active_internal_window(window))PostMessageW(active,WM_CLOSE,0,0);}return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
void bounds(HWND window,HWND root){
    RECT client{},actual{};GetClientRect(root,&client);GetWindowRect(window,&actual);MapWindowPoints(nullptr,root,reinterpret_cast<POINT*>(&actual),2);
    check(EqualRect(&client,&actual)!=FALSE,"An internal page escaped or failed to fill the root client area");
}
struct DialogTest {HWND root,owner;bool initialized=false,checked=false,fail=false;};
INT_PTR CALLBACK dialog_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* data=reinterpret_cast<DialogTest*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){data=reinterpret_cast<DialogTest*>(lp);SetWindowLongPtrW(window,DWLP_USER,lp);data->initialized=true;
        check(GetParent(window)==data->root&&IsWindowEnabled(data->root),"A modal child disabled or replaced the root");
        check(!IsWindowVisible(data->owner),"A modal child failed to cover its live logical owner");
        if(data->fail)throw std::runtime_error("Expected dialog initialization failure");
        PostMessageW(window,WM_APP+1,0,0);return TRUE;
    }
    if(message==WM_APP+1){bounds(window,data->root);data->checked=true;lgaed::presentation::end_internal_dialog(window,IDOK);return TRUE;}
    if(message==WM_COMMAND&&LOWORD(wp)==IDCANCEL){lgaed::presentation::end_internal_dialog(window,IDCANCEL);return TRUE;}
    return FALSE;
}
}
int main(){
    HWND root=nullptr;
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        const auto instance=GetModuleHandleW(nullptr);
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=root_proc;cls.lpszClassName=L"LGAED.InternalNavigation.QA";RegisterClassW(&cls);
        root=CreateWindowW(cls.lpszClassName,L"Internal navigation QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,50,50,800,600,nullptr,nullptr,instance,nullptr);
        check(root!=nullptr,"Cannot create isolated navigation test root");
        const auto page=CreateWindowW(L"STATIC",L"Same live lesson",WS_CHILD|WS_VISIBLE,0,0,700,500,root,nullptr,instance,nullptr);
        const auto editor=CreateWindowW(L"EDIT",L"Unsubmitted answer and selection",WS_CHILD|WS_VISIBLE|WS_TABSTOP,10,10,400,40,page,nullptr,instance,nullptr);
        SetForegroundWindow(root);SetFocus(editor);SendMessageW(editor,EM_SETSEL,4,15);pump();
        const auto first=lgaed::presentation::create_internal_window(page,L"STATIC",L"Options",nullptr);ShowWindow(first,SW_SHOW);
        check(GetAncestor(first,GA_ROOT)==root&&(GetWindowLongPtrW(first,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(first,GWL_STYLE)&WS_POPUP),"Opening a page created a second top-level window");
        check(IsWindowEnabled(root)&&!IsWindowVisible(page)&&IsWindow(editor),"Opening options disabled the root or destroyed lesson state");bounds(first,root);
        const auto second=lgaed::presentation::create_internal_window(first,L"STATIC",L"Licenses",nullptr);ShowWindow(second,SW_SHOW);
        check(lgaed::presentation::active_internal_window(root)==second&&!IsWindowVisible(first),"Nested page navigation exposed both destinations");
        SetWindowPos(root,nullptr,0,0,1100,750,SWP_NOMOVE|SWP_NOZORDER);bounds(second,root);bounds(first,root);
        DestroyWindow(second);check(IsWindowVisible(first)&&!IsWindowVisible(page),"Returning from licenses skipped options");
        DestroyWindow(first);check(IsWindowVisible(page)&&GetFocus()==editor,"Returning to the lesson failed to restore the same focused field");
        DWORD begin=0,end=0;SendMessageW(editor,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
        wchar_t text[100]{};GetWindowTextW(editor,text,100);
        check(begin==4&&end==15&&std::wstring(text)==L"Unsubmitted answer and selection","Internal navigation lost a draft, selection or caret");
        const auto chooser=lgaed::presentation::create_internal_window(page,L"STATIC",L"History chooser",nullptr);ShowWindow(chooser,SW_SHOW);
        const auto comparison=lgaed::presentation::create_internal_window(chooser,L"STATIC",L"Record comparison",nullptr);ShowWindow(comparison,SW_SHOW);
        DestroyWindow(chooser);
        check(!IsWindow(comparison)&&IsWindowVisible(page)&&GetFocus()==editor&&!lgaed::presentation::active_internal_window(root),"Destroying a logical owner left its comparison orphaned or lost the original lesson");
        struct Layout {DLGTEMPLATE header;WORD menu=0,window_class=0,title=0;} layout{};layout.header.style=WS_CHILD|DS_CONTROL;
        DialogTest dialog{root,page};check(lgaed::presentation::show_internal_dialog(page,&layout.header,dialog_proc,reinterpret_cast<LPARAM>(&dialog))==IDOK&&dialog.initialized&&dialog.checked,"Internal modal choice failed to return its result");
        check(IsWindowVisible(page)&&GetFocus()==editor,"A modal choice lost the lesson's focus");
        dialog.fail=true;bool failed=false;try{(void)lgaed::presentation::show_internal_dialog(page,&layout.header,dialog_proc,reinterpret_cast<LPARAM>(&dialog));}catch(const std::runtime_error&){failed=true;}
        check(failed&&IsWindowVisible(page)&&IsWindowEnabled(root)&&!lgaed::presentation::active_internal_window(root),"Initialization failure left an orphan dialog or hidden lesson");
        for(const auto flags:{MB_YESNO|MB_DEFBUTTON2,MB_OKCANCEL|MB_DEFBUTTON2}){
            timer_action=[root,flags]{const auto box=lgaed::presentation::active_internal_window(root);check(box&&GetAncestor(box,GA_ROOT)==root&&IsWindowEnabled(root),"Confirmation opened outside the enabled root");
                wchar_t title[80]{};GetWindowTextW(box,title,80);check(std::wstring(title)==L"Preserve data QA","Confirmation title is inaccessible");
                const auto expected=(flags&MB_TYPEMASK)==MB_YESNO?IDNO:IDCANCEL;
                check(SendMessageW(box,DM_GETDEFID,0,0)==MAKELRESULT(expected,DC_HASDEFID),"A destructive confirmation lost its conservative default");
                MSG enter{};enter.hwnd=GetDlgItem(box,expected);enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;
                check(IsDialogMessageW(box,&enter)!=FALSE,"Confirmation did not handle Enter on its default button");};
            SetTimer(root,1,20,nullptr);const auto result=lgaed::presentation::internal_message_box(page,L"No real data is used by this test.",L"Preserve data QA",flags);
            if(timer_error)std::rethrow_exception(timer_error);
            check(result==((flags&MB_TYPEMASK)==MB_YESNO?IDNO:IDCANCEL),"Enter accepted an operation despite the conservative default");
            check(IsWindowVisible(page)&&GetFocus()==editor,"Closing a confirmation failed to restore the live lesson");
        }
        const auto dependent=lgaed::presentation::create_internal_window(page,L"STATIC",L"Dependent page",nullptr);ShowWindow(dependent,SW_SHOW);
        const auto nested=lgaed::presentation::create_internal_window(dependent,L"STATIC",L"Nested dependent",nullptr);ShowWindow(nested,SW_SHOW);
        DestroyWindow(page);
        check(!IsWindow(dependent)&&!IsWindow(nested)&&IsWindowEnabled(root)&&!lgaed::presentation::active_internal_window(root),"Destroying a lesson left dependent internal pages alive");
        // Settings is a manually hosted child, rather than an internal Surface.
        // The root must still be marked as closing before a license child dies.
        const auto manual=CreateWindowW(L"STATIC",L"Manual settings page",WS_CHILD|WS_VISIBLE,0,0,500,400,root,nullptr,instance,nullptr);
        const auto manual_edit=CreateWindowW(L"EDIT",L"Preserved field",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,200,40,manual,nullptr,instance,nullptr);
        SetWindowSubclass(manual,restoration_probe,1,0);SetWindowSubclass(manual_edit,restoration_probe,1,0);SetFocus(manual_edit);
        const auto root_child=lgaed::presentation::create_internal_window(manual,L"STATIC",L"Settings license",nullptr);ShowWindow(root_child,SW_SHOW);
        root_closing=true;DestroyWindow(root);root=nullptr;
        check(!IsWindow(root_child)&&restored_during_close==0,"Closing the root restored or focused a dying logical owner");
        std::cout<<"PASS internal navigation, resize, focus/drafts, modal errors and conservative confirmations\n";return 0;
    }catch(const std::exception& error){if(IsWindow(root))DestroyWindow(root);std::cerr<<error.what()<<'\n';return 1;}
}
