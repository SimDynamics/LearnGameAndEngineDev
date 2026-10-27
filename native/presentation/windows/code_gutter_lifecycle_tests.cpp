#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/code_gutter.hpp"
#include "presentation/windows/reader_theme.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <richedit.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
namespace theme=presentation::reader_theme;
constexpr wchar_t band_class[]=L"LearnGameAndEngineDev.FixedCodeNumbers";
static_assert(sizeof(void*)==8&&sizeof(DWORD_PTR)==8,"The lifecycle regression targets the Windows x64 preset");

void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool local_visible(HWND window){return (GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=0;}
std::string source(unsigned lines){std::string result;for(unsigned line=0;line<lines;++line){
    if(line)result+='\n';result+="    int value_"+std::to_string(line)+" = "+std::to_string(line)+";";}return result;}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}}
const wchar_t* host_class(){static const bool registered=[] {
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.hInstance=GetModuleHandleW(nullptr);
    type.lpfnWndProc=DefWindowProcW;type.lpszClassName=L"LearnGameAndEngineDev.CodeGutterLifecycleQA";
    check(RegisterClassExW(&type)!=0,"Cannot register the isolated lifecycle host");return true;}();
    (void)registered;return L"LearnGameAndEngineDev.CodeGutterLifecycleQA";}

