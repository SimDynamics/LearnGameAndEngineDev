#pragma once

#include "presentation/windows/programming_technical_ranges.hpp"
#include <optional>

namespace lgaed::presentation::technical_prose {
// Displayed UTF-16 offsets only. Source strings, narration and authored state
// remain unchanged; visible context must never contain unrevealed solutions.
struct CurricularRoles { std::vector<Range> code,numbers,neutral_numbers,citations,outputs,strings; };

namespace curricular_role_detail {
inline void merge(std::vector<Range>& ranges) {
    std::ranges::sort(ranges); std::vector<Range> result;
    for(const auto item:ranges)if(item.first<item.second){
        if(!result.empty()&&item.first<=result.back().second)result.back().second=std::max(result.back().second,item.second);
        else result.push_back(item);
    }
    ranges=std::move(result);
}
inline bool contained(Range item,std::span<const Range> ranges) {
    return std::ranges::any_of(ranges,[&](Range value){return value.first<=item.first&&value.second>=item.second;});
}
inline bool capital(wchar_t value) {
    WORD kind=0;return GetStringTypeW(CT_CTYPE1,&value,1,&kind)&&(kind&C1_UPPER);
}
inline std::vector<Range> bibliographic(std::wstring_view text,std::span<const Range> code) {
    std::vector<Range> result;
    // A year is not sufficient: require author-year citation grammar. Localizers
    // belong to that same citation, never every page/section number in a lesson.
    for(std::size_t at=0;at<text.size();++at){
        if(text[at]!=L'(')continue;
        const auto close=text.find(L')',at+1);if(close==text.npos||close-at>512)continue;
        const auto inside=text.substr(at+1,close-at-1);
        const bool author_year=has(inside,LR"(^[^0-9()]+,\s*(?:[0-9]{4}[a-z]?|s\.[ \t]*f\.|n\.[ \t]*d\.)(?:[ ,;].*)?$)",true)&&
            std::ranges::any_of(inside,letter);
        const bool year_only=has(inside,LR"(^\s*(?:[0-9]{4}[a-z]?|s\.[ \t]*f\.|n\.[ \t]*d\.)(?:\s*[,;].*)?\s*$)",true);
        bool narrative=false;auto first=at;
        if(year_only&&at&&space(text[at-1])){
            auto last=at;while(last&&space(text[last-1]))--last;first=last;
            if(text.substr(0,last).ends_with(L"et al.")){
                last-=6;while(last&&space(text[last-1]))--last;first=last;
            }
            while(first&&(letter(text[first-1])||text[first-1]==L'-'||text[first-1]==L'’'||text[first-1]==L'\''))--first;
            narrative=first<last&&capital(text[first]);
        }
        if(!author_year&&!narrative)continue;
        if(!narrative)first=at;
        auto end=close+1;
        const auto suffix=text.substr(end);
        const auto& localizer=expression(LR"(^\s*,?\s*(?:pp?\.|pág(?:ina)?s?\.?|pages?|sec(?:ción|ciones|tions?)?\.?|sections?|§{1,2})\s*[0-9]+(?:[.][0-9]+)*(?:\s*[–—-]\s*[0-9]+(?:[.][0-9]+)*)?)",true);
        std::match_results<std::wstring_view::const_iterator> match;
        if(std::regex_search(suffix.begin(),suffix.end(),match,localizer))end+=match.length();
        if(!overlaps({first,end},code)){result.emplace_back(first,end);at=end-1;}
    }
    return result;
}
inline void subtract(std::vector<Range>& ranges,std::span<const Range> removed) {
    std::vector<Range> result;
    for(auto item:ranges){std::vector<Range> pieces{item};
        for(const auto cut:removed){std::vector<Range> next;
            for(const auto piece:pieces)if(!overlaps(piece,std::span<const Range>(&cut,1)))next.push_back(piece);
                else {if(piece.first<cut.first)next.emplace_back(piece.first,cut.first);if(cut.second<piece.second)next.emplace_back(cut.second,piece.second);}
            pieces=std::move(next);
        }
        result.insert(result.end(),pieces.begin(),pieces.end());
    }
    ranges=std::move(result);
}
inline bool output_number(std::wstring_view text,Range number,std::span<const Range> strings) {
    if(contained(number,strings))return false;
    // Local output grammar wins over legacy numeric backticks. Do not inherit
    // a role from a matching value elsewhere in a program or its answers.
    auto prefix=tail(text,number.first,192);
    for(auto end=prefix.size();end;--end){
        const auto position=end-1;const auto value=prefix[position];
        const bool decimal=value==L'.'&&position&&position+1<prefix.size()&&digit(prefix[position-1])&&digit(prefix[position+1]);
        if(std::wstring_view(L"\n!?;").find(value)!=std::wstring_view::npos||(value==L'.'&&!decimal)){
            prefix.remove_prefix(end);break;
        }
    }
    return has(prefix,LR"((?:\b(?:antes|ahora|before|after|avant|après|resultado|result|résultat|salida|output|sortie)\s*:\s*|\b(?:la\s+)?(?:consola|console|salida|output|sortie|mensaje|message|resultado|result|résultat)\s+(?:muestra|imprime|es|son|contiene|shows?|prints?|is|are|affiche|est)\s*|\b(?:se\s+muestra|se\s+imprime|produce|produces|produit)\s*)(?:[+−-]?[0-9]+(?:[.,][0-9]+)?[ \t,;]*(?:(?:y|and|et)[ \t]+)?)*$)",true);
}
inline bool output_cue(std::wstring_view prefix) {
    return has(prefix,LR"((?:\b(?:la\s+)?(?:salida|consola|mensaje|resultado|output|console|sortie)\s+(?:(?:exact[oa]|final|esperad[oa])\s+)?(?:(?:debe|puede)\s+)?(?:ser|ser[aá]|es|son|mostrar|muestra|imprime|contiene|produce|shows?|prints?|is|are|affiche|est)\s*|\b(?:se\s+muestra|se\s+imprime|(?:deben?\s+)?(?:mostrar|imprimir|producir)|muestre|muestra|produce)\s+|\b(?:salida|output|sortie)(?:\s+exacta)?\s*[:=]\s*)$)",true);
}
inline bool source_literal_sample(std::wstring_view value) {
    return !value.empty()&&(value.front()==L'"'||value.front()==L'\'')&&closed_quote_end(value,0,value.size())==value.size();
}
inline bool described_data_cue(std::wstring_view prefix) {
    // A composed value or a file's contents are characters, like console
    // output. A quoted C++ literal keeps its separate string role below.
    return has(prefix,LR"(\b(?:(?:el\s+)?(?:archivo|muestra)\s+contiene(?:\s+[uú]nicamente)?|(?:el\s+)?contenido\s+(?:ser[aá]|es)|(?:la\s+)?cadena\s+(?:compuesta|resultante|obtenida)\s+(?:ser[aá]|es)|se\s+presentan?\s+como)\s*$)",true);
}
inline bool record_report_sample(std::wstring_view value) {
    if(value.find(L"::")!=value.npos||has(value,LR"(^(?:const|volatile|int|char|bool|float|double|auto|return|struct)\b)"))return false;
    return value==L"<>"||has(value,LR"(^(?:[A-Za-z_][A-Za-z_0-9]*[ \t]+)?\[[+-]?[0-9]+[ \t]*;[ \t]*[+-]?[0-9]+\]$)")||
        has(value,LR"(^[A-Za-z_0-9+-]+(?:[ \t]+[A-Za-z_0-9]+)*:[A-Za-z_0-9+-]+(?::[A-Za-z_0-9+-]+)*(?:;[A-Za-z_][A-Za-z_0-9]*:[A-Za-z_0-9+-]+)*$)")||
        has(value,LR"(^[A-Za-z_][A-Za-z_0-9]*:<[^<>\r\n]*>$)")||
        has(value,LR"(^[A-Z][a-z_]+(?:[ \t]+[A-Za-z_][A-Za-z_0-9]*)*$)");
}
inline bool string_cue(std::wstring_view prefix) {
    return has(prefix,LR"((?:\b(?:literal(?:\s+de\s+(?:texto|cadena))?|cadena(?:\s+de\s+(?:texto|caracteres))?|etiqueta(?:\s+de\s+texto)?|std::string)\s+(?:(?:exact[oa]|contiene|aporta|conserva|es|con|como)\s+)?|\b(?:operador|lector)\s+(?:recibe|lee)\s+)$)",true);
}
inline std::wstring_view data_sentence(std::wstring_view prefix) {
    std::size_t first=0;
    for(std::size_t at=0;at<prefix.size();++at){
        if(prefix[at]==L'"'||prefix[at]==L'“'||prefix[at]==L'«'){
            auto end=prefix[at]==L'"'?closed_quote_end(prefix,at,prefix.size()):prefix.find(prefix[at]==L'“'?L'”':L'»',at+1);
            if(end!=prefix.npos){if(prefix[at]!=L'"')++end;at=end-1;continue;}
        }
        if(std::wstring_view(L".!?\n").find(prefix[at])!=prefix.npos)first=at+1;
    }
    return prefix.substr(first);
}
inline bool composed_data_sample(std::wstring_view value) {
    // A public result may contain repeated fields or missing separators. Its
    // display role does not certify that the candidate has the right value.
    // A source declaration/statement, quoted literal or ordinary sentence is
    // not such a sample merely because it mentions an equals sign.
    return has(value,LR"(^[A-Za-z_][A-Za-z_0-9]*=[A-Za-z_0-9=+:-]+(?:\.[A-Za-z_0-9=+:-]+)*(?:;[A-Za-z_][A-Za-z_0-9]*=[A-Za-z_0-9=+:-]+(?:\.[A-Za-z_0-9=+:-]+)*)*$)");
}
inline bool formatted_message_sample(std::wstring_view value) {
    // A visible formatted-message candidate may contain a literal brace field,
    // a quantity and an empty/repeated trailing field. This recognizes its
    // presentation shape, never whether it satisfies the exercise.
    return has(value,LR"(^[A-Za-z_][A-Za-z_0-9]*(?:[ \t]+\{[A-Za-z_0-9{}]*\})?:[ \t]*[0-9]+[ \t]+[A-Za-z_][A-Za-z_0-9]*(?:;[ \t]*[A-Za-z_][A-Za-z_0-9]*=[A-Za-z_0-9{}]*)?$)");
}
inline bool result_list_cue(std::wstring_view prefix) {
    return has(prefix,LR"(\b(?:(?:sus|las)\s+l[ií]neas\s+son|su\s+fila\s+es)\s*$)",true);
}
inline bool composed_data_cue(std::wstring_view prefix) {
    return has(prefix,LR"((?:\b(?:forma(?:n)?|compone(?:n)?|construye(?:n)?|obtiene(?:n)?|genera(?:n)?|produce(?:n)?)\s+(?:el\s+|un\s+)?(?:texto|cadena)|\bse\s+ve|\b(?:texto|cadena|string)\s+con\s+el\s+formato)\s*$)",true);
}
inline void data_references(CurricularRoles& roles,std::wstring_view text,std::span<const Range> marked,
        std::span<const Range> protected_ranges) {
    const auto add=[&](std::vector<Range>& result,std::size_t first,std::size_t last){
        if(first<last&&last<=text.size()&&!overlaps({first,last},protected_ranges))result.emplace_back(first,last);
    };
    std::vector<Range> quotations;
    for(std::size_t at=0;at<text.size();++at){
        if(text[at]!=L'"'&&text[at]!=L'“'&&text[at]!=L'«')continue;
        auto end=text[at]==L'"'?closed_quote_end(text,at,text.size()):text.find(text[at]==L'“'?L'”':L'»',at+1);
        if(end==text.npos)continue;if(text[at]!=L'"')++end;
        quotations.emplace_back(at,end);at=end-1;
    }
    // A displayed result block ends at its own blank line. A second result in
    // the same paragraph is independent; the prose between them is not data.
    matches(text,LR"((?:\bsalida[^\n:]{0,64}|\bconsola\s+debe\s+mostrar|\bmuestra\s+estas?\s+(?:(?:dos|tres|[0-9]+)\s+)?l[ií]neas)\s*:\s*\n[ \t]*\n)",[&](const auto& match){
        const auto first=static_cast<std::size_t>(match.position()+match.length());
        auto last=text.find(L"\n\n",first);if(last==text.npos)last=text.size();
        add(roles.outputs,first,last);
    },true);
    // A literal output can start on the very next line, after a bounded
    // instruction about its final newline. Its three data rows are not C++
    // assignments. The next blank line still ends this local output block;
    // argument lists and source references elsewhere retain their own roles.
    matches(text,LR"(\bmuestra\s+(?:exactamente\s+)?estas?\s+(?:(?:dos|tres|[0-9]+)\s+)?l[ií]neas(?:[ \t]*,[^\n:]{0,128})?[ \t]*:[ \t]*\n)",[&](const auto& match){
        const auto cue=static_cast<std::size_t>(match.position()),end=cue+static_cast<std::size_t>(match.length());
        if(overlaps({cue,end},marked)||overlaps({cue,end},protected_ranges))return;
        auto first=end;while(first<text.size()&&(text[first]==L'\n'||text[first]==L'\r'))++first;
        auto last=text.find(L"\n\n",first);if(last==text.npos)last=text.size();
        add(roles.outputs,first,last);
    },true);
    // A named literal-output section can begin on the next line as well.
    // Require the complete label/colon/newline, outside code, and retain the
    // same blank-line boundary used by the existing two-newline form.
    matches(text,LR"(\bsalida(?:\s+(?:exacta|esperada|completa|requerida))?[ \t]*:[ \t]*\r?\n)",[&](const auto& match){
        const auto cue=static_cast<std::size_t>(match.position()),end=cue+static_cast<std::size_t>(match.length());
        if(overlaps({cue,end},marked)||overlaps({cue,end},protected_ranges))return;
        auto first=end;while(first<text.size()&&(text[first]==L'\n'||text[first]==L'\r'))++first;
        auto last=text.find(L"\n\n",first);if(last==text.npos)last=text.size();
        add(roles.outputs,first,last);
    },true);
    std::vector<Range> ordered(marked.begin(),marked.end());std::ranges::sort(ordered);
    std::optional<std::size_t> previous_marked_output_end;
    for(const auto range:ordered){
        if(range.first>=range.second||range.second>text.size())continue;
        const auto prefix=tail(text,range.first,192),fragment=text.substr(range.first,range.second-range.first);
        const bool report=record_report_sample(fragment)||composed_data_sample(fragment);
        const bool continued_report=report&&previous_marked_output_end&&*previous_marked_output_end<=range.first&&
            has(text.substr(*previous_marked_output_end,range.first-*previous_marked_output_end),
                LR"(^[ \t]*(?:,[ \t]*(?:despu[eé]s[ \t]+)?(?:(?:[0-9]+|una|dos|tres|cuatro|cinco|seis|siete|ocho|nueve|diez|once|doce|trece)\s+l[ií]neas?\s+)?|(?:y|o)[ \t]+(?:despu[eé]s[ \t]+)?)$)",true);
        const bool output=output_cue(prefix)||(!source_literal_sample(fragment)&&
            (described_data_cue(prefix)||continued_report||
                (report&&has(prefix,LR"(\b(?:muestra\s*,\s*en\s+(?:ese\s+)?orden\s*,|presenta|muestran|informe\s+empieza\s+(?:con|por)|informe\s+terminado\s+en)\s*$)",true))||
                (has(prefix,LR"(\b(?:el\s+)?texto\s*$)",true)&&has(text.substr(range.second),LR"(^\s+se\s+imprime\b)",true))));
        if(source_literal_sample(fragment))add(roles.strings,range.first,range.second);
        else if(output){add(roles.outputs,range.first,range.second);
            if(!overlaps(range,protected_ranges))previous_marked_output_end=range.second;}
        else if(fragment.find(L"::")==fragment.npos&&fragment.find(L':')!=fragment.npos&&
            has(fragment,LR"(^[^:;{}=<>\n]+:[ \t]*$)")&&
            !has(data_sentence(prefix),LR"(\b(?:literal|variable|s[ií]mbolo|operador)\b|etiqueta\s+C\+\+)",true)&&
            (has(data_sentence(prefix),LR"(\b(?:prompt|salida|consola|muestra|mostrar|imprime)\b)",true)||
                (has(prefix,LR"(\bprompt\b)",true)&&has(data_sentence(prefix),LR"(\bpor\s+ejemplo\s*,?\s*$)",true))))
            add(roles.outputs,range.first,range.second);
    }
    // An unquoted composed value is data when the local prose introduces its
    // composition/result. Preserve every explicitly marked C++ occurrence,
    // even if it has an identical spelling elsewhere in this same paragraph.
    std::optional<std::size_t> previous_output_end;
    matches(text,LR"((?:[A-Za-z_][A-Za-z_0-9]*=[A-Za-z_0-9=+:-]+(?:\.[A-Za-z_0-9=+:-]+)*(?:;[A-Za-z_][A-Za-z_0-9]*=[A-Za-z_0-9=+:-]+(?:\.[A-Za-z_0-9=+:-]+)*)*|\[[^\]\r\n]*\](?:\|\[[^\]\r\n]*\])+))",[&](const auto& match){
        const auto first=static_cast<std::size_t>(match.position()),last=first+static_cast<std::size_t>(match.length());
        if((first&&identifier(text[first-1]))||(last<text.size()&&identifier(text[last]))||overlaps({first,last},marked))return;
        const auto prefix=tail(text,first,192);
        // A comma/conjunction continues only the immediately introduced list.
        // Neither intervening prose nor another source sample inherits it.
        const bool continues=previous_output_end&&*previous_output_end<=first&&
            has(text.substr(*previous_output_end,first-*previous_output_end),LR"(^[ \t]*(?:,[ \t]*|y[ \t]+)$)",true);
        if(composed_data_cue(prefix)||output_cue(prefix)||result_list_cue(prefix)||continues){
            add(roles.outputs,first,last);
            if(!overlaps({first,last},protected_ranges))previous_output_end=last;
        }
    });
    // A two-field numeric record is a displayed character convention when
    // prose explicitly describes writing it. Its semicolon is not a C++
    // statement separator. Do not infer a role from a matching value elsewhere.
    matches(text,LR"(\[[+-]?[0-9]+[ \t]*;[ \t]*[+-]?[0-9]+\])",[&](const auto& match){
        const auto first=static_cast<std::size_t>(match.position()),last=first+static_cast<std::size_t>(match.length());
        if(overlaps({first,last},marked)||overlaps({first,last},quotations))return;
        const auto prefix=tail(text,first,192);
        if(has(prefix,LR"(\b(?:escribir|escribe)\s*$)",true))add(roles.outputs,first,last);
    });
    for(std::size_t at=0;at<text.size();++at){
        if(text[at]!=L'"'&&text[at]!=L'“'&&text[at]!=L'«')continue;
        auto end=text[at]==L'"'?closed_quote_end(text,at,text.size()):text.find(text[at]==L'“'?L'”':L'»',at+1);
        if(end==text.npos)continue;if(text[at]!=L'"')++end;
        const auto prefix=tail(text,at,192);
        const auto sentence=data_sentence(prefix);
        if(contained({at,end},roles.strings)){at=end-1;continue;}
        if(output_cue(prefix))add(roles.outputs,at,end);
        else if(string_cue(prefix)||has(sentence,LR"(\b(?:literal(?:\s+de\s+(?:texto|cadena))?|cadena(?:s)?|etiquetas?\s+de\s+texto|concaten\w*|std::string)\b)",true)||
            (has(prefix,LR"(\bpara\s*$)",true)&&has(text.substr(end),LR"(^\s+en\s+el\s+locale\s+C\b)",true))||
            (has(sentence,LR"(^\s*En\s*$)",true)&&has(prefix,LR"(\bliteral\s+de\s+texto\b)",true)))
            add(roles.strings,at,end);
        at=end-1;
    }
    // Bare displayed words need a local operation cue and a clear data end.
    // Do not promote arbitrary prose just because a matching word occurs in
    // another program or in the answer key.
    matches(text,LR"((?:\b(?:la\s+)?(?:salida|consola|mensaje)\s+(?:(?:exact[oa]|final|esperad[oa])\s+)?(?:(?:debe|puede)\s+)?(?:ser|es|son|mostrar|muestra|imprime|contiene|produce)\s+|\b(?:se\s+muestra|se\s+imprime|mostrar|imprimir|muestre)\s+|\bliteral\s+(?:contiene|aporta)\s+)([^\n,;.()!?]+))",[&](const auto& match){
        auto first=static_cast<std::size_t>(match.position(1)),last=first+static_cast<std::size_t>(match.length(1));
        while(last>first&&space(text[last-1]))--last;
        const auto value=text.substr(first,last-first);
        if(value.empty()||value.size()>96||value.find(L'"')!=value.npos||value.find(L'`')!=value.npos||
            value.find(L"::")!=value.npos||value.find_first_of(L"{}=<>\\")!=value.npos)return;
        WORD kind=0;const bool starts_capital=GetStringTypeW(CT_CTYPE1,value.data(),1,&kind)&&(kind&C1_UPPER);
        if(!starts_capital&&!digit(value.front()))return;
        const auto prefix=tail(text,first,192);
        // A bare numeric literal is not thereby a character string. Its
        // existing source/output numeric role remains authoritative.
        const bool string=string_cue(prefix);
        if(string&&!starts_capital)return;
        add(string?roles.strings:roles.outputs,first,last);
    },true);
    merge(roles.outputs);merge(roles.strings);subtract(roles.strings,roles.outputs);
}
}

inline CurricularRoles curricular_roles(std::wstring_view text,const Analysis& semantic,
    std::span<const Range> explicit_code={}) {
    using namespace curricular_role_detail;
    CurricularRoles result;
    const auto authored=inline_code_spans(text);
    std::vector<Range> trusted(explicit_code.begin(),explicit_code.end());
    trusted.insert(trusted.end(),authored.fragments.begin(),authored.fragments.end());
    const auto isolated_operator=[&](Range range){
        if(range.first>=range.second||range.second>text.size())return false;
        return word_in(text.substr(range.first,range.second-range.first),
            L"+ - * / % = += -= *= /= %= ++ -- == != < > <= >= && || ! & | ^ ~ << >> <<= >>= &= |= ^= :: -> ;");
    };
    for(const auto range:semantic.fragments)if(range.first<range.second&&range.second<=text.size()){
        const bool crosses=std::ranges::any_of(trusted,[&](Range marked){
            return isolated_operator(marked)&&overlaps(range,std::span<const Range>(&marked,1))&&
                (range.first<marked.first||range.second>marked.second);
        });
        if(!crosses)result.code.push_back(range);
        else for(const auto marked:trusted)if(marked.first<marked.second&&marked.second<=text.size()&&
            overlaps(range,std::span<const Range>(&marked,1)))
                result.code.emplace_back(std::max(range.first,marked.first),std::min(range.second,marked.second));
    }
    const auto add_code=[&](std::size_t first,std::size_t last){
        if(first<last&&last<=text.size()&&!overlaps({first,last},semantic.protected_ranges))result.code.emplace_back(first,last);
    };
    // Plain words do not acquire code roles from neighboring source, noun
    // cues or a homonymous authored reference. Only supplied ranges own code.
    merge(result.code);
    data_references(result,text,trusted,semantic.protected_ranges);
    result.code.insert(result.code.end(),result.outputs.begin(),result.outputs.end());
    result.code.insert(result.code.end(),result.strings.begin(),result.strings.end());
    merge(result.code);
    result.citations=bibliographic(text,result.code);
    subtract(result.code,result.citations);
    std::vector<Range> strings=result.strings;
    for(std::size_t cursor=0;cursor<text.size();++cursor)if((text[cursor]==L'"'||text[cursor]==L'\'')&&contained({cursor,cursor+1},result.code)){
        const auto end=closed_quote_end(text,cursor,text.size());
        if(end!=text.npos&&contained({cursor,end},result.code)){strings.emplace_back(cursor,end);cursor=end-1;}
    }
    for(std::size_t cursor=0;cursor<text.size();){
        if(!digit(text[cursor])||(cursor&&identifier(text[cursor-1]))){++cursor;continue;}
        const auto first=cursor++;
        // Numeric tokens keep their real source spelling, including C++ bases,
        // exponent/suffixes and digit separators. No floating-point conversion.
        const bool hexadecimal=text[first]==L'0'&&cursor<text.size()&&(text[cursor]==L'x'||text[cursor]==L'X');
        const bool binary=text[first]==L'0'&&cursor<text.size()&&(text[cursor]==L'b'||text[cursor]==L'B');
        if(hexadecimal||binary)++cursor;
        while(cursor<text.size()){
            const auto current=text[cursor];
            if(digit(current)||(hexadecimal&&((current>=L'a'&&current<=L'f')||(current>=L'A'&&current<=L'F')))){++cursor;continue;}
            if((current==L'.'||current==L'\'')&&cursor+1<text.size()&&digit(text[cursor+1])){++cursor;continue;}
            if(std::wstring_view(L"eEpP").find(current)!=std::wstring_view::npos&&cursor+1<text.size()){
                auto next=cursor+1;if(text[next]==L'+'||text[next]==L'-')++next;
                if(next<text.size()&&digit(text[next])){cursor=next;continue;}
            }
            break;
        }
        while(cursor<text.size()&&std::wstring_view(L"uUlLfFzZ").find(text[cursor])!=std::wstring_view::npos)++cursor;
        if(cursor<text.size()&&identifier(text[cursor])){while(cursor<text.size()&&identifier(text[cursor]))++cursor;continue;}
        auto begin=first;
        if(begin&&text[begin-1]==L'.'&&(begin==1||!identifier(text[begin-2])))--begin;
        if(begin&&std::wstring_view(L"+−-").find(text[begin-1])!=std::wstring_view::npos&&
            (begin==1||space(text[begin-2])||std::wstring_view(L"(:,;=[{").find(text[begin-2])!=std::wstring_view::npos))--begin;
        const Range number{begin,cursor};
        if(overlaps(number,semantic.protected_ranges)||overlaps(number,result.citations))continue;
        result.numbers.push_back(number);
        if(!contained(number,result.code)||output_number(text,number,strings))result.neutral_numbers.push_back(number);
    }
    return result;
}
}
