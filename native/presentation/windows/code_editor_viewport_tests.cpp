#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/editor_render_transaction.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace lgaed;
namespace {
int failed=0,assertions=0,mid_edit_paints=0;bool notifying=false;HWND editor_window=nullptr;
void check(bool ok,const char* message){++assertions;if(!ok){++failed;std::cerr<<"FAIL "<<message<<'\n';}}
LRESULT CALLBACK probe(HWND w,UINT m,WPARAM a,LPARAM b,UINT_PTR,DWORD_PTR){
    if(m==WM_PRINTCLIENT&&notifying)++mid_edit_paints;
    return DefSubclassProc(w,m,a,b);
}
LRESULT CALLBACK parent_proc(HWND w,UINT m,WPARAM a,LPARAM b){
    if(m==WM_COMMAND&&HIWORD(a)==EN_CHANGE&&reinterpret_cast<HWND>(b)==editor_window){
        notifying=true;presentation::style_cpp(editor_window);
        RedrawWindow(editor_window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_NOERASE);
        notifying=false;return 0;
    }
    return DefWindowProcW(w,m,a,b);
}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
struct Raster{
    HDC dc;HBITMAP bitmap;HGDIOBJ old;
    Raster(HWND w,bool native){RECT r{};GetClientRect(w,&r);auto screen=GetDC(w);dc=CreateCompatibleDC(screen);
        bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom);ReleaseDC(w,screen);old=SelectObject(dc,bitmap);
        const auto fill=CreateSolidBrush(presentation::reader_theme::background(w));FillRect(dc,&r,fill);DeleteObject(fill);
        if(native)CallWindowProcW(reinterpret_cast<WNDPROC>(GetClassLongPtrW(w,GCLP_WNDPROC)),w,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        else SendMessageW(w,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    }
    ~Raster(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
};
}
int main(){
    const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich)return 2;
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW type{};type.lpfnWndProc=parent_proc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"EditorViewportRepairQA";
    RegisterClassW(&type);
    const auto host=CreateWindowExW(0,type.lpszClassName,L"Isolated editor viewport QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        30,30,800,550,nullptr,nullptr,type.hInstance,nullptr);
    const auto edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL,
        0,0,650,260,host,reinterpret_cast<HMENU>(100),type.hInstance,nullptr);
    if(!host||!edit)return 2;
    presentation::reader_theme::apply(edit,presentation::reader_theme::Surface::field);
    std::string text;for(int i=0;i<70;++i)text+="int row_"+std::to_string(i)+" = "+std::to_string(i)+"; // Hg gjpqy\n";
    SetWindowSubclass(edit,probe,77,0);
    auto editor=std::make_unique<windows::CodeEditor>(edit,windows::predefined_code_document(text),true);
    presentation::style_cpp(edit);editor_window=edit;
    const auto band=FindWindowExW(host,nullptr,L"LearnGameAndEngineDev.FixedCodeNumbers",nullptr);
    check(band!=nullptr,"Actual gutter sibling missing");
    SetFocus(host);pump();
    for(int zoom:{75,100,125,150,200}){
        SendMessageW(edit,EM_SETZOOM,zoom,100);RECT format{},client{};SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));GetClientRect(edit,&client);
        format.top=16;format.bottom=client.bottom-16;SendMessageW(edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
        SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        check(format.top==0&&format.bottom==client.bottom,"Invisible vertical editor formatting padding remained");
        for(LONG y:{23,57,91,15}){
            POINT target{0,y};SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&target));
            check(!GetUpdateRect(band,nullptr,FALSE),"Gutter paint lagged behind EM_SETSCROLLPOS");
            POINT scroll{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
            auto snapshot=windows::code_gutter_snapshot(edit);
            for(const auto& line:snapshot.visible){POINTL actual{};SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&actual),line.character);
                check(actual.y==line.text_position.y,"Gutter label does not use current native row position");}
            const LONG left=std::max<LONG>(snapshot.width+12,format.left*zoom/100+2);
            Raster composed(edit,false),native(edit,true);std::size_t different=0,ink=0;
            for(int py=0;py<client.bottom;++py)for(int x=left;x<std::min<LONG>(client.right,450);++x){
                const auto expected=GetPixel(native.dc,x,py);if(expected!=presentation::reader_theme::background(edit))++ink;
                if(GetPixel(composed.dc,x,py)!=expected)++different;
            }
            check(ink>0&&different==0,"Partial source rows were erased instead of naturally clipped");
        }
    }
    SendMessageW(edit,EM_SETZOOM,100,100);POINT target{0,300};SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&target));
    POINT before{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&before));
    for(int i=0;i<4;++i){SendMessageW(edit,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-30)),0);
        check(!GetUpdateRect(band,nullptr,FALSE),"Mouse-wheel movement left the number column awaiting paint");}
    POINT after{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&after));
    check(after.y>before.y,"Accumulated high-resolution wheel delta did not scroll code");
    check(editor->document().text==text,"Scrolling changed source characters");
    mid_edit_paints=0;SetFocus(edit);CHARRANGE selection{5,5};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));
    SendMessageW(edit,WM_CHAR,L'x',1);
    check(mid_edit_paints==0,"Syntax formatting painted an intermediate editor frame during typing");
    check(editor->document().text==text.substr(0,5)+"x"+text.substr(5),"A typed character changed another source range");
    editor->undo();check(editor->document().text==text,"Atomic paint transaction damaged Undo");
    editor->redo();check(editor->document().text==text.substr(0,5)+"x"+text.substr(5),"Atomic paint transaction damaged Redo");
    editor->reset({"int main() {}",{},12,12});
    MSG key{edit,WM_KEYDOWN,VK_RETURN,1};const bool dispatched=windows::CodeEditor::handle_key_message(key);
    check(dispatched,"Physical Enter was not routed to the existing smart editor action before dialog dispatch");
    if(!dispatched)SendMessageW(edit,WM_KEYDOWN,VK_RETURN,1);
    check(editor->document().text=="int main() {\n\t\n}","Enter did not split paired braces with Android indentation");
    check(editor->document().selection_start==14&&editor->document().selection_end==14,"Smart Enter did not leave the caret on its indented middle line");
    key.message=WM_KEYUP;(void)windows::CodeEditor::handle_key_message(key);
    editor->undo();check(editor->document().text=="int main() {}","Smart Enter is not one Undo operation");
    const std::string indented="    int count = 1;";editor->reset({indented,{},static_cast<std::int64_t>(indented.size()),static_cast<std::int64_t>(indented.size())});
    key.message=WM_KEYDOWN;const bool second=windows::CodeEditor::handle_key_message(key);if(!second)SendMessageW(edit,WM_KEYDOWN,VK_RETURN,1);
    check(editor->document().text==indented+"\n    ","Enter lost the current line's existing spaces");
    key.message=WM_KEYUP;(void)windows::CodeEditor::handle_key_message(key);
    {
        // Fullscreen reproduction: add many rows, pan downward, then delete
        // the added rows. The native first visible line must return to zero,
        // rather than leaving an empty bottom viewport that cannot pan up.
        MoveWindow(edit,0,0,650,950,TRUE);
        SendMessageW(edit,EM_SETZOOM,100,100);
        std::string expanded;
        for(int row=0;row<110;++row)
            expanded+="int row_"+std::to_string(row)+" = "+std::to_string(row)+";\n";
        editor->reset(windows::predefined_code_document(expanded));
        POINT far_down{0,900};
        SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&far_down));
        POINT saved{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&saved));
        check(saved.y>0,"Fullscreen regression failed to scroll inside its long source");
        const auto cut=SendMessageW(edit,EM_LINEINDEX,18,0);
        check(cut>0,"Fullscreen regression has no eighteenth native line");
        CHARRANGE tail{static_cast<LONG>(cut),-1};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&tail));
        SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L""));
        pump();
        POINT trimmed{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&trimmed));
        check(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)==0&&trimmed.y==0,
            "Deleting fullscreen rows left the editor stuck at its previous bottom scroll");
        const auto shortened=editor->document().text;
        check(shortened.starts_with("int row_0 = 0;")&&
            shortened.find("row_109")==shortened.npos&&
            SendMessageW(edit,EM_GETLINECOUNT,0,0)<=19,
            "Fullscreen scroll repair changed the student's remaining source text");
        editor->undo();pump();
        POINT restored{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&restored));
        check(editor->document().text==expanded&&restored.y>0,
            "Undo failed to recover the long source and its earlier genuine scroll");
        editor->redo();pump();
        POINT redone{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&redone));
        check(editor->document().text==shortened&&redone.y==0&&
            SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)==0,
            "Redo restored a stale fullscreen bottom offset after source shrink");
        // Resizing from a short viewport to fullscreen should also discard
        // an obsolete offset only when every remaining visual line fits.
        MoveWindow(edit,0,0,650,170,TRUE);
        POINT inset{0,160};SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&inset));
        MoveWindow(edit,0,0,650,950,TRUE);pump();
        POINT expanded_view{};SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&expanded_view));
        check(expanded_view.y==0&&SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)==0,
            "Returning to fullscreen retained a stale compact-viewport scroll");
    }
    editor_window=nullptr;editor.reset();DestroyWindow(host);OleUninitialize();FreeLibrary(rich);
    std::cout<<assertions<<" actual Win32 editor checks, "<<failed<<" failures\n";return failed?1:0;
}