struct Fixture {
    HWND host=nullptr,first_pane=nullptr,second_pane=nullptr,edit=nullptr;
    HFONT font=nullptr;
    std::unique_ptr<CodeEditor> editor;
    explicit Fixture(bool clipped=false,bool native_theme=false){
        host=CreateWindowExW(WS_EX_NOACTIVATE,host_class(),L"Isolated gutter lifecycle QA",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            40,40,1000,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the isolated lifecycle host");
        const BOOL disable=TRUE;(void)DwmSetWindowAttribute(host,DWMWA_TRANSITIONS_FORCEDISABLED,&disable,sizeof(disable));
        const auto pane_style=WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS;
        first_pane=CreateWindowExW(WS_EX_CONTROLPARENT,host_class(),L"",pane_style,17,21,900,300,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        second_pane=CreateWindowExW(WS_EX_CONTROLPARENT,host_class(),L"",pane_style,31,350,900,260,host,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(first_pane&&second_pane,"Cannot create isolated viewport panes");
        const DWORD style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_AUTOHSCROLL|ES_AUTOVSCROLL|
            (clipped?WS_CLIPSIBLINGS:0);
        edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",style,23,29,600,230,first_pane,reinterpret_cast<HMENU>(1001),GetModuleHandleW(nullptr),nullptr);
        check(edit!=nullptr,"Cannot create the lifecycle RichEdit");
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,FIXED_PITCH,L"Consolas");
        check(font!=nullptr,"Cannot create the lifecycle source font");
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        theme::apply(edit,theme::Surface::field);theme::use_native_scrollbar_theme(edit,native_theme);
    }
    void attach(std::string text=source(13)){editor=std::make_unique<CodeEditor>(edit,predefined_code_document(text),true);}
    ~Fixture(){editor.reset();if(host)DestroyWindow(host);if(font)DeleteObject(font);}
};

HWND matching_band(HWND edit){
    const auto snapshot=code_gutter_snapshot(edit);check(snapshot.active&&snapshot.width>0,"The source has no active number band");
    RECT client{};GetClientRect(edit,&client);POINT origin{};ClientToScreen(edit,&origin);
    HWND found=nullptr;
    for(auto child=GetWindow(GetParent(edit),GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        wchar_t type[128]{};GetClassNameW(child,type,128);if(std::wstring_view(type)!=band_class)continue;
        RECT area{};GetWindowRect(child,&area);
        if(area.left==origin.x&&area.top==origin.y&&area.bottom==origin.y+client.bottom&&
                area.right>=origin.x+std::min<LONG>(snapshot.width,client.right)&&area.right<=origin.x+client.right){
            check(!found,"Several decorative bands claimed one source viewport");found=child;}
    }
    check(found!=nullptr,"No sibling number band matches the actual native client rectangle");return found;
}
void verify_band(HWND edit,HWND expected=nullptr){
    const auto band=matching_band(edit);if(expected)check(band==expected,"Viewport synchronization replaced the existing decorative band");
    check(GetParent(band)==GetParent(edit)&&GetParent(band)!=edit,"The fixed band reentered RichEdit's scrolling child surface");
    const auto style=GetWindowLongPtrW(band,GWL_STYLE),extended=GetWindowLongPtrW(band,GWL_EXSTYLE);
    check((style&(WS_CHILD|WS_DISABLED|WS_CLIPSIBLINGS))==(WS_CHILD|WS_DISABLED|WS_CLIPSIBLINGS)&&!(style&WS_TABSTOP),
        "The decorative band acquired input or lost sibling clipping");
    check(!IsWindowEnabled(band)&&(extended&WS_EX_NOACTIVATE),"The decorative band can activate or accept input");
    check(SendMessageW(band,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE&&SendMessageW(band,WM_NCHITTEST,0,0)==HTTRANSPARENT,
        "The decorative band intercepts source interaction");
    check(local_visible(band)==local_visible(edit),"Source and band visibility diverged");
    check((GetWindowLongPtrW(edit,GWL_STYLE)&WS_CLIPSIBLINGS)!=0,"RichEdit can draw over its fixed sibling");
    RECT client{},format{};GetClientRect(edit,&client);SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
    int numerator=0,denominator=0;SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
    if(numerator<=0||denominator<=0){numerator=1;denominator=1;}
    const auto text_left=static_cast<LONG>(static_cast<long long>(format.left)*numerator/denominator);
    const auto gap=MulDiv(10,static_cast<int>(GetDpiForWindow(edit)),96);
    const auto snapshot=code_gutter_snapshot(edit);
    check(client.right<snapshot.width+gap||text_left>=snapshot.width+gap,"The number column consumed the fixed source gap");
}

void sibling_geometry_and_visibility(){
    Fixture field;field.attach();const auto band=matching_band(field.edit);verify_band(field.edit,band);
    const auto before=field.editor->document();const auto focus=GetFocus();
    for(const auto size:{SIZE{670,240},SIZE{320,170},SIZE{760,260}}){
        check(MoveWindow(field.edit,43,11,size.cx,size.cy,TRUE)!=FALSE,"Cannot move the native source viewport");
        pump();verify_band(field.edit,band);
        ShowWindow(field.edit,SW_HIDE);pump();check(!local_visible(field.edit)&&!local_visible(band),"Hiding only the source left its number band visible");
        verify_band(field.edit,band);ShowWindow(field.edit,SW_SHOWNOACTIVATE);pump();verify_band(field.edit,band);
    }
    // An ancestor may be hidden while a source retains its own visible bit.
    // The sibling must follow that bit so it returns with the same pane.
    ShowWindow(field.first_pane,SW_HIDE);MoveWindow(field.edit,61,17,480,210,TRUE);pump();verify_band(field.edit,band);
    ShowWindow(field.first_pane,SW_SHOWNOACTIVATE);pump();verify_band(field.edit,band);
    const auto after=field.editor->document();check(after.text==before.text&&after.selection_start==before.selection_start&&
        after.selection_end==before.selection_end&&after.indentation_starts==before.indentation_starts,"Moving the decorative band altered source or selection");
    check(GetFocus()==focus,"Band synchronization stole keyboard focus");
}

void reparent_source_and_readonly(){
    Fixture field;field.attach();const auto band=matching_band(field.edit);
    const auto replacement=source(130);auto document=predefined_code_document(replacement);document.selection_start=4;document.selection_end=12;
    field.editor->reset(std::move(document));check(code_gutter_snapshot(field.edit).logical_lines==130,"A source replacement retained obsolete paragraph identities");
    SendMessageW(field.edit,EM_SETREADONLY,TRUE,0);const auto before=field.editor->document();
    for(const auto parent:{field.second_pane,field.first_pane,field.second_pane}){
        check(SetParent(field.edit,parent)!=nullptr,"Cannot reparent the existing source to another viewport pane");
        // LessonView reparents first, then commits its final native position.
        SetWindowPos(field.edit,nullptr,37,9,520,200,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);pump();
        verify_band(field.edit,band);check((GetWindowLongPtrW(field.edit,GWL_STYLE)&ES_READONLY)!=0,"Band synchronization unlocked readonly source");
        for(const auto zoom:{75,100,150}){SendMessageW(field.edit,EM_SETZOOM,zoom,100);verify_band(field.edit,band);}
        ShowWindow(field.edit,SW_HIDE);pump();verify_band(field.edit,band);ShowWindow(field.edit,SW_SHOWNOACTIVATE);pump();verify_band(field.edit,band);
    }
    SendMessageW(field.edit,WM_CHAR,L'x',1);
    const auto after=field.editor->document();check(after.text==before.text&&after.selection_start==before.selection_start&&
        after.selection_end==before.selection_end&&after.indentation_starts==before.indentation_starts,
        "Reparenting or readonly keyboard routing changed the authored source or selection");
}

void direct_source_destruction(){
    Fixture field;field.attach();const auto band=matching_band(field.edit);const auto source_window=field.edit;
    check(DestroyWindow(source_window)!=FALSE,"Cannot destroy the isolated source directly");field.edit=nullptr;
    check(!IsWindow(source_window)&&!IsWindow(band),"Destroying RichEdit left a detached sibling number band alive");
    field.editor.reset();check(IsWindow(field.first_pane)&&IsWindow(field.second_pane),"Adapter teardown destroyed a caller-owned viewport pane");
}

void adapter_restores_caller_policy(bool initial_clip,bool initial_native,bool clear_clip){
    Fixture field(initial_clip,initial_native);
    field.attach();const auto band=matching_band(field.edit);const auto before=field.editor->document();
    auto requested=GetWindowLongPtrW(field.edit,GWL_STYLE)|WS_GROUP;
    if(clear_clip)requested&=~static_cast<LONG_PTR>(WS_CLIPSIBLINGS);
    SetWindowLongPtrW(field.edit,GWL_STYLE,requested);
    check((GetWindowLongPtrW(field.edit,GWL_STYLE)&WS_CLIPSIBLINGS)!=0,"An active source lost protective sibling clipping");
    const auto before_detach=GetWindowLongPtrW(field.edit,GWL_STYLE);
    field.editor.reset();check(!IsWindow(band),"Normal adapter destruction leaked its decorative sibling");
    const auto restored=GetWindowLongPtrW(field.edit,GWL_STYLE);
    const bool expected_clip=clear_clip?false:initial_clip;
    check(((restored&WS_CLIPSIBLINGS)!=0)==expected_clip,"Adapter teardown replaced the caller's clipping preference");
    check((restored&WS_GROUP)!=0,"Adapter teardown discarded an unrelated caller style change");
    check((restored&~static_cast<LONG_PTR>(WS_CLIPSIBLINGS|WS_GROUP))==(before_detach&~static_cast<LONG_PTR>(WS_CLIPSIBLINGS|WS_GROUP)),
        "Adapter teardown changed native source or scrollbar styles");
    check(theme::native_scrollbar_theme(field.edit)==initial_native,"Adapter teardown replaced the caller's native scrollbar policy");
    check(!code_gutter_snapshot(field.edit).active,"Destroyed adapter still exposes an active number-band identity");
    GETTEXTLENGTHEX length{GTL_NUMCHARS|GTL_PRECISE,1200};const auto count=SendMessageW(field.edit,EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    check(count>=0,"Cannot read the detached source length");std::wstring detached(static_cast<std::size_t>(count)+1,L'\0');
    GETTEXTEX get{static_cast<DWORD>(detached.size()*sizeof(wchar_t)),GT_RAWTEXT,1200,nullptr,nullptr};
    const auto copied=SendMessageW(field.edit,EM_GETTEXTEX,reinterpret_cast<WPARAM>(&get),reinterpret_cast<LPARAM>(detached.data()));
    check(copied>=0,"Cannot read the detached source story");detached.resize(static_cast<std::size_t>(copied));
    for(auto& character:detached)if(character==L'\r')character=L'\n';
    check(detached==std::wstring(before.text.begin(),before.text.end()),"Adapter teardown changed the native source story");
}
}

int main(){
    std::cout<<std::unitbuf;const auto previous_dpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(!previous_dpi){std::cerr<<"Cannot establish physical-pixel lifecycle geometry\n";return 2;}
    const auto rich=LoadLibraryW(L"Msftedit.dll");if(!rich){SetThreadDpiAwarenessContext(previous_dpi);return 2;}
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);theme::prepare_fonts();
    int passed=0,failed=0;const auto test=[&](const char* name,const std::function<void()>& action){try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    test("Fixed sibling follows source geometry and local hide/show without focus",sibling_geometry_and_visibility);
    test("Existing band follows reparented readonly source through zoom and source replacement",reparent_source_and_readonly);
    test("Direct source destruction immediately destroys its decorative sibling",direct_source_destruction);
    for(const bool clipped:{false,true})for(const bool native:{false,true}){
        test("Normal adapter teardown restores existing caller clipping and scrollbar policy",[=]{adapter_restores_caller_policy(clipped,native,false);});
        test("Normal adapter teardown preserves an explicit caller clipping removal",[=]{adapter_restores_caller_policy(clipped,native,true);});
    }
    FreeLibrary(rich);SetThreadDpiAwarenessContext(previous_dpi);
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
