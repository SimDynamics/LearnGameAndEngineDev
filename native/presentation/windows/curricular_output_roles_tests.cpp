#include "presentation/windows/curricular_text_roles.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace lgaed::presentation::technical_prose;
void require(bool value,std::string_view reason) { if(!value)throw std::runtime_error(std::string(reason)); }
const auto unit=std::filesystem::path(build::source_root)/
    "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo";
curriculum::Block block(std::string_view file,std::size_t page,std::size_t index) {
    const auto lesson=windows::load_lesson(unit/std::string(file));
    require(page&&page<=lesson.pages.size(),"Current output-role page disappeared");
    require(index<lesson.pages[page-1].blocks.size(),"Current output-role block disappeared");
    return lesson.pages[page-1].blocks[index];
}
Range locate(std::wstring_view text,std::wstring_view sample) {
    const auto at=text.find(sample);require(at!=text.npos,"The exact public sample disappeared");
    return {at,at+sample.size()};
}
CurricularRoles classify(std::wstring_view text,std::span<const Range> marked={},std::span<const Range> math={}) {
    const auto semantic=analyze(text,"es",marked,math);
    return curricular_roles(text,"es",semantic,{}, {},marked);
}
void contains(std::span<const Range> spans,Range sample,std::string_view reason) {
    require(curricular_role_detail::contained(sample,spans),reason);
}
void absent(std::span<const Range> spans,Range sample,std::string_view reason) {
    require(!overlaps(sample,spans),reason);
}
void choice(std::string_view file,std::size_t page,std::size_t index,std::string_view question,
        std::span<const std::string_view> expected) {
    const auto current=block(file,page,index);
    require(current.type==curriculum::BlockType::multiple_choice,"The public output exercise changed kind");
    require(current.question==question,"The pinned public result question changed");
    require(current.options.size()==expected.size(),"The public output alternatives changed count");
    for(std::size_t n=0;n<expected.size();++n) {
        require(current.options[n]==expected[n],"A pinned public output alternative changed");
        require(rich_text_choice_reference_role(current.question,current.options[n])==RichTextReferenceRole::output,
            std::string(file)+" page "+std::to_string(page)+" block "+std::to_string(index)+
                " option "+std::to_string(n)+": missing whole neutral output role for ["+current.options[n]+"] in ["+current.question+"]");
    }
}
void current_choices() {
    const std::array<std::string_view,3> precision={"F=12.00;S=1.20e+01;G=12","F=12.00;S=1.2e+01;G=12.00","F=12;S=12.00e+00;G=1.2"};
    choice("lesson_precision_y_presentacion_de_double.json",6,5,
        "Para el dato doce, ¿qué cadena respeta fixed con dos decimales, scientific con dos decimales y general con dos cifras significativas?",precision);
    const std::array<std::string_view,3> braces={"pedido {{X}}: 6 unidades; copia={X}","pedido {X}: 6 unidades; copia=X","pedido {}: 6 unidades; copia="};
    choice("lesson_texto_con_campos_y_patrones_literales.json",6,5,
        "La composición exige pedido {CODIGO}: UNIDADES unidades; copia=CODIGO. Con codigo=\"{X}\" y unidades=6, ¿qué cadena resulta?",braces);
    const std::array<std::string_view,3> separators={"envio: 6 cajas","envio {}: 6 cajas; codigo=","envio {0}: 6 cajas; codigo=0"};
    choice("lesson_texto_con_campos_y_patrones_literales.json",9,1,
        "Con código vacío y seis cajas, ¿qué separadores deben conservarse?",separators);
    const std::array<std::string_view,3> hexadecimal={"1A","0X1A","1a"};
    choice("lesson_bases_y_anchuras_en_cada_campo.json",3,4,
        "¿Qué produce {0:X} para el entero veintiséis, sin #?",hexadecimal);
}
void current_paragraphs() {
    auto source=wide(block("lesson_representar_enteros_en_distintas_bases.json",8,2).text);
    auto roles=classify(source);
    for(const auto sample:{L"dec=0;hex=0;oct=0",L"dec=8;hex=8;oct=10",L"dec=15;hex=f;oct=17",L"dec=64;hex=40;oct=100"})
        contains(roles.outputs,locate(source,sample),"A current base-result list item lost its full data role");
    require(roles.outputs.size()==4,"A result-list cue swallowed prose or unrelated values");
    absent(roles.outputs,locate(source,L"Ese programa parcial"),"A list continued into explanatory prose");
    contains(roles.neutral_numbers,locate(source,L"31"),"An unrelated curricular quantity changed numeric role");

    source=wide(block("lesson_ancho_relleno_y_alineacion_de_campos.json",3,3).text);roles=classify(source);
    contains(roles.outputs,locate(source,L"[0007]|[0012]"),"A current zero-padded row was parsed as source integers");
    require(roles.outputs.size()==1,"A single produced row swallowed surrounding prose");

    source=wide(block("lesson_ancho_relleno_y_alineacion_de_campos.json",7,2).text);roles=classify(source);
    for(const auto sample:{L"[Sol   ]|[  4]",L"[Pasillo]|[1000]",L"[      ]|[  0]",L"[Taller]|[  6]"})
        contains(roles.outputs,locate(source,sample),"A current fixed-width result lost spaces or a field");
    require(roles.outputs.size()==4,"A row list promoted ordinary curricular text to output");
    contains(roles.strings,locate(source,L"\"Taller\""),"A separately quoted string lost its existing string role");
    absent(roles.outputs,locate(source,L"\"Taller\""),"A quoted source string inherited a nearby row's output role");
}
void contextual_boundaries() {
    const std::wstring source=LR"(Las instrucciones Cajas=4;Estado=listo; actualizan variables. El literal "Cajas=4;Estado=listo" conserva caracteres. La salida es F=12.00;S=1.20e+01;G=12. La variable total conserva 7 (Stroustrup, 2024).)";
    const std::array<Range,1> marked={locate(source,L"Cajas=4;Estado=listo;")};
    const auto roles=classify(source,marked);
    contains(roles.code,marked.front(),"An explicit source assignment lost technical ownership");
    absent(roles.outputs,marked.front(),"An explicit source assignment became output");
    contains(roles.strings,locate(source,L"\"Cajas=4;Estado=listo\""),"A quoted source literal lost string ownership");
    contains(roles.outputs,locate(source,L"F=12.00;S=1.20e+01;G=12"),"A decimal/exponent result field was truncated");
    absent(roles.outputs,locate(source,L"La variable total"),"A result role leaked into the next sentence");
    absent(roles.code,locate(source,L"2024"),"A citation year acquired technical ownership");
    absent(roles.numbers,locate(source,L"2024"),"A citation year acquired a curricular numeric backdrop");

    const std::wstring arrays=L"Las líneas de código contienen [0007]|[0012]. Cuenta las líneas son dos; no son resultados. La fila del editor es [Sol]|[4].";
    require(classify(arrays).outputs.empty(),"Counting/source/editor prose was mistaken for a result-list cue");
    const std::wstring bounded=L"Sus líneas son N=1, N=2 y N=3. Después se asigna N=4. La cadena \"N=5\" conserva datos.";
    const auto list=classify(bounded);
    require(list.outputs.size()==3,"A comma/conjunction result list did not stay local");
    absent(list.outputs,locate(bounded,L"N=4"),"An output list leaked through a sentence boundary");
    contains(list.strings,locate(bounded,L"\"N=5\""),"A later quoted literal lost its independent string role");

    const std::wstring labels=L"La etiqueta \"Taller\" ocupa seis caracteres. La etiqueta C++ \"salto\" nombra una sentencia. La frase \"sin salir\" es una cita normal.";
    const auto label_roles=classify(labels);
    contains(label_roles.strings,locate(labels,L"\"Taller\""),"A directly named text label lost its string role");
    absent(label_roles.strings,locate(labels,L"\"salto\""),"A C++ statement label was mistaken for a character string");
    absent(label_roles.strings,locate(labels,L"\"sin salir\""),"An ordinary quoted sentence inherited an earlier label's role");

    const std::wstring mathematical=L"La salida es F=12.00;S=1.20e+01;G=12.";
    const std::array<Range,1> math={locate(mathematical,L"F=12.00;S=1.20e+01;G=12")};
    require(classify(mathematical,{},math).outputs.empty(),"Output inference overrode an explicit mathematical range");

    for(const auto sample:{"Porque el formato conserva dos decimales.","Una cadena vacía, porque no hay datos.","std::cout << 12;","int total = 12;","\"F=12.00\""})
        require(rich_text_choice_reference_role("¿Qué cadena resulta?",sample)!=RichTextReferenceRole::output,
            "A source/explanatory/quoted alternative became an unquoted result");
    require(rich_text_choice_reference_role("¿Qué cadena resulta?","\"F=12.00\"")==RichTextReferenceRole::string,
        "A whole quoted chain candidate lost its existing green string role");
    require(rich_text_choice_reference_role("¿Qué asignación actualiza los campos?","F=12.00;S=1.20e+01;G=12")==RichTextReferenceRole::automatic,
        "Sample spelling alone changed a source-assignment question's role");
    require(rich_text_choice_reference_role("¿Qué produce {0:X} para el entero veintiséis, sin #?","return 26;")==RichTextReferenceRole::automatic,
        "A formatting question turned source code into a scalar output");
}
}
int main() {
    try {
        current_choices();current_paragraphs();contextual_boundaries();
        std::cout<<"PASS 12 current formatted-result alternatives, nine current inline rows, and source/string/prose/citation/math boundaries; headless roles only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
