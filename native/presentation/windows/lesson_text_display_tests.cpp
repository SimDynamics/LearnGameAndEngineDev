#include "presentation/windows/order_code_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lgaed::presentation;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
COLORREF ink(HWND row,LONG index){
    CHARRANGE old{},part{index,index+1};SendMessageW(row,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&old));
    SendMessageW(row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&part));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(row,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    SendMessageW(row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&old));return format.crTextColor;
}
void colored(HWND row,std::wstring_view part,COLORREF color){
    const auto text=wide(edit_text(row));const auto start=text.find(part);
    require(start!=std::wstring::npos,"Missing tested text");
    for(std::size_t index=start;index<start+part.size();++index)
        require(ink(row,static_cast<LONG>(index))==color,"Wrong color at character "+std::to_string(index));
}
void string_prose(HWND host){
    const auto library=LoadLibraryW(L"Msftedit.dll");require(library!=nullptr,"Missing RichEdit");
    const auto row=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_READONLY|ES_MULTILINE,
        0,0,850,300,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(row!=nullptr,"Cannot create rich text probe");reader_theme::apply(row,reader_theme::Surface::canvas);
    const auto base=reader_theme::foreground(row),green=RGB(122,195,141);
    const std::string page6="Después de responder, contrasta los papeles de las piezas: << realiza la inserción y el literal \"Carga lista\" aporta el mensaje. Las comillas delimitan el texto y no aparecen como parte de él (Stroustrup, 2024).";
    set_rich_text(row,page6);require(edit_text(row)==page6,"Page 6 source was rewritten");
    colored(row,L"\"Carga lista\"",green);colored(row,L" aporta el mensaje.",base);
    set_rich_text(row,"Una cita normal dice \"Carga lista\" y sigue aquí.");colored(row,L"\"Carga lista\"",base);
    set_rich_text(row,R"txt(El literal "std::cout // << \"sí\"" aporta el mensaje.)txt");
    colored(row,LR"txt("std::cout // << \"sí\"")txt",green);colored(row,L" aporta el mensaje.",base);
    set_rich_text(row,"Un literal exacto \"\" representa una cadena vacía.");colored(row,L"\"\"",green);
    set_rich_text(row,"El literal \"sin cerrar no debe colorear todo el párrafo.");colored(row,L"sin cerrar",base);
    set_rich_text(row,"La salida es `Carga lista`, con un espacio.");colored(row,L"Carga lista",green);colored(row,L", con un espacio.",base);
    DestroyWindow(row);FreeLibrary(library);
    std::cout<<"PASS explicit string prose: page 6, quoted operators/escapes, empty/incomplete literals, ordinary quotes, output\n";
}
void ordered_text(HWND host){
    const std::vector<std::string> source={"#include <iostream>\nint main() {",
        "    std::cout << \"Paso uno\\n\";", "    std::cout << \"Paso dos\";", "    return 0;\n}",
        "    std::cout << \"Ágjpq texto largo para verificar el ajuste de línea y los márgenes\";"};
    OrderCodeView view(host,source,{0,1,2,3,4},[]{});
    std::size_t inspected=0;
    for(const int scale:{75,100,125,150,200})for(const bool wrap:{false,true})for(const int width:{320,900}){
        view.set_text_scale(scale);view.set_wrap(wrap);
        const auto height=view.height_for_width(width);MoveWindow(view.window(),0,0,width,height,TRUE);
        (void)view.height_for_width(width);
        for(std::size_t item=0;item<source.size();++item){
            const auto row=GetDlgItem(view.window(),static_cast<int>(100+item));require(row!=nullptr,"Missing original order row");
            require(edit_text(row)==source[item],"Layout changed source text");
            Microsoft::WRL::ComPtr<IRichEditOle> ole;
            SendMessageW(row,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(ole.GetAddressOf()));
            Microsoft::WRL::ComPtr<ITextDocument> document;require(ole&&SUCCEEDED(ole.As(&document)),"Cannot inspect real text bounds");
            RECT client{};GetClientRect(row,&client);const auto text=wide(source[item]);
            const int pad=std::max(1,MulDiv(6,static_cast<int>(GetDpiForWindow(row)),96));
            for(std::size_t i=0;i<text.size();++i){
                if(text[i]==L'\n'||text[i]==L'\r')continue;
                Microsoft::WRL::ComPtr<ITextRange> range;
                require(document->Range(static_cast<LONG>(i),static_cast<LONG>(i),&range)==S_OK,"Cannot inspect character");
                LONG left=0,top=0,right=0,bottom=0;
                constexpr LONG flags=tomStart|tomClientCoord|tomAllowOffClient;
                require(range->GetPoint(flags|TA_LEFT|TA_TOP,&left,&top)==S_OK&&
                    range->GetPoint(flags|TA_RIGHT|TA_BOTTOM,&right,&bottom)==S_OK,"Missing native glyph box");
                const auto context=" scale="+std::to_string(scale)+" wrap="+std::to_string(wrap)+" width="+std::to_string(width)+
                    " row="+std::to_string(item)+" character="+std::to_string(i)+" box="+std::to_string(left)+","+std::to_string(top)+","+
                    std::to_string(right)+","+std::to_string(bottom)+" client="+std::to_string(client.right)+","+std::to_string(client.bottom);
                require(left>=1&&top>=pad-1&&right<=client.right-1&&bottom<=client.bottom-pad+1,"Clipped order-card glyph:"+context);
                ++inspected;
            }
        }
    }
    const auto row=GetDlgItem(view.window(),101);colored(row,L"\"Paso uno\\n\"",RGB(122,195,141));
    SendMessageW(row,EM_SETSEL,4,8);view.set_text_scale(125);(void)view.height_for_width(900);
    CHARRANGE selected{};SendMessageW(row,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
    require(selected.cpMin==4&&selected.cpMax==8,"Measurement changed selection");
    const auto before=view.order();view.set_completed(true);view.set_enabled(false);
    require(view.order()==before&&edit_text(row)==source[1],"Completion changed original order/source");
    colored(row,L"\"Paso uno\\n\"",RGB(122,195,141));
    std::cout<<"PASS order geometry: "<<inspected<<" glyph boxes, five scales, two widths, wrap on/off, source/selection/colors/completion\n";
}
}
int main(int argc,char** argv){
    const auto old=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    reader_theme::prepare_fonts();
    const auto host=CreateWindowExW(0,L"STATIC",L"Lesson text QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        0,0,1100,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int result=0;
    try{require(host!=nullptr,"Missing host");
        if(argc<2||std::string(argv[1])=="color")string_prose(host);
        if(argc<2||std::string(argv[1])=="geometry")ordered_text(host);
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    if(host)DestroyWindow(host);OleUninitialize();if(old)SetThreadDpiAwarenessContext(old);return result;
}
