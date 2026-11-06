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
    for(std::size_t index=start;index<start+part.size();++index){
        const auto actual=ink(row,static_cast<LONG>(index));
        if(actual!=color)std::cerr<<"COLOR DIAGNOSTIC source="<<utf8(text)<<" part="<<utf8(part)
            <<" index="<<index<<" expected="<<color<<" actual="<<actual<<'\n';
        require(actual==color,"Wrong color at character "+std::to_string(index));
    }
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
    set_rich_text(row,"La salida es `Carga lista`, con un espacio.");colored(row,L"Carga lista",base);colored(row,L", con un espacio.",base);
    const std::string mixed="El segundo contiene el literal `\"Carga lista\"`, que aporta los caracteres del mensaje.\n\nLa salida es `Carga lista`, con un espacio entre las palabras.";
    set_rich_text(row,mixed);colored(row,L"\"Carga lista\"",green);colored(row,L"La salida es Carga lista",base);
    const auto background_at=[&](std::wstring_view token){
        const auto at=wide(edit_text(row)).find(token);require(at!=std::wstring::npos,"Missing backed data sample");
        CHARRANGE selected{static_cast<LONG>(at),static_cast<LONG>(at+1)};SendMessageW(row,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        CHARFORMAT2W style{};style.cbSize=sizeof(style);SendMessageW(row,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));
        require(!(style.dwEffects&CFE_UNDERLINE),"A data reference gained an underline");
        require(reader_theme::palette(row).high_contrast||(style.dwEffects&CFE_AUTOBACKCOLOR)==0,"A data reference lost its existing neutral backdrop");
        return style.crBackColor;
    };
    require(background_at(L"\"Carga lista\"")==background_at(L"Carga lista, con"),"String/output roles use different neutral backdrops");
    for(const auto option:{"0","Listo0","Listo"}){
        const std::string question="Un programa ejecuta std::cout << \"Listo\"; y después return 0;. ¿Qué salida de texto produce?";
        RichTextContext context;context.prose=question;context.reference_role=rich_text_choice_reference_role(question,option);
        require(context.reference_role==RichTextReferenceRole::output,"A real output distractor lost its public question role");
        set_rich_text(row,option,{},false,RichTextRole::body,"es",{},context);colored(row,wide(option),base);(void)background_at(wide(option));
    }
    const std::string output_question="El programa envía un mensaje. ¿Qué salida de texto produce?";
    for(const auto option:{"¡Hola!","Hola.","Un turno","Una caja","Inicio: 7; fin: 3.","Hola.\nFin: 9","\"Hola.\\nFin: 9\""}){
        RichTextContext context;context.prose=output_question;context.reference_role=rich_text_choice_reference_role(output_question,option);
        require(context.reference_role==RichTextReferenceRole::output,"Punctuation, escapes or a line break removed a displayed output alternative's role");
        set_rich_text(row,option,{},false,RichTextRole::body,"es",{},context);colored(row,wide(option),base);(void)background_at(wide(option));
    }
    const std::string long_output=std::string(160,'A')+".\nFin: 9";
    require(rich_text_choice_reference_role(output_question,long_output)==RichTextReferenceRole::output,"A message's length changed its displayed output role");
    for(const auto prose:{"Porque no se ejecuta la inserción.","El programa imprime el valor anterior.","Se muestra un diagnóstico.","No produce salida.",
            "std::cout << \"Hola.\";","return 0;","int total = 7;"})
        require(rich_text_choice_reference_role(output_question,prose)==RichTextReferenceRole::automatic,
            "A causal explanation or C++ source statement became a displayed output alternative");
    const std::string concat="Dos etiquetas de texto contienen \"04\" y \"2\". ¿Qué produce concatenarlas en ese orden?";
    set_rich_text(row,concat);require(edit_text(row)==concat,"The current concatenation question changed its source");
    colored(row,L"\"04\"",green);colored(row,L"\"2\"",green);
    require(background_at(L"\"04\"")==background_at(L"\"2\""),"The current question's two string samples use different neutral backdrops");
    for(const auto option:{"\"042\"","\"6\""}){
        RichTextContext context;context.prose=concat;context.reference_role=rich_text_choice_reference_role(concat,option);
        require(context.reference_role==RichTextReferenceRole::string,"A real concatenation alternative lost its visible string role");
        set_rich_text(row,option,{},false,RichTextRole::body,"es",{},context);colored(row,wide(option),green);(void)background_at(wide(option));
    }
    require(rich_text_choice_reference_role(concat,"Una cantidad entera de valor 42")==RichTextReferenceRole::automatic,
        "A prose alternative inherited string ink from another option");
    const std::string blocks="La salida del caso inicial es:\n\nModulo 02\nInicio: 7\nAhora: 10\n\nLa variable `al_inicio` conserva el dato. La salida debe ser:\n\nModulo 02\nInicio: 0\nAhora: 2\n\nConserva los espacios.";
    set_rich_text(row,blocks);colored(row,L"Modulo 02\nInicio: 7\nAhora: 10",base);colored(row,L"Modulo 02\nInicio: 0\nAhora: 2",base);
    colored(row,L"al_inicio",RGB(171,141,227));(void)background_at(L"Modulo 02");
    set_rich_text(row,"Un prompt es una salida. Por ejemplo, `Cantidad: ` puede avisar del dato. Es una salida producida por `std::cout`.");
    colored(row,L"Cantidad:",base);
    colored(row,L"std",RGB(171,141,227));colored(row,L"::",RGB(231,155,186));colored(row,L"cout",RGB(171,141,227));
    (void)background_at(L"Cantidad:");
    set_rich_text(row,"La consola muestra una cantidad. La etiqueta C++ `salida:` pertenece al código.");
    colored(row,L"salida",RGB(171,141,227));
    set_rich_text(row,"La cadena \"Sur\" conserva datos. La frase \"sin salir\" es una cita normal.");
    colored(row,L"\"Sur\"",green);colored(row,L"\"sin salir\"",base);
    set_rich_text(row,"El objetivo es:\n\nLeer el apartado\nContinuar con calma\n\nNo es una muestra de consola.");
    require(rich_text_reference_ranges(row).empty(),"A generic objective block was treated as console output");
    for(const auto output:{"C++","std::cout","int"}){
        set_rich_text(row,std::string("La salida es `")+output+"`.");colored(row,wide(output),base);(void)background_at(wide(output));
        set_rich_text(row,std::string("El literal \"")+output+"\" aporta esos caracteres.");colored(row,wide(output),green);(void)background_at(wide(output));
    }
    const std::string composed="Las inserciones de la línea que empieza con `texto << \"Cajas=\"` forman el texto Cajas=4;Estado=listo dentro del flujo. Las cadenas aportan las etiquetas y el texto listo; la inserción del entero `4` aporta el dígito 4. Esa composición todavía se encuentra en memoria.\n\nLa definición `std::string mensaje = texto.str();` inicializa la variable `mensaje` con la cadena obtenida del flujo. La instrucción `std::cout << mensaje;` envía esa cadena a la salida estándar: se ve Cajas=4;Estado=listo, sin salto de línea final. Después, `return 0;` termina `main`.";
    auto composed_display=composed;std::erase(composed_display,'`');
    set_rich_text(row,composed);require(edit_text(row)==composed_display,"Composed-result roles changed the real L4 prose source");
    colored(row,L"Cajas=4;Estado=listo dentro del flujo",base);colored(row,L"se ve Cajas=4;Estado=listo",base);
    colored(row,L"\"Cajas=\"",green);
    const auto number=wide(edit_text(row)).find(L"entero 4")+7;
    require(ink(row,static_cast<LONG>(number))==RGB(224,151,109),"A distinct explicit C++ integer lost its literal category");
    colored(row,L"dígito 4",base);(void)background_at(L"Cajas=4;Estado=listo");
    const std::string composed_question="La función compone primero con 2 y 1 y después con 0 y 0, creando un flujo local en cada llamada. ¿Qué debe contener la segunda cadena?";
    for(const auto sample:{"Validos=2;Rechazados=1Validos=0;Rechazados=0","Validos=0;Rechazados=0"}){
        RichTextContext context;context.prose=composed_question;context.reference_role=rich_text_choice_reference_role(composed_question,sample);
        require(context.reference_role==RichTextReferenceRole::output,"A public composed-chain candidate lost its data/result role");
        set_rich_text(row,sample,{},false,RichTextRole::body,"es",{},context);colored(row,wide(sample),base);(void)background_at(wide(sample));
    }
    require(rich_text_choice_reference_role(composed_question,"Una cadena vacía, porque todos los valores son cero.")==RichTextReferenceRole::automatic,
        "An explanatory option was promoted to a composed-result sample");
    require(rich_text_choice_reference_role("¿Qué asignación actualiza los campos?","Validos=0;Rechazados=0")==RichTextReferenceRole::automatic,
        "A source-assignment question inherited a data/result role from the sample spelling");
    set_rich_text(row,"Las instrucciones `Cajas=4;Estado=listo;` actualizan variables. El literal \"Cajas=4;Estado=listo\" conserva caracteres.");
    colored(row,L"\"Cajas=4;Estado=listo\"",green);
    const auto code_number=wide(edit_text(row)).find(L"Cajas=4")+6;
    require(ink(row,static_cast<LONG>(code_number))==RGB(224,151,109),"A real marked assignment was recolored as composed data");
    DestroyWindow(row);FreeLibrary(library);
    std::cout<<"PASS explicit string/output prose: real mixed U1 feedback, all output alternatives, quoted concatenation alternatives, independent output blocks and prompt labels; complete existing ink/backdrops, no source changes\n";
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
