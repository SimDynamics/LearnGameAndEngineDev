"""Guarded editor repair and real Win32 regression driver. Removed on publication."""
from pathlib import Path
import argparse, os, re
ROOT=Path(__file__).resolve().parents[2]
W=ROOT/'native/presentation/windows'
QA=Path(os.environ.get('RUNNER_TEMP', str(ROOT/'out/qa')))/'editor-viewport-qa'

def once(s,a,b):
    if s.count(a)!=1: raise RuntimeError('Changed or ambiguous anchor: '+a[:120])
    return s.replace(a,b)
def read(p): return p.read_text(encoding='utf-8-sig')
def save(p,s): p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8',newline='\n')
def between(s,a,b,replacement):
    if s.count(a)!=1 or s.count(b)!=1: raise RuntimeError('Changed function boundary '+a)
    start=s.index(a);end=s.index(b,start);return s[:start]+replacement+s[end:]

HEADER=r'''#pragma once
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
'''
TEST=r'''#include "presentation/windows/code_editor.hpp"
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
    key.message=WM_KEYUP;windows::CodeEditor::handle_key_message(key);
    editor->undo();check(editor->document().text=="int main() {}","Smart Enter is not one Undo operation");
    const std::string indented="    int count = 1;";editor->reset({indented,{},static_cast<std::int64_t>(indented.size()),static_cast<std::int64_t>(indented.size())});
    key.message=WM_KEYDOWN;const bool second=windows::CodeEditor::handle_key_message(key);if(!second)SendMessageW(edit,WM_KEYDOWN,VK_RETURN,1);
    check(editor->document().text==indented+"\n    ","Enter lost the current line's existing spaces");
    key.message=WM_KEYUP;windows::CodeEditor::handle_key_message(key);
    editor_window=nullptr;editor.reset();DestroyWindow(host);OleUninitialize();FreeLibrary(rich);
    std::cout<<assertions<<" actual Win32 editor checks, "<<failed<<" failures\n";return failed?1:0;
}
'''

def prepare():
    save(W/'editor_render_transaction.hpp',HEADER)
    save(W/'code_editor_viewport_tests.cpp',TEST)
    QA.mkdir(parents=True,exist_ok=True)
    config=read(ROOT/'cmake/native_build_config.hpp.in')
    values={'LGAED_DEFAULT_PROFILE':'Codex','CMAKE_SOURCE_DIR':ROOT.as_posix()}
    config=re.sub(r'@([^@]+)@',lambda m:values.get(m[1],''),config)
    save(QA/'native_build_config.hpp',config)
    cmake='''cmake_minimum_required(VERSION 3.25)
project(EditorViewportQA LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
add_compile_options(/EHsc /utf-8 /Gy /Gw)
add_link_options(/OPT:REF /OPT:ICF)
include_directories("ROOT/native" "QA")
add_library(controls STATIC
 "ROOT/native/presentation/windows/code_editor.cpp"
 "ROOT/native/presentation/windows/code_gutter.cpp"
 "ROOT/native/presentation/windows/rich_text.cpp"
 "ROOT/native/presentation/windows/reader_theme.cpp"
 "ROOT/native/presentation/windows/icons.cpp"
 "ROOT/native/presentation/windows/native_menu.cpp"
 "ROOT/native/core/lesson.cpp"
 "ROOT/native/core/lesson_state.cpp"
 "ROOT/native/core/profile.cpp"
 "ROOT/native/core/view_preferences.cpp"
 "ROOT/native/platform/windows/state_store.cpp")
target_link_libraries(controls PUBLIC user32 gdi32 gdiplus uxtheme comctl32 ole32 oleaut32 uuid windowsapp bcrypt crypt32 dwmapi imm32)
foreach(name code_editor_viewport code_editor code_gutter code_gutter_lifecycle)
 add_executable(${name} "ROOT/native/presentation/windows/${name}_tests.cpp")
 target_link_libraries(${name} PRIVATE controls)
 add_test(NAME native.${name} COMMAND ${name})
 set_tests_properties(native.${name} PROPERTIES TIMEOUT 180)
endforeach()
add_library(application_compile OBJECT "ROOT/native/presentation/windows/lesson_view.cpp")
'''.replace('ROOT',ROOT.as_posix()).replace('QA',QA.as_posix())
    save(QA/'CMakeLists.txt',cmake)

