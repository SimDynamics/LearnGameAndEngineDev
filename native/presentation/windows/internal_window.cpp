#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/window_fullscreen.hpp"
#include <commctrl.h>
#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
constexpr wchar_t surface_property[]=L"LGAED.InternalWindow";
constexpr wchar_t dialog_property[]=L"LGAED.InternalDialog";
constexpr wchar_t closing_property[]=L"LGAED.InternalOwnerClosing";
constexpr UINT_PTR surface_subclass=0x4C474957;
constexpr UINT_PTR owner_subclass=0x4C47494F;
struct Surface { HWND owner=nullptr,focus=nullptr;bool restore=false; };
LRESULT CALLBACK owner_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_DESTROY){
        SetPropW(window,closing_property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
        // Internal pages are direct root children for layout, but retain the
        // original Win32 ownership lifetime. Closing an underlying chooser
        // also closes its comparison/editor, including nested modal loops.
        const auto root=GetAncestor(window,GA_ROOT);
        for(;;){
            HWND dependent=nullptr;
            for(auto child=GetWindow(root,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
                const auto* state=reinterpret_cast<Surface*>(GetPropW(child,surface_property));
                if(state&&state->owner==window){dependent=child;break;}
            }
            if(!dependent||!DestroyWindow(dependent))break;
        }
    }
    if(message==WM_NCDESTROY){
        RemovePropW(window,closing_property);RemoveWindowSubclass(window,owner_proc,owner_subclass);
    }
    return DefSubclassProc(window,message,wp,lp);
}
void fit(HWND window){
    const auto root=GetParent(window);if(!IsWindow(root))return;
    RECT area{};GetClientRect(root,&area);
    SetWindowPos(window,nullptr,0,0,area.right,area.bottom,SWP_NOACTIVATE|SWP_NOZORDER);
}
LRESULT CALLBACK surface_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
    if(message==WM_NCDESTROY){
        const std::unique_ptr<Surface> self(reinterpret_cast<Surface*>(raw));
        RemovePropW(window,surface_property);RemoveWindowSubclass(window,surface_proc,surface_subclass);
        const auto result=DefSubclassProc(window,message,wp,lp);
        if(IsWindow(self->owner)&&!GetPropW(self->owner,closing_property)&&!GetPropW(GetAncestor(self->owner,GA_ROOT),closing_property)){
            if(self->restore)ShowWindow(self->owner,SW_SHOW);
            if(IsWindow(self->focus)&&IsWindowVisible(self->focus)&&IsWindowEnabled(self->focus))SetFocus(self->focus);
            else if(self->restore&&IsWindowEnabled(self->owner))SetFocus(self->owner);
        }
        return result;
    }
    if(message==WM_DPICHANGED){fit(window);SendMessageW(window,WM_DPICHANGED_AFTERPARENT,0,0);return 0;}
    return DefSubclassProc(window,message,wp,lp);
}
void attach(HWND window,HWND owner){
    const auto root=GetAncestor(owner,GA_ROOT);
    auto state=std::make_unique<Surface>();state->owner=owner;
    const auto focus=GetFocus();
    if(focus&&(focus==owner||IsChild(owner,focus)))state->focus=focus;
    state->restore=owner!=root&&IsWindowVisible(owner)!=FALSE;
    if(!SetPropW(window,surface_property,reinterpret_cast<HANDLE>(state.get())))throw std::runtime_error("Cannot attach internal page ownership");
    if(!SetWindowSubclass(window,surface_proc,surface_subclass,reinterpret_cast<DWORD_PTR>(state.get()))){
        RemovePropW(window,surface_property);throw std::runtime_error("Cannot attach internal page lifecycle");
    }
    if(!SetWindowSubclass(owner,owner_proc,owner_subclass,0)||
       (root!=owner&&!SetWindowSubclass(root,owner_proc,owner_subclass,0))){
        RemoveWindowSubclass(window,surface_proc,surface_subclass);RemovePropW(window,surface_property);
        throw std::runtime_error("Cannot attach internal page owner lifetime");
    }
    const bool hide=state->restore;state.release();
    if(hide)ShowWindow(owner,SW_HIDE);
    fit(window);SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
struct Dialog {HWND owner=nullptr,window=nullptr;DLGPROC procedure=nullptr;LPARAM data=0;INT_PTR result=IDCANCEL;bool done=false;std::exception_ptr error;};
struct Message {
    const wchar_t* text;const wchar_t* title;UINT flags;
    HWND heading=nullptr,body=nullptr,first=nullptr,second=nullptr;
    HFONT font=nullptr,title_font=nullptr;
    ~Message(){if(font)DeleteObject(font);if(title_font)DeleteObject(title_font);}
    void layout(HWND window){
        RECT area{};GetClientRect(window,&area);const auto dpi=GetDpiForWindow(window);
        const auto px=[dpi](int value){return MulDiv(value,dpi,96);};
        const int margin=px(24),width=std::max(1L,area.right-2*margin),height=px(48),button=px(160);
        MoveWindow(heading,margin,margin,width,px(56),TRUE);
        MoveWindow(body,margin,margin+px(68),width,std::max(1L,area.bottom-3*margin-px(68)-height),TRUE);
        const int right=area.right-margin;
        MoveWindow(first,right-button,area.bottom-margin-height,button,height,TRUE);
        if(second)MoveWindow(second,std::max(margin,right-2*button-px(12)),area.bottom-margin-height,button,height,TRUE);
    }
};
INT_PTR CALLBACK message_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<Message*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){
        self=reinterpret_cast<Message*>(lp);SetWindowLongPtrW(window,DWLP_USER,lp);
        SetWindowTextW(window,self->title);
        reader_theme::apply(window,reader_theme::Surface::screen);
        const auto make=[window](const wchar_t* kind,const wchar_t* text,DWORD style,int id){return CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);};
        self->heading=make(L"STATIC",self->title,0,10);
        self->body=make(L"EDIT",self->text,WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,11);
        const bool yesno=(self->flags&MB_TYPEMASK)==MB_YESNO,okcancel=(self->flags&MB_TYPEMASK)==MB_OKCANCEL;
        const auto language=PRIMARYLANGID(GetUserDefaultUILanguage());
        const auto yes=language==LANG_SPANISH?L"Sí":language==LANG_FRENCH?L"Oui":L"Yes";
        const auto cancel=language==LANG_SPANISH?L"Cancelar":language==LANG_FRENCH?L"Annuler":L"Cancel";
        self->first=make(L"BUTTON",yesno?yes:L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON,yesno?IDYES:IDOK);
        if(yesno||okcancel)self->second=make(L"BUTTON",yesno?L"No":cancel,WS_TABSTOP|BS_PUSHBUTTON,yesno?IDNO:IDCANCEL);
        if(self->second&&(self->flags&MB_DEFMASK)==MB_DEFBUTTON2){
            SendMessageW(self->first,BM_SETSTYLE,BS_PUSHBUTTON,FALSE);
            SendMessageW(self->second,BM_SETSTYLE,BS_DEFPUSHBUTTON,FALSE);
            SendMessageW(window,DM_SETDEFID,yesno?IDNO:IDCANCEL,0);
        }
        const auto dpi=GetDpiForWindow(window);
        self->font=CreateFontW(-MulDiv(18,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        self->title_font=CreateFontW(-MulDiv(24,dpi,96),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        for(const auto child:{self->heading,self->body,self->first,self->second})if(child){reader_theme::apply(child,reader_theme::Surface::screen);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(child==self->heading?self->title_font:self->font),TRUE);}
        reader_theme::primary_button(self->first);self->layout(window);
        SetFocus(self->second&&(self->flags&MB_DEFMASK)==MB_DEFBUTTON2?self->second:self->first);return FALSE;
    }
    if(!self)return FALSE;
    if(message==WM_SIZE){self->layout(window);return TRUE;}
    if(message==WM_COMMAND){const auto id=LOWORD(wp);
        if(id==IDCANCEL){end_internal_dialog(window,(self->flags&MB_TYPEMASK)==MB_OK?IDOK:IDCANCEL);return TRUE;}
        if(id==IDOK||id==IDYES||id==IDNO){end_internal_dialog(window,id);return TRUE;}
    }
    if(message==WM_CLOSE){end_internal_dialog(window,(self->flags&MB_TYPEMASK)==MB_OK?IDOK:IDCANCEL);return TRUE;}
    if(message==WM_ERASEBKGND){RECT area{};GetClientRect(window,&area);FillRect(reinterpret_cast<HDC>(wp),&area,reader_theme::brush(reader_theme::Surface::screen));return TRUE;}
    if(message==WM_DRAWITEM)return reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
    LRESULT result{};if(reader_theme::control_color(message,wp,lp,result))return result;
    return FALSE;
}
INT_PTR CALLBACK dialog_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<Dialog*>(GetPropW(window,dialog_property));
    try{
    if(message==WM_INITDIALOG){
        state=reinterpret_cast<Dialog*>(lp);state->window=window;
        if(!SetPropW(window,dialog_property,reinterpret_cast<HANDLE>(state)))throw std::runtime_error("Cannot attach the internal dialog state");
        const auto style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE));
        SetWindowLongPtrW(window,GWL_STYLE,(style&~(WS_POPUP|WS_OVERLAPPEDWINDOW))|WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|DS_CONTROL);
        SetParent(window,GetAncestor(state->owner,GA_ROOT));
        attach(window,state->owner);
        return state->procedure(window,message,wp,state->data);
    }
    if(!state)return FALSE;
    const auto result=state->procedure(window,message,wp,lp);
    if(message==WM_NCDESTROY){state->done=true;RemovePropW(window,dialog_property);}
    if(!result&&message==WM_CLOSE){end_internal_dialog(window,IDCANCEL);return TRUE;}
    return result;
    }catch(...){
        if(state){state->error=std::current_exception();state->done=true;}
        if(IsWindow(window))DestroyWindow(window);
        return TRUE;
    }
}
}
HWND create_internal_window(HWND owner,const wchar_t* class_name,const wchar_t* title,void* data,DWORD ex){
    if(!IsWindow(owner))throw std::invalid_argument("An internal page needs a live owner");
    const auto root=GetAncestor(owner,GA_ROOT);RECT area{};GetClientRect(root,&area);
    const auto window=CreateWindowExW(ex,class_name,title,WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        0,0,area.right,area.bottom,root,nullptr,GetModuleHandleW(nullptr),data);
    if(!window)throw std::runtime_error("Cannot create an internal application page");
    try{attach(window,owner);}catch(...){DestroyWindow(window);throw;}
    return window;
}
HWND active_internal_window(HWND root){
    for(auto child=GetWindow(root,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
        if(GetPropW(child,surface_property)&&IsWindowVisible(child))return child;
    return nullptr;
}
void layout_internal_windows(HWND root){
    for(auto child=GetWindow(root,GW_CHILD);child;){const auto next=GetWindow(child,GW_HWNDNEXT);
        if(GetPropW(child,surface_property))fit(child);child=next;}
}
INT_PTR show_internal_dialog(HWND owner,const DLGTEMPLATE* layout,DLGPROC procedure,LPARAM data){
    if(!IsWindow(owner)||!layout||!procedure)throw std::invalid_argument("Invalid internal dialog");
    Dialog state{owner,nullptr,procedure,data};
    const auto dialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),layout,GetAncestor(owner,GA_ROOT),dialog_proc,reinterpret_cast<LPARAM>(&state));
    if(!dialog){if(state.error)std::rethrow_exception(state.error);if(state.done)return state.result;throw std::runtime_error("Cannot create an internal dialog");}
    fit(dialog);ShowWindow(dialog,SW_SHOW);MSG message{};int quit=0;bool quitting=false;
    while(!state.done&&IsWindow(dialog)){
        const auto got=GetMessageW(&message,nullptr,0,0);
        if(got<=0){quitting=got==0;quit=static_cast<int>(message.wParam);break;}
        if(dispatch_window_fullscreen_key(GetAncestor(dialog,GA_ROOT),message))continue;
        if(!IsDialogMessageW(dialog,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(IsWindow(dialog))DestroyWindow(dialog);
    if(quitting)PostQuitMessage(quit);
    if(state.error)std::rethrow_exception(state.error);
    return state.result;
}
void end_internal_dialog(HWND dialog,INT_PTR result){
    if(auto* state=reinterpret_cast<Dialog*>(GetPropW(dialog,dialog_property))){state->result=result;state->done=true;}
    if(IsWindow(dialog))DestroyWindow(dialog);
}
int internal_message_box(HWND owner,const wchar_t* text,const wchar_t* title,UINT flags){
    if(!IsWindow(owner))throw std::invalid_argument("An application message needs a live owner");
    struct Layout {DLGTEMPLATE header;WORD menu=0,window_class=0,title=0;} layout{};
    layout.header.style=WS_CHILD|DS_CONTROL|WS_CLIPCHILDREN;
    Message content{text?text:L"",title?title:L"",flags};
    return static_cast<int>(show_internal_dialog(owner,&layout.header,message_proc,reinterpret_cast<LPARAM>(&content)));
}
}
