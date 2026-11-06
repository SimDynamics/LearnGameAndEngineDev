#include "presentation/windows/curricular_text_roles.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

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
Range locate_after(std::wstring_view text,std::wstring_view cue,std::wstring_view sample) {
    const auto cue_at=locate(text,cue).second;const auto at=text.find(sample,cue_at);
    require(at!=text.npos,"The exact locally introduced public sample disappeared");return {at,at+sample.size()};
}
CurricularRoles classify(std::wstring_view text,std::span<const Range> marked={},std::span<const Range> math={}) {
    const auto semantic=analyze(text,"es",marked,math);
    return curricular_roles(text,"es",semantic,{}, {},marked);
}
struct PublicText { std::wstring text;std::vector<Range> marked;CurricularRoles roles; };
PublicText public_text(std::string_view authored) {
    const auto source=wide(authored);const auto inline_code=inline_code_spans(source);
    PublicText result;std::vector<std::size_t> map(source.size()+1);std::size_t wrapper=0;
    for(std::size_t at=0;at<source.size();) {
        map[at]=result.text.size();
        if(wrapper<inline_code.wrappers.size()&&inline_code.wrappers[wrapper].first==at) {
            const auto last=inline_code.wrappers[wrapper++].second;
            for(auto hidden=at;hidden<=last;++hidden)map[hidden]=result.text.size();at=last;continue;
        }
        result.text+=source[at++];
    }
    map.back()=result.text.size();
    for(const auto [first,last]:inline_code.fragments)result.marked.emplace_back(map[first],map[last]);
    result.roles=classify(result.text,result.marked);return result;
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
void current_record_prose() {
    auto rendered=public_text(block("lesson_insertar_un_tipo_y_encadenar_la_salida.json",1,1).text);
    contains(rendered.roles.outputs,locate(rendered.text,L"[2;1]"),"The current record's displayed numeric representation became source code");
    contains(rendered.roles.code,locate(rendered.text,L"aceptados"),"A distinct named record member lost its technical role");
    absent(rendered.roles.outputs,locate(rendered.text,L"[aceptados;rechazados]"),"Symbolic member placeholders were flattened into a literal output result");

    rendered=public_text(block("lesson_insertar_un_tipo_y_encadenar_la_salida.json",4,2).text);
    for(const auto sample:{L"[3;2];fin",L"Memoria=[3;2];fin|Guardado"})
        contains(rendered.roles.outputs,locate(rendered.text,sample),"A composed/file/console record result lost its exact neutral role");
    contains(rendered.roles.outputs,locate_after(rendered.text,L"el contenido será ",L"[3;2]"),
        "A file-content sample borrowed an identical earlier console sample's role");
    contains(rendered.roles.strings,locate(rendered.text,L"\";fin\""),"A quoted source suffix became a neutral produced result");
    contains(rendered.roles.code,locate(rendered.text,L"Recuento{3, 2}"),"A distinct C++ construction became an output sample");
    absent(rendered.roles.outputs,locate(rendered.text,L"Recuento{3, 2}"),"A data cue recolored a separate C++ construction");

    rendered=public_text(block("lesson_insertar_un_tipo_y_encadenar_la_salida.json",4,4).text);
    const auto rows=locate(rendered.text,L"Memoria=[3;2];fin|Guardado\nMemoria=[0;0];fin|Guardado\nMemoria=[10;10];fin|Guardado");
    contains(rendered.roles.outputs,rows,"A next-line literal output section lost a record row or final data character");
    contains(rendered.roles.strings,locate(rendered.text,L"\";fin\""),"The required output block recolored an earlier quoted suffix");
    absent(rendered.roles.outputs,locate(rendered.text,L"Las tres llamadas"),"A next-line output section crossed its terminating blank line");
    contains(rendered.roles.code,locate(rendered.text,L"{10, 10}"),"A separate C++ call argument pair lost its technical role");
    contains(rendered.roles.outputs,locate_after(rendered.text,L"la muestra contiene únicamente ",L"[10;10]"),
        "The last file-content sample lost its independent data role after the result block ended");

    rendered=public_text(block("lesson_insertar_un_tipo_y_encadenar_la_salida.json",5,2).text);
    for(const auto sample:{L"Memoria=[3;2];fin|Guardado",L"Guardado se imprime"}) {
        auto part=locate(rendered.text,sample);if(std::wstring_view(sample)==L"Guardado se imprime")part.second=part.first+8;
        contains(rendered.roles.outputs,part,"A public current reference lost its file/output result role");
    }
    contains(rendered.roles.outputs,locate_after(rendered.text,L"el archivo contiene ",L"[3;2]"),
        "The separate file contents borrowed the same spelling's console role");
    contains(rendered.roles.code,locate(rendered.text,L"close"),"A filename/result cue swallowed a separate API name");

    const auto convention=block("lesson_insertar_un_tipo_y_encadenar_la_salida.json",1,3);
    rendered=public_text(convention.question);
    contains(rendered.roles.outputs,locate(rendered.text,L"[2;1]"),"An unmarked displayed record convention in a current question lost neutral ownership");
    const auto suffix=block("lesson_insertar_un_tipo_y_encadenar_la_salida.json",4,3);
    for(const auto& option:suffix.options)if(option.find("[3;2]")!=option.npos) {
        rendered=public_text(option);
        contains(rendered.roles.outputs,locate(rendered.text,L"[3;2]"),"A current explanatory option lost its locally written record sample");
    }

    rendered=public_text(block("lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json",6,2).text);
    for(const auto sample:{L"\"OK \"",L"\"Fallo \""}) {
        contains(rendered.roles.strings,locate(rendered.text,sample),"An explicit source string after 'muestra' lost its green string role");
        absent(rendered.roles.outputs,locate(rendered.text,sample),"An explicit source literal inherited an output cue");
    }
    rendered=public_text(block("lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json",7,5).text);
    contains(rendered.roles.outputs,locate(rendered.text,L"Fallo [6;2]"),"A current required rejection report became C++ tokens");
    contains(rendered.roles.strings,locate(rendered.text,L"\"[3;4}\""),"A deliberately malformed input string lost literal ownership");
    rendered=public_text(block("lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json",5,4).question);
    contains(rendered.roles.strings,locate(rendered.text,L"\"[2;1]resto\""),"A directly described operator input in a current question lost string ownership");
    require(rendered.roles.outputs.empty(),"A quoted operator input became a produced output record");

    rendered=public_text(block("lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json",7,4).text);
    for(const auto sample:{L"OK [2;1]",L"OK [0;0]",L"OK [10;10]",L"Fallo [6;2]",L"OK [1;3]"}) {
        std::size_t from=0,seen=0;
        while(true) {
            const auto at=rendered.text.find(sample,from);if(at==rendered.text.npos)break;
            contains(rendered.roles.outputs,{at,at+std::wstring_view(sample).size()},"A current ordered report list lost a full or repeated result item");
            from=at+std::wstring_view(sample).size();++seen;
        }
        require(seen==(std::wstring_view(sample)==L"OK [2;1]"?2u:1u),"The current ordered report list changed its exact sample occurrences");
    }
    absent(rendered.roles.outputs,locate(rendered.text,L"resto"),"An ordered report list swallowed a separate pending-input identifier");

    const auto unmarked=public_text("La instrucción `[2;1]` es un ejemplo mal formado. Una cita dice \"escribe [2;1]\". El lector recibe \"[2;1]resto\". El texto menciona [2;1] sin describir una salida. Muestra, en orden, `std::cout << \"OK\";` y `return 0;`.");
    require(unmarked.roles.outputs.empty(),"A source sample, quoted phrase or unexplained record inherited a writing cue");
}
void matching_public_labels(std::string_view file,std::size_t page,std::size_t index,
        std::span<const std::string_view> sources,std::span<const std::string_view> targets) {
    using namespace winrt::Windows::Data::Json;
    const auto current=block(file,page,index);require(current.type==curriculum::BlockType::math_puzzle,"A current matching changed type");
    const auto raw=JsonObject::Parse(winrt::to_hstring(current.raw_json));
    const auto response=raw.GetNamedArray(L"responses").GetObjectAt(0);
    require(response.GetNamedString(L"type")==L"math_matching_drag","A current record matching changed interaction");
    for(const auto side:{0,1}) {
        const auto cards=response.GetNamedArray(side?L"targets":L"sources");const auto expected=side?targets:sources;
        require(cards.Size()==expected.size(),"A current record matching changed its public card count");
        for(std::uint32_t n=0;n<cards.Size();++n) {
            const auto card=cards.GetObjectAt(n);const auto label=winrt::to_string(card.GetNamedString(L"label"));
            require(label==expected[n],"A current record matching changed an exact public label");
            const auto content=card.GetNamedArray(L"content").GetObjectAt(0);
            require(content.GetNamedString(L"mathMode")==L"text"&&winrt::to_string(content.GetNamedString(L"text"))==label,
                "A matching card changed its literal text-mode content");
            const auto rendered=public_text(label);
            if(side) {
                const auto first=rendered.text.find(L": ");require(first!=rendered.text.npos,"A public output label lost its role cue");
                contains(rendered.roles.outputs,{first+2,rendered.text.size()},"A matching result lost spaces, trailing separators or full neutral ownership");
                absent(rendered.roles.outputs,{0,first},"A matching output cue became part of the result's backdrop");
            } else {
                contains(rendered.roles.code,{0,rendered.text.size()},"A public matching source lost its explicit technical role");
                require(rendered.roles.outputs.empty(),"An input literal or stream identifier became a matching output");
                if(rendered.text.front()==L'"')contains(rendered.roles.strings,{0,rendered.text.size()},"A matching input lost its quoted string role");
            }
        }
    }
    const auto feedback=raw.GetNamedObject(L"feedback");
    for(const auto kind:{L"correct",L"incorrect"}) {
        const auto authored=winrt::to_string(feedback.GetNamedString(kind));const auto rendered=public_text(authored);
        require(rendered.text==wide(authored),"A public matching explanation was rewritten");
        require(rendered.roles.outputs.empty(),"An explanatory matching verdict was mistaken for literal output data");
    }
}
void current_matching_labels() {
    const std::array<std::string_view,2> streams={"`a`","`b`"};
    const std::array<std::string_view,2> insertions={"Salida: `|   [10;2]|`","Salida: `[a;2]|a`"};
    matching_public_labels("lesson_insertar_un_tipo_y_encadenar_la_salida.json",5,4,streams,insertions);
    const std::array<std::string_view,3> inputs={"`\"[2;1]resto\"`","`\"[-1;2]RESTO\"`","`\"[2;1\"`"};
    const std::array<std::string_view,3> extractions={"Salida: `1:1|[6;2]|`","Salida: `0:0|[2;1]|resto`","Salida: `1:0|[6;2]|RESTO`"};
    matching_public_labels("lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json",7,3,inputs,extractions);
}
}
int main() {
    try {
        current_choices();current_paragraphs();contextual_boundaries();current_record_prose();current_matching_labels();
        std::cout<<"PASS 12 formatted alternatives, nine inline rows, current record prose and ten matching cards, with source/string/prose/citation/math boundaries; headless roles only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
