#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/state_store.hpp"

#include <richedit.h>
#include <commctrl.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Window { HWND value; ~Window() { if (value) DestroyWindow(value); } };
struct HintGestureProbe {
    std::vector<UINT> messages;
    bool consume=false;
    static LRESULT CALLBACK proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR value) {
        auto& self=*reinterpret_cast<HintGestureProbe*>(value);
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_MOUSEMOVE||message==WM_LBUTTONUP){
            self.messages.push_back(message);
            if(self.consume){
                if(message==WM_LBUTTONDOWN)SetCapture(item);
                if(message==WM_LBUTTONUP&&GetCapture()==item)ReleaseCapture();
                return 73;
            }
        }
        return DefSubclassProc(item,message,wp,lp);
    }
};
CHARFORMAT2W character(HWND edit, LONG position) {
    CHARRANGE before{}; SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&before));
    CHARRANGE range{position,position + 1}; SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W result{}; result.cbSize = sizeof(result);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&result));
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
    return result;
}
}
int main() {
    const auto module = LoadLibraryW(L"Msftedit.dll");
    if (!module) return 2;
    try {
        using namespace lgaed::presentation;
        Window parent{CreateWindowExW(0, L"STATIC", L"RichEdit QA", WS_OVERLAPPEDWINDOW,
            0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
        check(parent.value != nullptr, "Cannot create QA parent");
        Window editor{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 500, 300, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
        check(editor.value != nullptr, "Cannot create real RichEdit");
        {
            Window hinted{CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE,
                0,0,360,90,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(hinted.value!=nullptr,"Cannot create the real hinted RichEdit");reader_theme::apply(hinted.value,reader_theme::Surface::card);
            set_rich_text(hinted.value,"",{},true);SendMessageW(hinted.value,EM_EMPTYUNDOBUFFER,0,0);
            HintGestureProbe gesture;
            check(SetWindowSubclass(hinted.value,HintGestureProbe::proc,9917,reinterpret_cast<DWORD_PTR>(&gesture))!=FALSE,
                "Cannot attach the hint's lower gesture probe");
            const auto pixels=[&]{RECT bounds{};GetClientRect(hinted.value,&bounds);const auto dc=CreateCompatibleDC(nullptr);
                BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;
                info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                void* memory=nullptr;const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&memory,nullptr,0);
                check(bitmap&&memory,"Create the isolated hint drawing surface");const auto previous=SelectObject(dc,bitmap);
                FillRect(dc,&bounds,reader_theme::brush(reader_theme::Surface::card));
                SendMessageW(hinted.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);GdiFlush();
                const auto begin=static_cast<const std::uint32_t*>(memory);std::vector<std::uint32_t> result(begin,begin+bounds.right*bounds.bottom);
                SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return result;};
            const auto empty=pixels();const auto modified=SendMessageW(hinted.value,EM_GETMODIFY,0,0);
            set_edit_hint(hinted.value,"Escribe el código faltante");const auto hint=pixels();
            check(hint!=empty&&edit_text(hinted.value).empty()&&GetWindowTextLengthW(hinted.value)==0&&
                !SendMessageW(hinted.value,EM_CANUNDO,0,0)&&SendMessageW(hinted.value,EM_GETMODIFY,0,0)==modified,
                "The empty hint was not painted, became false document text, changed Modify or entered Undo");
            set_edit_hint(hinted.value,"Escribe el código faltante",RGB(174,183,198));
            check(pixels()!=hint&&edit_text(hinted.value).empty()&&!SendMessageW(hinted.value,EM_CANUNDO,0,0),
                "The mathematical hint color was ignored or changed the editable document");
            set_edit_hint(hinted.value,"Escribe el código faltante");
            SetFocus(hinted.value);const auto focus=GetFocus();CHARRANGE before{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
            (void)pixels();CHARRANGE after{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
            check(focus==hinted.value&&GetFocus()==focus&&before.cpMin==after.cpMin&&before.cpMax==after.cpMax,
                "Drawing a focused hint moved the native caret, selection or focus");
            SendMessageW(hinted.value,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"x"));const auto typed=pixels();
            set_edit_hint(hinted.value,"");check(pixels()==typed&&edit_text(hinted.value)=="x","A hint remained over the actual typed text");
            set_edit_hint(hinted.value,"Escribe el código faltante");
            check(SendMessageW(hinted.value,EM_UNDO,0,0)&&edit_text(hinted.value).empty()&&pixels()==hint,
                "Undo failed to restore an empty document with a purely visual hint");
            check(SendMessageW(hinted.value,EM_CANREDO,0,0)!=0,"Painting the restored hint discarded Redo");
            SendMessageW(hinted.value,EM_SETREADONLY,TRUE,0);const auto readonly=pixels();set_edit_hint(hinted.value,"");
            check(pixels()==readonly&&edit_text(hinted.value).empty(),"A readonly display acquired an editable placeholder");
            SendMessageW(hinted.value,EM_SETREADONLY,FALSE,0);
            set_edit_hint(hinted.value,"Escribe la salida");
            const auto empty_selection=[&]{
                CHARRANGE range{};SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&range));
                check(range.cpMin==0&&range.cpMax==0&&edit_text(hinted.value).empty(),
                    "The synthetic final paragraph was selected as if it were the placeholder's first letter");
            };
            SendMessageW(hinted.value,EM_SETSEL,0,-1);empty_selection();
            CHARRANGE synthetic{0,1};SendMessageW(hinted.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&synthetic));empty_selection();
            POINTL start{};SendMessageW(hinted.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&start),0);
            const auto down=MAKELPARAM(start.x+1,start.y+1),moved=MAKELPARAM(start.x+90,start.y+40);
            gesture.messages.clear();
            SendMessageW(hinted.value,WM_LBUTTONDOWN,MK_LBUTTON,down);empty_selection();
            SendMessageW(hinted.value,WM_MOUSEMOVE,MK_LBUTTON,moved);empty_selection();
            SendMessageW(hinted.value,WM_LBUTTONUP,0,moved);empty_selection();
            SendMessageW(hinted.value,WM_LBUTTONDBLCLK,MK_LBUTTON,down);empty_selection();
            SendMessageW(hinted.value,WM_LBUTTONUP,0,down);empty_selection();
            check(gesture.messages==std::vector<UINT>{WM_LBUTTONDOWN,WM_MOUSEMOVE,WM_LBUTTONUP,WM_LBUTTONDBLCLK,WM_LBUTTONUP},
                "The empty hint swallowed part of the native pointer sequence");
            gesture.consume=true;gesture.messages.clear();
            check(SendMessageW(hinted.value,WM_LBUTTONDOWN,MK_LBUTTON,down)==73&&GetCapture()==hinted.value&&
                SendMessageW(hinted.value,WM_MOUSEMOVE,MK_LBUTTON,moved)==73&&GetCapture()==hinted.value&&
                SendMessageW(hinted.value,WM_LBUTTONUP,0,moved)==73&&GetCapture()!=hinted.value,
                "The hint blocked a lower panning handler or stole its mouse capture");
            empty_selection();gesture.consume=false;
            SendMessageW(hinted.value,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Escribe"));
            SendMessageW(hinted.value,EM_SETSEL,0,1);CHARRANGE selected{};
            SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
            wchar_t copied[16]{};SendMessageW(hinted.value,EM_GETSELTEXT,0,reinterpret_cast<LPARAM>(copied));
            check(selected.cpMin==0&&selected.cpMax==1&&std::wstring_view(copied)==L"E",
                "The hint guard prevented selecting the first letter of real entered text");
            set_edit_text(hinted.value,"");set_edit_hint(hinted.value,"");
            SendMessageW(hinted.value,EM_SETSEL,0,-1);SendMessageW(hinted.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
            check(selected.cpMin==0&&selected.cpMax==1,"An inactive hint changed ordinary native selection behavior");
            SendMessageW(hinted.value,EM_SETSEL,0,0);
            // Android's hint and entered text use the same native line. Check
            // actual glyph bounds, not just a nonempty placeholder bitmap.
            const auto hint_dpi=static_cast<int>(GetDpiForWindow(hinted.value));
            SetWindowPos(hinted.value,nullptr,0,0,MulDiv(600,hint_dpi,96),MulDiv(160,hint_dpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(parent.value,SW_SHOWNOACTIVATE);
            const auto ink_bounds=[&](const auto& image,const auto& background){
                RECT client{};GetClientRect(hinted.value,&client);RECT ink{client.right,client.bottom,0,0};
                for(LONG y=0;y<client.bottom;++y)for(LONG x=0;x<client.right;++x){
                    const auto at=static_cast<std::size_t>(y*client.right+x);
                    if(((image[at]^background[at])&0x00ffffffu)==0)continue;
                    ink.left=std::min(ink.left,x);ink.top=std::min(ink.top,y);
                    ink.right=std::max(ink.right,x+1);ink.bottom=std::max(ink.bottom,y+1);
                }
                return ink;
            };
            const auto native_viewport=[&]{
                Microsoft::WRL::ComPtr<IRichEditOle> rich;
                SendMessageW(hinted.value,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(rich.GetAddressOf()));
                Microsoft::WRL::ComPtr<ITextDocument2> document;
                check(rich&&SUCCEEDED(rich.As(&document)),"Cannot inspect the real hint text viewport");
                RECT viewport{};
                check(document->GetClientRect(tomClientCoord|tomIncludeInset,&viewport.left,&viewport.top,&viewport.right,&viewport.bottom)==S_OK,
                    "Cannot measure all four real hint text insets");
                return viewport;
            };
            for(const auto zoom:{75,100,125,150,175,200}){
                SetFocus(parent.value);set_edit_hint(hinted.value,"");set_edit_text(hinted.value,"");
                SendMessageW(hinted.value,EM_EMPTYUNDOBUFFER,0,0);SendMessageW(hinted.value,EM_SETZOOM,zoom,100);
                const int padding=MulDiv(16,static_cast<int>(GetDpiForWindow(hinted.value)),96);
                RECT area{};GetClientRect(hinted.value,&area);
                const int inset=MulDiv(padding,100,zoom);
                RECT format{inset,inset,area.right-inset,area.bottom-inset};
                SendMessageW(hinted.value,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
                const auto blank=pixels();set_edit_hint(hinted.value,"Escribe la salida",reader_theme::foreground(hinted.value));
                const auto placeholder_ink=ink_bounds(pixels(),blank);
                check(placeholder_ink.right>placeholder_ink.left&&placeholder_ink.bottom>placeholder_ink.top,
                    "The zoomed native answer hint disappeared");
                SetFocus(hinted.value);RedrawWindow(hinted.value,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
                GUITHREADINFO caret_before{};caret_before.cbSize=sizeof(caret_before);
                check(GetGUIThreadInfo(GetCurrentThreadId(),&caret_before)&&caret_before.hwndCaret==hinted.value&&
                    caret_before.rcCaret.bottom>caret_before.rcCaret.top&&caret_before.rcCaret.top>=0&&
                    caret_before.rcCaret.bottom<=area.bottom-padding,
                    "Focusing a zoomed empty answer did not retain its complete native editing caret");
                for(int repaint=0;repaint<3;++repaint)RedrawWindow(hinted.value,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
                GUITHREADINFO caret_after{};caret_after.cbSize=sizeof(caret_after);
                check(GetGUIThreadInfo(GetCurrentThreadId(),&caret_after)&&caret_after.hwndCaret==hinted.value&&
                    EqualRect(&caret_before.rcCaret,&caret_after.rcCaret),
                    "Repainting the hint moved or replaced the native caret");
                SendMessageW(hinted.value,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Escribe la salida"));
                SetFocus(parent.value);const auto text_ink=ink_bounds(pixels(),blank);
                check(std::abs(placeholder_ink.left-text_ink.left)<=1&&std::abs(placeholder_ink.top-text_ink.top)<=1&&
                    std::abs(placeholder_ink.right-text_ink.right)<=2&&std::abs(placeholder_ink.bottom-text_ink.bottom)<=1,
                    "Entering the first line moved or clipped text relative to its zoomed placeholder");
                check(SendMessageW(hinted.value,EM_UNDO,0,0)&&edit_text(hinted.value).empty()&&
                    SendMessageW(hinted.value,EM_CANREDO,0,0),"Zoomed hint painting changed the original text Undo/Redo history");
                // A long, wrapped hint must clip exactly like native text at
                // every edge, including zoomed right/bottom asymmetric insets.
                SetFocus(parent.value);set_edit_hint(hinted.value,"");
                RECT asymmetric{MulDiv(MulDiv(11,hint_dpi,96),100,zoom),MulDiv(MulDiv(13,hint_dpi,96),100,zoom),
                    area.right-MulDiv(MulDiv(29,hint_dpi,96),100,zoom),area.bottom-MulDiv(MulDiv(37,hint_dpi,96),100,zoom)};
                SendMessageW(hinted.value,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&asymmetric));
                const auto without_hint=pixels();const auto viewport=native_viewport();
                const std::string long_hint=std::string(120,'W')+"\nEscribe la salida\nEscribe la salida\nEscribe la salida\nEscribe la salida";
                set_edit_hint(hinted.value,long_hint);const auto clipped=pixels();std::size_t ink=0;
                for(LONG y=0;y<area.bottom;++y)for(LONG x=0;x<area.right;++x){
                    const auto at=static_cast<std::size_t>(y*area.right+x);
                    if(((clipped[at]^without_hint[at])&0x00ffffffu)==0)continue;
                    ++ink;
                    check(x>=viewport.left&&x<viewport.right&&y>=viewport.top&&y<viewport.bottom,
                        "Hint pixels escaped the native zoomed text viewport");
                }
                check(ink>20&&edit_text(hinted.value).empty()&&SendMessageW(hinted.value,EM_CANREDO,0,0),
                    "Clipping the hint lost its text, modified the document or discarded Redo");
            }
            RemoveWindowSubclass(hinted.value,HintGestureProbe::proc,9917);
            ShowWindow(parent.value,SW_HIDE);
        }
        {
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"prosa",0,"bold_italic"}}};
            set_rich_text(editor.value,"Una prosa con `std::cout`.",emphasis);
            check(character(editor.value,0).yHeight==270
                && std::wstring(character(editor.value,0).szFaceName)==L"Roboto",
                "Lesson prose does not use the Android-derived native 18sp base");
            const auto emphasized=character(editor.value,4),technical=character(editor.value,14);
            check(emphasized.yHeight==270&&(emphasized.dwEffects&CFE_BOLD)&&(emphasized.dwEffects&CFE_ITALIC)
                && technical.yHeight==270&&std::wstring(technical.szFaceName)==reader_theme::code_font_face(),
                "Readable body typography flattened emphasis or changed the inline code face/size");
            set_rich_text(editor.value,"Una pregunta",{},false,RichTextRole::heading);
            check(character(editor.value,0).yHeight==345&&(character(editor.value,0).dwEffects&CFE_BOLD),
                "Headings and questions lost the 23sp bold hierarchy");
            set_rich_text(editor.value,"Una pregunta matemática",{},false,RichTextRole::prompt);
            check(character(editor.value,0).yHeight==270&&(character(editor.value,0).dwEffects&CFE_BOLD),
                "Mathematical prompts must be bold without increasing their 18sp body size");
            set_rich_text(editor.value,"Título de lección",{},false,RichTextRole::lesson_title);
            check(character(editor.value,0).yHeight==480&&(character(editor.value,0).dwEffects&CFE_BOLD),
                "The 32sp lesson title was flattened into an ordinary heading");
            set_rich_text(editor.value,"std::cout << 2;",{},true);
            check(character(editor.value,0).yHeight==270&&std::wstring(character(editor.value,0).szFaceName)==reader_theme::code_font_face(),
                "Displayed code is still smaller than the original source editor");
            SendMessageW(editor.value,EM_SETZOOM,150,100);
            const int dpi=static_cast<int>(GetDpiForWindow(editor.value));
            POINTL text_at{},space_at{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&text_at),1);
            SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_at),9);
            CHARRANGE preserved{2,4};SendMessageW(editor.value,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&preserved));
            check(rich_text_hit_test(editor.value,{text_at.x+1,text_at.y+MulDiv(27,dpi,96)})
                &&!rich_text_hit_test(editor.value,{space_at.x+1,space_at.y+MulDiv(27,dpi,96)}),
                "Actual font/zoom geometry confuses enlarged glyphs or real whitespace with a fixed-height gesture area");
            CHARRANGE after_hit{};SendMessageW(editor.value,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after_hit));
            check(after_hit.cpMin==preserved.cpMin&&after_hit.cpMax==preserved.cpMax,"Pure text hit testing changed the source selection");
            SendMessageW(editor.value,EM_SETZOOM,0,0);
        }
        {
            reader_theme::apply(editor.value,reader_theme::Surface::canvas);
            const auto base=reader_theme::foreground(editor.value);
            const auto string_ink=reader_theme::palette(editor.value).high_contrast?base:RGB(122,195,141);
            const auto check_ink=[&](std::wstring_view token,COLORREF ink){
                const auto visible=wide(edit_text(editor.value));const auto begin=visible.find(token);
                check(begin!=visible.npos,"The inline-string regression lost its original text");
                for(auto index=begin;index<begin+token.size();++index)
                    check(character(editor.value,static_cast<LONG>(index)).crTextColor==ink,
                        "An inline literal lost Android's complete string color or leaked into ordinary prose");
            };
            const std::string screenshot=R"text(𐐀 Un espacio dentro de un literal sí es parte del mensaje. En std::cout << "Sala" << " " << "Central";, las inserciones aportan Sala, un espacio y Central.)text";
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"Un espacio dentro de un literal",0,"bold"}}};
            set_rich_text(editor.value,screenshot,emphasis);
            check(edit_text(editor.value)==screenshot,"Coloring inline strings changed source text or narration offsets");
            for(const auto token:{LR"("Sala")",LR"(" ")",LR"("Central")"})check_ink(token,string_ink);
            check_ink(L"las inserciones aportan Sala",base);
            const auto literal_at=static_cast<LONG>(wide(screenshot).find(L"Central\""));
            const auto literal_style=character(editor.value,literal_at);
            check(literal_style.yHeight==270&&std::wstring(literal_style.szFaceName)==L"Roboto"&&
                (character(editor.value,static_cast<LONG>(wide(screenshot).find(L"Un espacio"))).dwEffects&CFE_BOLD),
                "Inline color correction changed the literal's typography or an existing emphasis range");
            const std::string escaped=R"text(En std::cout << "std::cout << \"Hola\"; int // sigue" << '\n';, después "texto normal".)text";
            set_rich_text(editor.value,escaped);
            check(edit_text(editor.value)==escaped,"Inline escaped strings changed their source characters");
            check_ink(LR"text("std::cout << \"Hola\"; int // sigue")text",string_ink);
            check_ink(LR"('\n')",string_ink);check_ink(LR"("texto normal")",base);
            set_rich_text(editor.value,R"text(En std::cout << escoger(y, "Hola", '\n');, sigue la explicación.)text");
            check_ink(LR"("Hola")",string_ink);check_ink(LR"('\n')",string_ink);
            for(const auto prose:{R"text(La frase "Hola" es prosa; std::cout representa el recurso.)text",
                R"text(Se menciona std::cout << "Hola" y luego otra frase; conserva su presentación.)text",
                "Se menciona std::cout << \"Hola\"\nOtra línea termina;",
                "Se menciona std::cout << \"Hola sin cerrar; continúa la frase."}){
                set_rich_text(editor.value,prose);check_ink(L"Hola",base);
            }
        }
        {
            reader_theme::apply(editor.value,reader_theme::Surface::canvas);
            const auto base=reader_theme::foreground(editor.value);
            const bool high_contrast=reader_theme::palette(editor.value).high_contrast;
            const auto identifier_ink=high_contrast?base:RGB(171,141,227);
            const auto string_ink=high_contrast?base:RGB(122,195,141);
            const auto literal_ink=high_contrast?base:RGB(224,151,109);
            const auto check_range=[&](std::wstring_view context,std::size_t offset,std::size_t count,COLORREF ink){
                const auto visible=wide(edit_text(editor.value));const auto start=visible.find(context);
                check(start!=visible.npos&&offset+count<=context.size(),"The technical-character regression lost its source context");
                for(auto index=start+offset;index<start+offset+count;++index)
                    check(character(editor.value,static_cast<LONG>(index)).crTextColor==ink,
                        "A technical character lost its Android ink or colored neighboring prose punctuation");
            };
            const std::string lesson=R"text(𐐀 Dentro de un literal, \n representa un salto de línea. Su escritura en el código utiliza una barra inversa y la letra n, pero el texto producido contiene un carácter de control. No se muestran la barra inversa ni la letra n: juntas forman el escape.)text";
            const std::array<lgaed::curriculum::Emphasis,2> emphasis{{
                {"\\n representa un salto de línea",0,"bold"},{"letra n",0,"italic"}}};
            set_rich_text(editor.value,lesson,emphasis);
            check(edit_text(editor.value)==lesson,"Character highlighting changed Unicode text or narration offsets");
            check_range(LR"(\n representa)",0,1,base);check_range(LR"(\n representa)",1,1,identifier_ink);
            check_range(L"la letra n, pero",0,9,base);check_range(L"la letra n, pero",9,1,identifier_ink);
            check_range(L"la letra n, pero",10,6,base);
            check_range(L"la letra n: juntas",9,1,identifier_ink);check_range(L"la letra n: juntas",10,8,base);
            const auto visible=wide(lesson);
            const auto named_style=character(editor.value,static_cast<LONG>(visible.find(L"letra n")+6));
            check(named_style.yHeight==270&&std::wstring(named_style.szFaceName)==L"Roboto"&&(named_style.dwEffects&CFE_ITALIC)
                &&(character(editor.value,static_cast<LONG>(visible.find(L"\\n")+1)).dwEffects&CFE_BOLD),
                "Coloring a named letter changed its typography or authored emphasis");
            for(const auto source:{"letra n","Letra N:","CARÁCTER n.","caracter t,","carácter q)","carácter v?"}){
                set_rich_text(editor.value,source);const auto text=wide(source);const auto letter=text.find(L' ')+1;
                check_range(text,0,letter,base);check_range(text,letter,1,identifier_ink);
                if(letter+1<text.size())check_range(text,letter+1,text.size()-letter-1,base);
            }
            const std::string escapes=R"(\n \t \r \0 \a \b \f \v)";
            set_rich_text(editor.value,escapes);
            for(const auto token:{LR"(\n)",LR"(\t)",LR"(\r)",LR"(\a)",LR"(\b)",LR"(\f)",LR"(\v)"}){
                check_range(token,0,1,base);check_range(token,1,1,identifier_ink);
            }
            check_range(LR"(\0)",0,1,base);check_range(LR"(\0)",1,1,literal_ink);
            const std::string isolated=R"text(Los símbolos \" y \\ son escapes técnicos; se leen por separado.)text";
            set_rich_text(editor.value,isolated);
            check(edit_text(editor.value)==isolated,"Isolated escaped symbols changed the original narration text");
            check_range(LR"(\")",0,2,identifier_ink);
            check_range(LR"(\\)",0,2,identifier_ink);
            check_range(L" son escapes técnicos",0,std::wstring_view(L" son escapes técnicos").size(),base);
            const std::string ordinary_quotes=R"text(En la ruta C:\\carpeta y en "una cita" no hay símbolos escapados aislados.)text";
            set_rich_text(editor.value,ordinary_quotes);
            check_range(wide(ordinary_quotes),0,wide(ordinary_quotes).size(),base);
            const std::string prose=R"text(La letra número, letra nombre, carácter nñ, carácter n2, carácter n_1; la n y una a. \nueva, \tarea, \z y alfa\n; superletra n y áletra n.)text";
            set_rich_text(editor.value,prose);check_range(wide(prose),0,wide(prose).size(),base);
            const std::string statement=R"text(En std::cout << "letra n: \n";, la letra n: sigue.)text";
            set_rich_text(editor.value,statement);
            check_range(LR"("letra n: \n")",0,std::wstring_view(LR"("letra n: \n")").size(),string_ink);
            check_range(L"la letra n: sigue",9,1,identifier_ink);check_range(L"la letra n: sigue",10,7,base);
            set_rich_text(editor.value,R"text(𐐀 `"letra n: \n"` y la letra n: sigue.)text");
            check_range(LR"("letra n: \n")",0,std::wstring_view(LR"("letra n: \n")").size(),string_ink);
            check_range(L"la letra n: sigue",9,1,identifier_ink);check_range(L"la letra n: sigue",10,7,base);
        }
        const std::string original = "#include <iostream>\n\nint main() {\n\tstd::cout << \"área  A\";  \n}\n";
        set_edit_text(editor.value, original);
        check(edit_text(editor.value) == original, "RichEdit did not preserve exact indentation, spaces or final LF");
        set_edit_text(editor.value, "{\\rtf1 This is C++ text, not RTF.}\n");
        check(edit_text(editor.value) == "{\\rtf1 This is C++ text, not RTF.}\n", "Code was interpreted as RTF");
        set_edit_text(editor.value, original);
        PARAFORMAT2 tabs{}; tabs.cbSize = sizeof(tabs); tabs.dwMask = PFM_TABSTOPS;
        tabs.cTabCount = 1; tabs.rgxTabs[0] = 720;
        SendMessageW(editor.value, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&tabs));
        SendMessageW(editor.value, EM_EMPTYUNDOBUFFER, 0, 0);
        CHARRANGE end{-1,-1}; SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
        SendMessageW(editor.value, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
        CHARRANGE before{3,8}; SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
        const auto modified = SendMessageW(editor.value, EM_GETMODIFY, 0, 0);
        style_cpp(editor.value);
        POINTL space_before{},next_before{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_before),8);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next_before),9);
        POINTL first_before{},second_before{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first_before),0);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second_before),1);
        const auto syntax_before=character(editor.value,0);POINT scroll_before{};SendMessageW(editor.value,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll_before));
        SendMessageW(editor.value,EM_SETREADONLY,TRUE,0);
        compact_code_spaces(editor.value);
        check((GetWindowLongPtrW(editor.value,GWL_STYLE)&ES_READONLY)!=0,"Compacting a real readonly code display left it editable");
        POINTL space_after{},next_after{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_after),8);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next_after),9);
        POINTL first_after{},second_after{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&first_after),0);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&second_after),1);
        POINT scroll_after{};SendMessageW(editor.value,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll_after));
        const auto normal_width=next_before.x-space_before.x,compact_width=next_after.x-space_after.x;
        // MSFTEDIT may return tomUndefined for GetScaling even when it draws
        // the requested scale. Assert real advances, not format metadata.
        check(normal_width>compact_width&&std::abs(compact_width-normal_width*.8)<=1&&
            second_before.x-first_before.x==second_after.x-first_after.x&&
            character(editor.value,8).yHeight==syntax_before.yHeight&&first_after.y==first_before.y,
            "Display-only code spaces did not narrow their actual native advance independently of other characters");
        compact_code_spaces(editor.value);
        POINTL space_again{},next_again{};SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&space_again),8);
        SendMessageW(editor.value,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&next_again),9);
        check(next_again.x-space_again.x==compact_width&&character(editor.value,0).crTextColor==syntax_before.crTextColor&&
            character(editor.value,0).yHeight==syntax_before.yHeight&&scroll_after.x==scroll_before.x&&scroll_after.y==scroll_before.y,
            "Compacting code was cumulative or changed syntax, font or scrolling");
        CHARRANGE after{}; SendMessageW(editor.value, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
        check(before.cpMin == after.cpMin && before.cpMax == after.cpMax, "Syntax formatting moved selection");
        check(SendMessageW(editor.value, EM_GETMODIFY, 0, 0) == modified, "Syntax formatting changed modified state");
        PARAFORMAT2 after_tabs{}; after_tabs.cbSize = sizeof(after_tabs);
        SendMessageW(editor.value, EM_GETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&after_tabs));
        check(after_tabs.cTabCount == 1 && after_tabs.rgxTabs[0] == 720, "Syntax formatting replaced the editor's measured tab stops");
        check(edit_text(editor.value) == original + 'X', "Syntax formatting changed source text");
        SendMessageW(editor.value,EM_SETREADONLY,FALSE,0);
        check(SendMessageW(editor.value, EM_UNDO, 0, 0) != 0 && edit_text(editor.value) == original, "Syntax formatting entered the text undo history");
        check(SendMessageW(editor.value, EM_REDO, 0, 0) != 0 && edit_text(editor.value) == original + 'X', "Syntax formatting broke redo history");
        set_rich_text(editor.value, "El `std::cout` conserva su texto.", {}, false);
        check(edit_text(editor.value) == "El std::cout conserva su texto.", "Inline code delimiters were displayed or text was lost");
        ShowWindow(editor.value,SW_HIDE);
        style_cpp(editor.value);
        check(!(GetWindowLongPtrW(editor.value,GWL_STYLE) & WS_VISIBLE),"Syntax formatting revealed a hidden editor");
        set_rich_text(editor.value,"Un párrafo oculto.");
        check(!(GetWindowLongPtrW(editor.value,GWL_STYLE) & WS_VISIBLE),"Rich text formatting revealed a hidden paragraph");
        check(utf8(wide("Acentos áéíóú y carácter 𐐀")) == "Acentos áéíóú y carácter 𐐀", "UTF-8/UTF-16 conversion lost Unicode");
        {
            const auto colors = reader_theme::palette();
            HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
            const bool system_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
                && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
            check(colors.high_contrast == system_contrast, "Theme ignored Windows high contrast");
            if (system_contrast) {
                check(colors.canvas == GetSysColor(COLOR_WINDOW) && colors.text == GetSysColor(COLOR_WINDOWTEXT),
                    "High-contrast theme replaced the user's system colors");
            } else {
                check(colors.canvas == RGB(16,16,16) && colors.header == RGB(23,23,23)
                    && colors.card == RGB(28,28,28) && colors.field == RGB(37,37,37)
                    && colors.text == RGB(218,221,228) && colors.accent == RGB(89,240,252),
                    "Reader theme diverged from the Android reference presentation");
            }
            {
                reader_theme::prepare_fonts();const auto dc=GetDC(parent.value);
                std::array<LONG,2> actual_weights{};
                for(std::size_t index=0;index<actual_weights.size();++index){
                    const auto font=CreateFontW(-20,0,0,0,index?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,
                        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
                    check(font!=nullptr,"Create the actual Android-equivalent font faces");const auto previous=SelectObject(dc,font);
                    TEXTMETRICW measured{};check(GetTextMetricsW(dc,&measured)!=FALSE,"Inspect the selected font's real weight");
                    actual_weights[index]=measured.tmWeight;SelectObject(dc,previous);DeleteObject(font);
                }
                // GetFontData proves which exact private font GDI selected,
                // rather than merely trusting the requested family name.
                for(const bool italic:{false,true}){
                    const auto font=CreateFontW(-24,0,0,0,FW_NORMAL,italic,FALSE,FALSE,
                        DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,reader_theme::math_font_face());
                    check(font!=nullptr,"Cannot create the original Android mathematical serif face");const auto previous=SelectObject(dc,font);
                    wchar_t face[LF_FACESIZE]{};check(GetTextFaceW(dc,LF_FACESIZE,face)>0&&std::wstring_view(face)==L"Noto Serif",
                        "Mathematical text silently fell back from the original Noto Serif family");
                    TEXTMETRICW metrics{};check(GetTextMetricsW(dc,&metrics)&&metrics.tmHeight-metrics.tmInternalLeading==24
                        &&static_cast<bool>(metrics.tmItalic)==italic,"Mathematical font changed its requested size or italic face");
                    const auto size=GetFontData(dc,0,0,nullptr,0);check(size==(italic?249748u:246740u),"Mathematical font does not have the reference Android font size");
                    std::vector<std::uint8_t> bytes(size);check(GetFontData(dc,0,0,bytes.data(),size)==size,"Cannot inspect actual selected mathematical font data");
                    check(lgaed::state::sha256(bytes)==(italic?"bb2ad30a8b95b367ed9576cea42bc085b7e69a6f81e6fe53ac3db6f10c7ff5f3":"1e65d9bbb6864fd4a32b96a8f0de3739b667c9eac793ec2d6d00bab7adb5a965"),
                        "The selected mathematical font is not byte-identical to the official OFL Android reference");
                    SelectObject(dc,previous);DeleteObject(font);
                }
                {
                    const auto font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                        DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,reader_theme::code_font_face());
                    check(font!=nullptr,"Cannot create Android's original monospace face");const auto previous=SelectObject(dc,font);
                    wchar_t face[LF_FACESIZE]{};check(GetTextFaceW(dc,LF_FACESIZE,face)>0&&std::wstring_view(face)==L"Droid Sans Mono",
                        "Technical text silently fell back from the original Droid Sans Mono family");
                    TEXTMETRICW metrics{};check(GetTextMetricsW(dc,&metrics)&&metrics.tmHeight-metrics.tmInternalLeading==24,
                        "The original monospace font changed its requested character size");
                    const auto size=GetFontData(dc,0,0,nullptr,0);check(size==108128u,"The selected code font has a different size from Android's reference");
                    std::vector<std::uint8_t> bytes(size);check(GetFontData(dc,0,0,bytes.data(),size)==size,"Cannot inspect the actual selected code font");
                    check(lgaed::state::sha256(bytes)=="db19a1fdaba41cc4a2fec0330e5c15e71c6dd68a3ef074f4f28268828b45c862",
                        "The selected code font is not byte-identical to the Apache-2.0 AOSP/LDPlayer reference");
                    std::array<SIZE,4> widths{};const std::wstring glyphs=L"iW 0";
                    for(std::size_t i=0;i<widths.size();++i)check(GetTextExtentPoint32W(dc,glyphs.data()+i,1,&widths[i])!=FALSE,"Cannot measure the code font's advances");
                    check(std::ranges::all_of(widths,[&](const auto& value){return value.cx==widths[0].cx;}),
                        "The original code font lost fixed-width glyph/space advances");
                    SelectObject(dc,previous);DeleteObject(font);
                }
                ReleaseDC(parent.value,dc);
                check(actual_weights[0]==FW_NORMAL&&actual_weights[1]>=FW_BOLD,
                    "A Windows Roboto registration resolves ordinary body text to the Bold face");
            }
            set_edit_text(editor.value, original);
            SendMessageW(editor.value, EM_EMPTYUNDOBUFFER, 0, 0);
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
            SendMessageW(editor.value, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&before));
            const auto mask = SendMessageW(editor.value, EM_GETEVENTMASK, 0, 0);
            const auto previous_modify = SendMessageW(editor.value, EM_GETMODIFY, 0, 0);
            reader_theme::apply(editor.value, reader_theme::Surface::field);
            style_cpp(editor.value);
            SendMessageW(editor.value, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
            check(before.cpMin == after.cpMin && before.cpMax == after.cpMax,
                "Local theme/syntax moved the code selection");
            check(mask == SendMessageW(editor.value, EM_GETEVENTMASK, 0, 0)
                && previous_modify == SendMessageW(editor.value, EM_GETMODIFY, 0, 0),
                "Local theme/syntax changed the editor event or modified state");
            check(reader_theme::background(editor.value) == colors.field && edit_text(editor.value) == original + 'X',
                "Local field theme changed the original code");
            check(character(editor.value, 0).crTextColor == (system_contrast ? colors.text : RGB(92,156,231)),
                "Themed syntax lost the Android preprocessor color or system contrast");
            const auto quote = static_cast<LONG>(wide(original).find(L'"'));
            check(character(editor.value, quote).crTextColor == (system_contrast ? colors.text : RGB(122,195,141)),
                "Themed syntax lost the original string color");
            check(character(editor.value, static_cast<LONG>(wide(original).find(L'}'))).crTextColor == colors.text,
                "Themed syntax left uncolored punctuation unreadable");
            check(SendMessageW(editor.value, EM_UNDO, 0, 0) != 0 && edit_text(editor.value) == original,
                "Theming entered the native Undo history");
            check(SendMessageW(editor.value, EM_REDO, 0, 0) != 0 && edit_text(editor.value) == original + 'X',
                "Theming broke native Redo");
            const auto technical = static_cast<LONG>(wide(original).find(L"std"));
            CHARRANGE underlined{technical,technical + 3};
            SendMessageW(editor.value, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&underlined));
            CHARFORMAT2W underline{}; underline.cbSize = sizeof(underline);
            underline.dwMask = CFM_UNDERLINE; underline.dwEffects = CFE_UNDERLINE;
            SendMessageW(editor.value, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&underline));
            style_cpp(editor.value);
            check((character(editor.value,technical).dwEffects & CFE_UNDERLINE) != 0,
                "Themed syntax removed an existing underline span");

            Window text{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE | ES_READONLY,
                0, 0, 500, 120, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(text.value != nullptr, "Cannot create themed prose control");
            reader_theme::apply(text.value, reader_theme::Surface::canvas);
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"valor",0,"bold_italic"}}};
            set_rich_text(text.value, "Álgebra con `std::cout` y valor.", emphasis);
            check(edit_text(text.value) == "Álgebra con std::cout y valor.", "Theming altered Unicode or displayed inline delimiters");
            check(character(text.value, 0).crTextColor == colors.text, "Themed prose kept an unreadable system foreground");
            const auto prose = wide(edit_text(text.value));
            check(std::wstring(character(text.value, static_cast<LONG>(prose.find(L"std"))).szFaceName) == reader_theme::code_font_face(),
                "Theming removed the technical fragment's monospace span");
            const auto value_style = character(text.value, static_cast<LONG>(prose.find(L"valor")));
            check((value_style.dwEffects & (CFE_BOLD | CFE_ITALIC)) == (CFE_BOLD | CFE_ITALIC),
                "Theming erased authored bold/italic emphasis");

            Window untouched{CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | ES_MULTILINE,
                0, 0, 200, 80, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(untouched.value != nullptr, "Cannot create original palette control");
            set_rich_text(untouched.value, "Original system presentation");
            check(!reader_theme::applied(untouched.value)
                && character(untouched.value, 0).crTextColor == GetSysColor(COLOR_WINDOWTEXT),
                "A themed HWND changed unrelated controls");
            const auto dc = GetDC(parent.value); LRESULT result = 0;
            check(!reader_theme::control_color(WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                reinterpret_cast<LPARAM>(untouched.value), result), "Theme intercepted an unrelated control");
            check(reader_theme::control_color(WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc),
                reinterpret_cast<LPARAM>(editor.value), result)
                && result == reinterpret_cast<LRESULT>(reader_theme::brush(reader_theme::Surface::field)),
                "Themed field did not use its local brush");
            ReleaseDC(parent.value, dc);

            for(const auto& [surface,expected]:std::array<std::pair<reader_theme::Surface,COLORREF>,3>{{
                {reader_theme::Surface::compiler_header,colors.high_contrast?colors.header:RGB(37,40,46)},
                {reader_theme::Surface::compiler_editor,colors.high_contrast?colors.field:RGB(28,31,36)},
                {reader_theme::Surface::compiler_output,colors.high_contrast?colors.field:RGB(5,6,7)}}}){
                reader_theme::apply(untouched.value,surface);
                check(reader_theme::background(untouched.value)==expected,"Compiler surface lost its exact Android fill or high-contrast fallback");
                LOGBRUSH brush{};check(GetObjectW(reader_theme::brush(surface),sizeof(brush),&brush)==static_cast<int>(sizeof(brush))
                    &&brush.lbColor==expected,"Compiler surface brush uses a stale surface index");
            }

            Window button{CreateWindowExW(0, L"BUTTON", L"&Comprobar", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 130, 36, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            Window radio{CreateWindowExW(0, L"BUTTON", L"Una opción", WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON,
                0, 40, 130, 36, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
            check(button.value && radio.value, "Cannot create real themed buttons");
            const auto focus = GetFocus();
            const auto radio_dialog_code = SendMessageW(radio.value,WM_GETDLGCODE,0,0);
            reader_theme::apply(button.value, reader_theme::Surface::card);
            reader_theme::apply(radio.value, reader_theme::Surface::card);
            check((GetWindowLongPtrW(button.value,GWL_STYLE) & (BS_TYPEMASK | WS_TABSTOP)) == (BS_OWNERDRAW | WS_TABSTOP),
                "Theming lost button Tab navigation or failed to draw the push button");
            check((GetWindowLongPtrW(radio.value,GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON,
                "Theming converted a radio button into a push button");
            check(SendMessageW(radio.value,WM_GETDLGCODE,0,0) == radio_dialog_code
                && (radio_dialog_code & DLGC_RADIOBUTTON) != 0,
                "Radio theming changed native dialog-key handling or its role");
            wchar_t caption[64]{}; GetWindowTextW(button.value,caption,64);
            check(std::wstring(caption) == L"&Comprobar" && GetFocus() == focus, "Theming changed button text or moved keyboard focus");
            const auto screen = GetDC(parent.value); const auto memory = CreateCompatibleDC(screen);
            const auto bitmap = CreateCompatibleBitmap(screen,130,36); const auto previous_bitmap = SelectObject(memory,bitmap);
            DRAWITEMSTRUCT item{}; item.CtlType = ODT_BUTTON; item.hwndItem = button.value;
            item.hDC = memory; item.rcItem = {0,0,130,36};
            for (const bool primary : {false,true}) {
                reader_theme::primary_button(button.value,primary);
                for (const UINT state : std::array<UINT,5>{0,ODS_SELECTED,ODS_DISABLED,ODS_FOCUS,ODS_FOCUS | ODS_NOFOCUSRECT}) {
                    item.itemState = state;
                    check(reader_theme::draw_button(item), "Themed button could not draw a normal, pressed, disabled or focused state");
                }
            }
            if(!colors.high_contrast){
                reader_theme::icon_button(button.value);reader_theme::apply(button.value,reader_theme::Surface::header);item.itemState=ODS_FOCUS;
                check(reader_theme::draw_button(item)&&GetPixel(memory,1,18)==colors.header&&GetPixel(memory,1,1)==colors.header,
                    "A header icon still paints a canvas-colored rectangular tile over its own surface");
                reader_theme::apply(button.value,reader_theme::Surface::card);
                SetWindowTextW(button.value,L"");reader_theme::icon_button(button.value,false);
                reader_theme::premium_button(button.value);item.itemState=0;
                check(reader_theme::draw_button(item),"The Premium action could not be rendered");
                const auto first=GetPixel(memory,16,18),middle=GetPixel(memory,65,18),last=GetPixel(memory,114,18);
                check(first!=middle&&middle!=last&&first!=last&&GetRValue(middle)>GetRValue(first)&&GetRValue(middle)>GetRValue(last),
                    "Premium lost Android's true three-stop gold gradient");
                item.itemState=ODS_DISABLED;check(reader_theme::draw_button(item)&&GetPixel(memory,16,18)==first
                    &&GetPixel(memory,65,18)==middle&&GetPixel(memory,114,18)==last,
                    "The non-clickable Premium activated status lost its Android gold drawable");
                reader_theme::premium_button(button.value,false);SetWindowTextW(button.value,L"&Comprobar");
            }
            item.hwndItem = radio.value;
            check(!reader_theme::draw_button(item), "Theme intercepted native radio-button drawing");
            const auto radio_dpi = GetDpiForWindow(radio.value);
            const auto glyph_left = MulDiv(3,static_cast<int>(radio_dpi ? radio_dpi : 96),96);
            const auto glyph_diameter = std::min(MulDiv(20,static_cast<int>(radio_dpi ? radio_dpi : 96),96),36-glyph_left*2);
            const auto glyph_center_x = glyph_left + glyph_diameter / 2;
            const auto glyph_center_y = 36 / 2;
            const auto raster = [&] {
                SendMessageW(radio.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            };
            const auto glyph_pixels = [&](COLORREF ink) {
                unsigned count = 0;
                for (int y = 0; y < 36; ++y)
                    for (int x = 0; x < std::min(130,glyph_left + glyph_diameter + 2); ++x)
                        if (GetPixel(memory,x,y) == ink) ++count;
                return count;
            };
            SendMessageW(radio.value,BM_SETCHECK,BST_UNCHECKED,0); raster();
            const auto unchecked_ink=colors.high_contrast?colors.muted:RGB(120,124,134);
            if(glyph_pixels(unchecked_ink)<24||GetPixel(memory,glyph_center_x,glyph_center_y)!=colors.card)
                std::cerr<<"Radio paint diagnostic: dpi="<<radio_dpi<<" diameter="<<glyph_diameter<<" left="<<glyph_left
                    <<" center=("<<glyph_center_x<<','<<glyph_center_y<<") actual="<<GetPixel(memory,glyph_center_x,glyph_center_y)
                    <<" background="<<colors.card<<" exact-edge="<<glyph_pixels(unchecked_ink)<<'\n';
            check(glyph_pixels(unchecked_ink) >= 24 && GetPixel(memory,glyph_center_x,glyph_center_y) == colors.card,
                "Unchecked radio did not draw the Android 20-DIP outline and empty center");
            if(!colors.high_contrast){unsigned blended=0;
                for(int y=0;y<36;++y)for(int x=0;x<glyph_left+glyph_diameter;++x){const auto value=GetPixel(memory,x,y);
                    if(value!=colors.card&&value!=unchecked_ink)++blended;}
                check(blended>=12,"The radio outline is still a jagged, unfiltered pixel circle");}
            unsigned caption_pixels = 0;
            for (int y = 0; y < 36; ++y) for (int x = glyph_left + glyph_diameter + 4; x < 130; ++x)
                if (GetPixel(memory,x,y) != colors.card) ++caption_pixels;
            check(caption_pixels >= 12,"Radio theming hid the original native caption");
            SendMessageW(radio.value,BM_SETCHECK,BST_CHECKED,0); raster();
            check(SendMessageW(radio.value,BM_GETCHECK,0,0) == BST_CHECKED
                && GetPixel(memory,glyph_center_x,glyph_center_y) == colors.accent
                && glyph_pixels(colors.accent) >= 32,"Selected radio did not preserve native state and draw its accent center");
            EnableWindow(radio.value,FALSE); raster();
            const auto disabled_ink = colors.high_contrast ? GetSysColor(COLOR_GRAYTEXT) : colors.accent;
            check(GetPixel(memory,glyph_center_x,glyph_center_y) == disabled_ink,
                "A correct/disabled selected radio lost its route accent instead of matching Android");
            EnableWindow(radio.value,TRUE);
            SendMessageW(radio.value,BM_SETCHECK,BST_CHECKED,0);
            check(SendMessageW(radio.value,BM_GETCHECK,0,0) == BST_CHECKED,
                "Radio theming broke native checked-state persistence");
            GetWindowTextW(radio.value,caption,64);
            check(std::wstring(caption) == L"Una opción"
                && (GetWindowLongPtrW(radio.value,GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON,
                "Radio drawing changed the original caption or native control type");
            Window checkbox{CreateWindowExW(0,L"BUTTON",L"Activar lectura",WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
                0,80,130,36,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(checkbox.value != nullptr,"Cannot create a real automatic checkbox");
            const auto checkbox_dialog=SendMessageW(checkbox.value,WM_GETDLGCODE,0,0);reader_theme::apply(checkbox.value,reader_theme::Surface::card);
            SendMessageW(checkbox.value,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(checkbox.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            check(GetPixel(memory,glyph_center_x,glyph_center_y)==colors.card&&glyph_pixels(colors.high_contrast?colors.muted:colors.accent)>=24,
                "The native checkbox has no empty, clearly drawn square glyph");
            SendMessageW(checkbox.value,BM_CLICK,0,0);SendMessageW(checkbox.value,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
            check(SendMessageW(checkbox.value,BM_GETCHECK,0,0)==BST_CHECKED&&glyph_pixels(colors.accent)>=32,
                "The checkbox lost native automatic state or its visible checked square");
            check((GetWindowLongPtrW(checkbox.value,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX
                && SendMessageW(checkbox.value,WM_GETDLGCODE,0,0)==checkbox_dialog,"Checkbox painting changed its native type or keyboard dialog behavior");
            GetWindowTextW(checkbox.value,caption,64);check(std::wstring(caption)==L"Activar lectura","Checkbox painting replaced its accessible native caption");
            SelectObject(memory,previous_bitmap); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(parent.value,screen);
        }
        {
            lgaed::state::StateData preferences;
            check(reader_theme::appearance_accent(preferences) == RGB(89,240,252)
                && reader_theme::appearance_accent(preferences,"programming") == RGB(228,50,72)
                && reader_theme::appearance_accent(preferences,"math_physics") == RGB(108,209,92),
                "Appearance defaults differ from the original Android repository");
            auto& appearance = preferences.preferences["appearance_settings"];
            appearance["accent_red"] = std::int64_t{10}; appearance["accent_green"] = std::int64_t{20}; appearance["accent_blue"] = std::int64_t{30};
            appearance["route_accent_green_programming"] = std::int64_t{88};
            const auto unchanged = preferences;
            check(reader_theme::appearance_accent(preferences) == RGB(10,20,30)
                && reader_theme::appearance_accent(preferences,"programming") == RGB(228,88,72)
                && reader_theme::appearance_accent(preferences,"math_physics") == RGB(108,209,92)
                && reader_theme::appearance_accent(preferences,"other") == RGB(10,20,30) && preferences == unchanged,
                "Reading local appearance lost original per-component overrides, changed another route or mutated preferences");
            Window branch{CreateWindowExW(0,L"STATIC",L"Scoped theme QA",WS_CHILD,0,0,500,200,parent.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            Window formula{CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD | ES_MULTILINE,0,0,450,160,branch.value,nullptr,GetModuleHandleW(nullptr),nullptr)};
            Window other{CreateWindowExW(0,L"STATIC",L"Unrelated theme QA",WS_OVERLAPPEDWINDOW,0,0,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
            check(branch.value && formula.value && other.value,"Cannot create isolated per-window theme controls");
            reader_theme::apply(formula.value,reader_theme::Surface::card);
            const std::string source="Negrita con `std::cout` y 6 × 100 = 600.";
            const std::array<lgaed::curriculum::Emphasis,1> emphasis{{{"Negrita",0,"bold_italic"}}};
            set_rich_text(formula.value,source,emphasis); const auto displayed = edit_text(formula.value);
            std::vector<CHARFORMAT2W> original_ink;
            for (std::size_t index=0;index<wide(displayed).size();++index) original_ink.push_back(character(formula.value,static_cast<LONG>(index)));
            const auto events=SendMessageW(formula.value,EM_GETEVENTMASK,0,0),original_modified=SendMessageW(formula.value,EM_GETMODIFY,0,0);
            reader_theme::set_accent(parent.value,reader_theme::appearance_accent(preferences,"programming"));
            reader_theme::set_accent(branch.value,reader_theme::appearance_accent(preferences,"math_physics"));
            const auto system = reader_theme::palette();
            check(reader_theme::palette(editor.value).accent == (system.high_contrast ? system.accent : RGB(228,88,72))
                && reader_theme::palette(formula.value).accent == (system.high_contrast ? system.accent : RGB(108,209,92))
                && reader_theme::palette(other.value).accent == system.accent && reader_theme::palette().accent == system.accent,
                "A local route accent leaked globally, ignored its nearest parent or replaced high-contrast system colors");
            reader_theme::set_accent(branch.value,RGB(0,0,0));
            check(reader_theme::palette(formula.value).accent == (system.high_contrast ? system.accent : RGB(0,0,0)),"A valid black accent was confused with an absent property");
            check(reader_theme::accent_foreground(formula.value)==(system.high_contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(255,255,255)),"A black primary accent lost Android's white enabled Button foreground");
            for(const auto surface:{system.canvas,system.header,system.card,system.field})
                check(reader_theme::accent_ink(formula.value,surface)==system.text,"A black accent disappeared as icon/text ink on a real themed surface");
            check(reader_theme::palette(formula.value).accent==(system.high_contrast?system.accent:RGB(0,0,0))&&preferences==unchanged,
                "Contrast-safe ink rewrote a valid configured RGB or stored preference");
            for(const auto normal:{RGB(89,240,252),RGB(228,50,72),RGB(108,209,92)}){
                reader_theme::set_accent(branch.value,normal);
                for(const auto surface:{system.canvas,system.header,system.card,system.field})
                    check(reader_theme::accent_ink(formula.value,surface)==(system.high_contrast?GetSysColor(COLOR_WINDOWTEXT):normal),
                        "Contrast-safe accent ink changed an original Android accent or ignored actual system high contrast");
            }
            reader_theme::set_accent(branch.value,RGB(255,255,255));
            check(reader_theme::accent_foreground(formula.value)==(system.high_contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(0,0,0)),"A white primary accent lost Android's black enabled Button foreground");
            reader_theme::set_accent(branch.value,std::nullopt);
            check(reader_theme::palette(formula.value).accent == (system.high_contrast ? system.accent : RGB(228,88,72)),"Removing a local accent did not restore parent inheritance");
            for (std::size_t index=0;index<original_ink.size();++index) {
                const auto current_format=character(formula.value,static_cast<LONG>(index));const auto& original_format=original_ink[index];
                check(original_format.crTextColor == current_format.crTextColor && original_format.dwEffects == current_format.dwEffects && original_format.yHeight == current_format.yHeight
                    && std::wstring_view(original_format.szFaceName) == current_format.szFaceName,"Changing a route accent recolored syntax/formulas or altered an original emphasis span");
            }
            check(edit_text(formula.value) == displayed && events == SendMessageW(formula.value,EM_GETEVENTMASK,0,0)
                && original_modified == SendMessageW(formula.value,EM_GETMODIFY,0,0),"Local appearance changed original text, edit events or modified state");
            reader_theme::set_accent(parent.value,std::nullopt);
        }
        std::cout << "Real RichEdit: exact source/LF, RTF safety, selection/Undo/Redo, inline spans, local Android theme and native contrast/buttons: PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; FreeLibrary(module); return 1;
    }
    FreeLibrary(module); return 0;
}