def fix():
    path=W/'rich_text.cpp';s=read(path)
    s=once(s,'#include "presentation/windows/rich_text.hpp"','#include "presentation/windows/rich_text.hpp"\n#include "presentation/windows/editor_render_transaction.hpp"')
    s=once(s,'struct FormatTransaction {\n    HWND edit;','struct FormatTransaction {\n    editor_render::Scope rendering;\n    HWND edit;')
    s=once(s,'explicit FormatTransaction(HWND target) : edit(target) {','explicit FormatTransaction(HWND target) : rendering(target), edit(target) {')
    save(path,s)
    path=W/'code_editor.cpp';s=read(path)
    s=once(s,'#include "presentation/windows/code_editor.hpp"','#include "presentation/windows/code_editor.hpp"\n#include "presentation/windows/editor_render_transaction.hpp"')
    s=once(s,'        applying=true;\n        {','        applying=true;\n        presentation::editor_render::Scope rendering(window);\n        {')
    s=once(s,'            const auto result = DefSubclassProc(window, message, wparam, lparam);\n            if (message == WM_SETFONT)',
        '''            std::optional<presentation::editor_render::Scope> rendering;
            if(mutating&&!self.applying)rendering.emplace(window);
            LRESULT result=0;
            if(mutating&&!self.applying){
                FrozenText frozen(window);
                result=DefSubclassProc(window,message,wparam,lparam);
            }else result=DefSubclassProc(window,message,wparam,lparam);
            if (message == WM_SETFONT)''')
    s=once(s,'                self.pending_edit.reset();\n            }\n            return result;',
        '''                self.pending_edit.reset();
                if(!self.applying)if(const auto parent=GetParent(window))
                    SendMessageW(parent,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(window),CodeEditor::layout_ready_notification),reinterpret_cast<LPARAM>(window));
            }
            return result;''')
    s=once(s,'    auto& editor=*reinterpret_cast<Impl*>(data);\n    if(message.message==WM_KEYUP)',
        '''    auto& editor=*reinterpret_cast<Impl*>(data);
    if(message.wParam==VK_RETURN&&editor.multiline){
        if(message.message==WM_KEYUP){editor.suppress_enter_char=false;return true;}
        if(!IsWindowEnabled(message.hwnd)||(GetWindowLongPtrW(message.hwnd,GWL_STYLE)&ES_READONLY))return false;
        // Use the existing newline transaction before dialog/default-button
        // routing. No TranslateMessage follows a handled key, so insert once.
        editor.capture();
        SendMessageW(message.hwnd,WM_KEYDOWN,VK_RETURN,message.lParam);
        return true;
    }
    if(message.message==WM_KEYUP)''')
    save(path,s)
    path=W/'code_gutter.cpp';s=read(path)
    s=once(s,'#include "presentation/windows/code_gutter.hpp"','#include "presentation/windows/code_gutter.hpp"\n#include "presentation/windows/editor_render_transaction.hpp"')
    s=once(s,'    bool restore_clip_siblings=false,setting_clip_style=false;','    bool restore_clip_siblings=false,setting_clip_style=false;\n    bool pending_content=false,pending_metrics=false;int wheel_remainder=0;')
    s=once(s,'        if(message==WM_PAINT){\n            PAINTSTRUCT paint{};',
        '''        if(message==WM_PAINT&&presentation::editor_render::active(self->window)){ValidateRect(band,nullptr);return 0;}
        if(message==WM_PAINT){
            PAINTSTRUCT paint{};''')
    s=once(s,'        if(updating||publishing_scroll||!window||!IsWindowVisible(window))return;',
        '        if(updating||publishing_scroll||!window||!IsWindowVisible(window)||presentation::editor_render::active(window))return;')
    s=between(s,'    struct VisibleRows{','    void reveal_caret(){',
        '''    void edge_to_edge_format(){
        RECT client{},format{};GetClientRect(window,&client);SendMessageW(window,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        if(format.top==0&&format.bottom==client.bottom)return;
        // RichEdit formatting insets are permanent clipping planes, unlike
        // Android's scrollable content padding. Keep only horizontal margins.
        format.top=0;format.bottom=client.bottom;
        SendMessageW(window,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
    }
''')
    s=once(s,'        if(updating||!window)return;updating=true;',
        '''        if(updating||!window)return;
        if(presentation::editor_render::active(window)){pending_content|=content;pending_metrics|=metrics;return;}
        updating=true;''')
    s=once(s,'        try{\n            if(content){const auto text=raw_text(window);',
        '        try{\n            edge_to_edge_format();\n            if(content){const auto text=raw_text(window);')
    s=once(s,'        result.active=true;result.width=width;result.logical_lines=starts.size();',
        '''        if(presentation::editor_render::active(window)&&committed_geometry)return *committed_geometry;
        result.active=true;result.width=width;result.logical_lines=starts.size();''')
    s=between(s,'        const auto visible=complete_rows();','        DeleteObject(editor_fill);',
        '        // Leave partially visible source glyphs to native client clipping.\n')
    s=once(s,'        IntersectClipRect(dc,0,viewport.top,width,viewport.bottom);','        IntersectClipRect(dc,0,client.top,width,client.bottom);')
    s=once(s,'        if(message==WM_ERASEBKGND)return 1; // The composed client includes its complete background.',
        '''        if(message==presentation::editor_render::committed_message){
            const bool metrics=self.pending_metrics;self.pending_metrics=false;self.pending_content=false;
            self.refresh(true,metrics);self.publish_scroll();return 0;
        }
        if(message==WM_PAINT&&presentation::editor_render::active(window)){ValidateRect(window,nullptr);return 0;}
        RECT formatting{};
        if((message==EM_SETRECT||message==EM_SETRECTNP)&&lp){
            formatting=*reinterpret_cast<const RECT*>(lp);RECT client{};GetClientRect(window,&client);
            formatting.top=0;formatting.bottom=client.bottom;lp=reinterpret_cast<LPARAM>(&formatting);
        }
        if(message==WM_MOUSEWHEEL&&!(LOWORD(wp)&(MK_CONTROL|MK_SHIFT))){
            SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE};
            if(GetScrollInfo(window,SB_VERT,&range)&&range.nPage&&range.nMax-range.nMin+1>static_cast<int>(range.nPage)){
                UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
                self.wheel_remainder+=static_cast<short>(HIWORD(wp));
                const int turns=self.wheel_remainder/WHEEL_DELTA;self.wheel_remainder%=WHEEL_DELTA;
                if(lines==WHEEL_PAGESCROLL){for(int n=0;n<std::abs(turns);++n)SendMessageW(window,EM_SCROLL,turns>0?SB_PAGEUP:SB_PAGEDOWN,0);}
                else if(turns&&lines)SendMessageW(window,EM_LINESCROLL,0,static_cast<LPARAM>(-turns*static_cast<int>(std::min<UINT>(lines,1000))));
                // Native smooth-wheel animation can bypass our sibling gutter.
                // Use its line/page scroll API, then publish the same viewport.
                if(turns){InvalidateRect(window,nullptr,FALSE);if(self.band_window)InvalidateRect(self.band_window,nullptr,FALSE);self.publish_scroll();}
                return 0;
            }
        }
        if(message==WM_ERASEBKGND)return 1; // The composed client includes its complete background.''')
    s=once(s,'const bool may_scroll=message==WM_SIZE||message==WM_VSCROLL',
        'const bool may_scroll=message==WM_TIMER||message==EM_SCROLL||message==WM_SIZE||message==WM_VSCROLL')
    s=once(s,'const bool native_scroll=message==WM_HSCROLL||message==WM_VSCROLL||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL;',
        '''const bool native_scroll=message==WM_HSCROLL||message==WM_VSCROLL||message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL||
                        message==EM_SETSCROLLPOS||message==EM_LINESCROLL||message==EM_SCROLL||message==WM_TIMER||message==WM_MOUSEMOVE;''')
    s=once(s,'            if(message==WM_SIZE||message==EM_SETRECT||message==EM_SETRECTNP||message==EM_SETTARGETDEVICE){\n                self.position_band();',
        '            if(message==WM_SIZE||message==EM_SETRECT||message==EM_SETRECTNP||message==EM_SETTARGETDEVICE){\n                self.edge_to_edge_format();\n                self.position_band();')
    save(path,s)
    path=W/'code_gutter.hpp';s=read(path)
    s=between(s,'// Paints the fixed RichEdit margin','class CodeGutter {',
        '''// Fixed logical-line numbers share the native code viewport. Partial
// glyphs and numbers are naturally clipped at its actual client edges.
// Owns no text, selection, history or input window. Caret navigation still
// reveals its complete row, without permanently masking partial source rows.
''')
    save(path,s)
    path=W/'code_gutter_tests.cpp';s=read(path)
    a=s.index('void partial_number_edges(');b=s.index('void code_whole_rows(',a)
    part=s[a:b];part=once(part,'const auto display_inset=static_cast<LONG>(static_cast<long long>(native_inset)*zoom/100);','const LONG display_inset=0;')
    s=s[:a]+part+s[b:]
    replacement=r'''void code_whole_rows(bool readonly,bool wrap=false){
    // Regression now follows Android: clipped source rows remain visible,
    // rather than erasing them to enforce whole-line-only presentation.
    const auto text=source(24);Fixture field(text,true,true,wrap?180:500,wrap);
    presentation::set_rich_text(field.edit,text,{},true);
    if(readonly)SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);
    ShowWindow(field.host,SW_SHOWNOACTIVATE);SetWindowPos(field.host,HWND_TOPMOST,40,40,650,380,SWP_NOACTIVATE);
    SetFocus(field.host);
    for(int zoom:{75,100,150,200}){
        SendMessageW(field.edit,EM_SETZOOM,zoom,100);
        RECT client{},format{};GetClientRect(field.edit,&client);SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        format.top=11;format.bottom=client.bottom-27;SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
        SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        check(format.top==0&&format.bottom==client.bottom,"Code retained an invisible vertical formatting inset");
        for(LONG y:{7,23,57,91}){
            POINT requested{0,y};SendMessageW(field.edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&requested));
            RedrawWindow(field.edit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);pump();
            const auto gutter=code_gutter_snapshot(field.edit);Raster composed(field.edit),native(field.edit,true);
            const auto left=std::max<LONG>(gutter.width+10,format.left*zoom/100+1);
            std::size_t ink=0,different=0;const auto background=presentation::reader_theme::background(field.edit);
            for(int row=0;row<client.bottom;++row)for(int x=left;x<client.right;++x){
                const auto original=GetPixel(native.dc,x,row);if(original!=background)++ink;
                if(GetPixel(composed.dc,x,row)!=original)++different;
            }
            check(ink>0&&!different,"Partially visible source glyphs differ from the native renderer");
        }
    }
    check(field.editor->document().text==text&&code_gutter_snapshot(field.edit).logical_lines==24,
        "Partial-line clipping changed source characters or logical numbering");
}
'''
    s=between(s,'void code_whole_rows(','void scrollbar_release_keeps_viewport(',replacement)
    for var in ('format','content'):
        anchor=f'SendMessageW(field.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&{var}));'
        s=s.replace(anchor,anchor+f'SendMessageW(field.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&{var}));')
    s=s.replace('shows whole intermediate rows and reveals its caret without changing Undo','preserves partially visible source rows')
    s=s.replace('shows whole intermediate rows and reveals selection','preserves partially visible source rows')
    s=s.replace('masks only partial visual rows and preserves complete paragraphs','retains partial visual rows and complete paragraphs')
    save(path,s)
    path=ROOT/'CMakeLists.txt';s=read(path)
    anchor='    add_executable(lgaed_code_gutter_tests native/presentation/windows/code_gutter_tests.cpp)'
    new='''    add_executable(lgaed_code_editor_viewport_tests native/presentation/windows/code_editor_viewport_tests.cpp)
    target_link_libraries(lgaed_code_editor_viewport_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.code_editor_viewport COMMAND lgaed_code_editor_viewport_tests)
    set_tests_properties(native.code_editor_viewport PROPERTIES TIMEOUT 120)
'''
    save(path,once(s,anchor,new+anchor))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('stage',choices=['prepare','fix']);stage=parser.parse_args().stage
    (prepare if stage=='prepare' else fix)();print('Editor viewport stage:',stage)
