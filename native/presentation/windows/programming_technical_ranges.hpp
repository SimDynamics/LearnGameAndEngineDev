#pragma once

#include <windows.h>
#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::presentation::technical_prose {
using Range=std::pair<std::size_t,std::size_t>;
inline bool letter(wchar_t value){if(value<128)return (value>=L'a'&&value<=L'z')||(value>=L'A'&&value<=L'Z');WORD type=0;return GetStringTypeW(CT_CTYPE1,&value,1,&type)&&(type&C1_ALPHA);}
inline bool digit(wchar_t value){return value>=L'0'&&value<=L'9';}
inline bool identifier(wchar_t value){if(value<128)return value==L'_'||letter(value)||digit(value);WORD type=0;GetStringTypeW(CT_CTYPE3,&value,1,&type);return letter(value)||(type&(C3_NONSPACING|C3_DIACRITIC|C3_VOWELMARK));}
inline bool space(wchar_t value){return value==L' '||value==L'\t'||value==L'\r'||value==L'\n'||value==L'\v'||value==L'\f'||value==0xa0||value==0x202f;}
inline const std::wregex& expression(std::wstring_view pattern,bool insensitive=false){
    // All expressions are fixed, reviewed rules from ProgrammingTechnicalRanges
    // and ManualEnglishTerms. A per-thread cache avoids recompiling them per view.
    thread_local std::map<std::pair<std::wstring,bool>,std::wregex> cache;
    const auto key=std::pair{std::wstring(pattern),insensitive};auto found=cache.find(key);
    if(found==cache.end())found=cache.emplace(key,std::wregex(key.first,insensitive?std::regex_constants::icase:std::regex_constants::ECMAScript)).first;
    return found->second;
}
inline std::wstring lower(std::wstring_view text);
inline bool has(std::wstring_view text,std::wstring_view pattern,bool insensitive=false){
    if(insensitive){const auto folded=lower(text);return std::regex_search(folded.begin(),folded.end(),expression(pattern,true));}
    return std::regex_search(text.begin(),text.end(),expression(pattern));}
inline bool begins(std::wstring_view text,std::wstring_view pattern,bool insensitive=false){
    if(insensitive){const auto folded=lower(text);return std::regex_search(folded.begin(),folded.end(),expression(pattern,true),std::regex_constants::match_continuous);}
    return std::regex_search(text.begin(),text.end(),expression(pattern),std::regex_constants::match_continuous);}
inline bool at_word(std::wstring_view text,std::wstring_view pattern,bool insensitive=false){
    for(std::size_t begin=0;begin<text.size();++begin)if((!begin||!identifier(text[begin-1]))&&begins(text.substr(begin),pattern,insensitive))return true;return false;
}
template<class Action>void matches(std::wstring_view text,std::wstring_view pattern,Action action,bool insensitive=false){
    for(std::regex_iterator<std::wstring_view::const_iterator> match(text.begin(),text.end(),expression(pattern,insensitive)),end;match!=end;++match)action(*match);
}
inline std::wstring lower(std::wstring_view text){std::wstring result(text);if(!result.empty())LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,text.data(),static_cast<int>(text.size()),result.data(),static_cast<int>(result.size()),nullptr,nullptr,0);return result;}
inline std::wstring_view tail(std::wstring_view text,std::size_t end,std::size_t count){return text.substr(end>count?end-count:0,std::min(end,count));}
inline std::size_t skip_spaces(std::wstring_view text,std::size_t index){while(index<text.size()&&(text[index]==L' '||text[index]==L'\t'))++index;return index;}
inline bool at(std::wstring_view text,std::size_t index,wchar_t value){return index<text.size()&&text[index]==value;}
inline bool overlaps(Range range,std::span<const Range> ranges){return std::ranges::any_of(ranges,[&](Range item){return item.first<range.second&&item.second>range.first;});}
inline bool boundary(std::wstring_view text,std::size_t begin,std::size_t end){return (!begin||!identifier(text[begin-1]))&&(end==text.size()||!identifier(text[end]));}
inline std::size_t closed_quote_end(std::wstring_view text,std::size_t start,std::size_t limit){
    if(text[start]==L'"'&&has(tail(text,start,4),LR"((?:u8|u|U|L)?R$)")){
        const auto opening=text.find(L'(',start+1);if(opening==text.npos||opening>=limit||opening-start-1>16)return text.npos;
        const auto delimiter=text.substr(start+1,opening-start-1);if(delimiter.find_first_of(L" ()\\\t\r\n\v\f")!=delimiter.npos)return text.npos;
        const auto closing=L")"+std::wstring(delimiter)+L"\"";const auto end=text.find(closing,opening+1);
        return end==text.npos||end+closing.size()>limit?text.npos:end+closing.size();
    }
    for(auto index=start+1;index<limit;++index){if(text[index]==L'\\'){++index;continue;}if(text[index]==text[start])return index+1;}return text.npos;
}
inline std::size_t quoted_end(std::wstring_view text,std::size_t start,std::size_t limit){const auto end=closed_quote_end(text,start,limit);return end==text.npos?limit:end;}
inline std::size_t balanced_end(std::wstring_view text,std::size_t start,wchar_t opening,wchar_t closing,std::size_t budget=512){
    int depth=0;const auto limit=budget>text.size()-start?text.size():start+budget;
    for(auto index=start;index<limit;++index){
        if(text[index]==L'"'||text[index]==L'\''){const auto end=closed_quote_end(text,index,limit);if(end==text.npos)return start;index=end-1;continue;}
        if(text[index]==opening)++depth;if(text[index]==closing&&--depth==0)return index+1;
        if(opening==L'<'&&(text[index]==L'\r'||text[index]==L'\n'))return start;
    }return start;
}
inline std::size_t statement_end(std::wstring_view text,std::size_t start){
    const auto limit=std::min(text.size(),start+512);
    for(auto index=start;index<limit;++index){
        const auto current=text[index];if(current==L';')return index+1;
        const bool member_dot=current==L'.'&&index+1<text.size()&&(digit(text[index+1])||(index&&(identifier(text[index-1])||text[index-1]==L')'||text[index-1]==L']')&&identifier(text[index+1])));
        if(current==L'\r'||current==L'\n'||current==L'!'||current==L'?'||(current==L'.'&&!member_dot)||
            (space(current)&&begins(text.substr(index),LR"(\s+(?:y|o|pero|porque|and|or|but|because|which|et|ou|mais|car|puis|qui|que)\b)",true)))return start;
        if(current==L'"'||current==L'\''){const auto end=closed_quote_end(text,index,limit);if(end==text.npos)return start;index=end-1;continue;}
        const auto open=std::wstring_view(L"([{<").find(current);
        if(open!=std::wstring_view::npos){const auto end=balanced_end(text,index,current,std::wstring_view(L")]}>")[open]);if(end>index){
            if(current==L'{'){const auto next=skip_spaces(text,end);return at(text,next,L';')?next+1:end;}index=end-1;}}
    }return start;
}
inline std::size_t suffix_end(std::wstring_view text,std::size_t initial){
    auto end=initial,next=skip_spaces(text,end);
    if(at(text,next,L'<')){const auto balanced=balanced_end(text,next,L'<',L'>');if(balanced>next){end=balanced;next=skip_spaces(text,end);}}
    if(has(tail(text,initial,64),LR"(std[ \t]*::(?:vector|array|string|string_view|map|set|optional|pair|size_t)$)")){
        auto last=skip_spaces(text,end);const bool separated=last>end||at(text,last,L'*')||at(text,last,L'&');
        while(last<text.size()&&(text[last]==L'*'||text[last]==L'&'))last=skip_spaces(text,last+1);
        if(separated&&last<text.size()&&(letter(text[last])||text[last]==L'_')){++last;while(last<text.size()&&identifier(text[last]))++last;
            const auto following=skip_spaces(text,last);if(following<text.size()&&std::wstring_view(L"=;{(").find(text[following])!=std::wstring_view::npos){const auto statement=statement_end(text,last);if(statement>last)return statement;}}
    }
    while(next<text.size()&&std::wstring_view(L"([{").find(text[next])!=std::wstring_view::npos&&(next==end||text[next]==L'{')){
        const auto opening=text[next],closing=opening==L'('?L')':opening==L'['?L']':L'}';const auto balanced=balanced_end(text,next,opening,closing);if(balanced==next)break;
        end=balanced;next=skip_spaces(text,end);
    }
    if(at(text,next,L';'))return next+1;
    for(const auto op:{L"<<",L">>",L"+=",L"-=",L"*=",L"/=",L"=",L"++",L"--"})if(text.substr(next).starts_with(op)){
        const auto statement=statement_end(text,next);if(statement>next)return statement;break;
    }return end;
}
inline bool explicit_semicolon(std::wstring_view text,std::size_t position){
    if(!position||position>=text.size()||text[position]!=L';'||(text[position-1]!=L' '&&text[position-1]!=L'\t'))return false;
    if(position+1<text.size()&&!space(text[position+1])&&std::wstring_view(L"\"')]}.,!?:»").find(text[position+1])==std::wstring_view::npos)return false;
    auto before=tail(text,position,160);const auto stop=before.find_last_of(L";.!?\r\n");if(stop!=before.npos)before.remove_prefix(stop+1);
    const auto after=text.substr(position+1,std::min<std::size_t>(80,text.size()-position-1));auto trimmed=after;while(!trimmed.empty()&&space(trimmed.front()))trimmed.remove_prefix(1);
    if(has(before,LR"((?:^|[^A-Za-z_0-9])(?:el|al|del|la|los|las|un|una|unos|unas|su|sus|otro|otra|cada|the|an?|its|this|that|each|le|les|une?|son|sa|ses|ce|ces|au|du)(?:[ \t]+(?:primer|último|mismo|first|last|premier|dernier))?[ \t]*$)",true)||
       has(before,LR"((?:^|[^A-Za-z_0-9])(?:con|sin|mediante|tras|hasta|(?:después|antes|en lugar|falta|carece|requisito|validación)[ \t]+de|termina(?:da|das|do|dos|n)?[ \t]+en|añad(?:as|e|ir)|agreg(?:ues|a|ar)|pongas|pon|escrib(?:as|e|ir)|insert(?:es|a|ar)|quit(?:es|a|ar)|elimin(?:es|a|ar)|exig(?:e|en|ir)|exija|faltar|comprueba|validar|contiene|conserva|devuelve|encuentra|esperaba|ser|add|append|insert|remove|delete|omit|write|type|use|with|without|symbol|character|sign|terminator|(?:termine|terminer|finit|finir)[ \t]+par)[ \t]*$)",true)||
       has(before,LR"((?:^|[^A-Za-z_0-9])(?:signos?|símbolos?|carácter|caracteres|operador(?:es)?|operators?|tokens?|symboles?|signes?|caractères?|opérateurs?|terminateurs?)[ \t]*:?[ \t]*$)",true))return true;
    if(has(before,LR"(\b(?:ajoute|ajouter|écris|écrire|insère|insérer|supprime|supprimer|retire|retirer)(?:[ \t]+pas)?[ \t]*$)",true)&&
       (trimmed.empty()||std::wstring_view(L".,!?").find(trimmed.front())!=std::wstring_view::npos||begins(trimmed,LR"((?:à|avant|après|dans|pour|entre|sur|comme|ici)(?=\s|$|[.,:;!?]))",true)))return true;
    const bool enumeration=has(before,LR"((?:,|\b(?:y|o|and|or|et|ou))[ \t]*$)",true);
    if((std::ranges::all_of(before,space)||enumeration||has(before,LR"(\bsi[ \t]*$)",true))&&begins(after,LR"([ \t]+(?:(?:se[ \t]+)?(?:reconoce[n]?|devuelve|consume|reserva|termina|separa)|ya[ \t]+salió)(?=\s|$|[.,;!?]))",true))return true;
    if(enumeration&&has(before,LR"(\b(?:signos?|tokens?|unidades?|literal|lector|entrega(?:r)?|devuelve|encuentra|secuencia|entre)\b)",true))return true;
    if(has(before,LR"(\b(?:sentencia|final)[ \t]*$)",true)&&has(before,LR"(\bgramática\b|→|[+*/-][ \t]*,[ \t]*[+*/-])",true))return true;
    const auto opening=before.find_last_of(L'['),closing=after.find(L']');
    return opening!=before.npos&&closing!=after.npos&&has(before.substr(opening+1),LR"(^[ \t+\-0-9]+$)")&&has(after.substr(0,closing),LR"(^[ \t+\-0-9]+$)");
}
inline bool word_in(std::wstring_view word,std::wstring_view inventory){
    for(auto begin=std::size_t{0};begin<inventory.size();){const auto end=inventory.find(L' ',begin);if(inventory.substr(begin,end==inventory.npos?end:end-begin)==word)return true;if(end==inventory.npos)break;begin=end+1;}return false;
}
inline bool manual_term(std::wstring_view word){
    // Finite reviewed ManualEnglishTerms inventory; no language detection or
    // recognition of arbitrary English-looking prose is involved.
    return word_in(word,L"include iostream cout cin main return double string bool float void long short signed unsigned const constexpr true false if else while switch break continue default try catch throw class struct enum public private protected virtual override using namespace template typename static_cast dynamic_cast reinterpret_cast const_cast nullptr noexcept new delete string_view size_t numeric_limits getline size empty push_back pop_back begin front back resize clear insert erase find sort swap move pair istream ostream ifstream ofstream istringstream ostringstream sstream ios fstream iomanip stdexcept cctype cstdio cstddef cstdint cassert algorithm limits utility cmath random format fixed scientific defaultfloat hex oct dec left right setw setfill setprecision endl cerr clog flush ws fail good bad eof EOF close seed reset mt19937 uniform_int_distribution uniform_real_distribution random_device result_type exception runtime_error logic_error invalid_argument out_of_range bad_alloc assert isalpha isdigit isspace tolower toupper int char auto do for case final define ifdef ifndef endif elif undef pragma vector array map set optional memory end at get str what data reserve");
}
inline bool template_context(std::wstring_view word,std::wstring_view before,std::wstring_view after){
    if(word_in(word,L"vector array map set optional numeric_limits")&&begins(after,LR"(<\s*[A-Za-z_][A-Za-z_0-9:]*(?:\s*[,>]))"))return true;
    return word_in(word,L"int char auto bool double float void string unsigned signed short long const")&&before.ends_with(L'<')&&
        has(before,LR"((?:vector|array|map|set|optional|static_cast|dynamic_cast|const_cast|reinterpret_cast)<$)");
}
inline bool type_cue(std::wstring_view word,std::wstring_view before,std::wstring_view after){
    if(word==L"final")return has(before,LR"((?:override|\))\s+$)")&&begins(after,LR"(\s*(?:[;{]|=\s*0))");
    if(word==L"case")return has(lower(before),LR"((?:un|cada|ningún|ningun|ese|el)\s+$)")||begins(after,LR"(\s+(?:[+−-]?\d+|[A-Za-z_][A-Za-z_0-9]*|'[^']')\s*:)");
    if(word==L"for")return has(lower(before),LR"((?:^|[^A-Za-z_0-9])(?:bucle|ciclo|sentencia|estructura|instrucción|recorrido|encabezado de|un|el|cada|del|su|en)\s+$)")||
        has(before,LR"(\bwhile\s+(?:y|o|en lugar de)\s+$)")||begins(after,LR"(\s+(?:y|o)\s+while\b)");
    if(word==L"data"||word==L"reserve")return has(lower(before),LR"((?:miembro|método)\s+$)");
    if(word==L"auto"||word==L"int"||word==L"char")return begins(after,word==L"auto"?LR"(\s+[*&]*\s*[A-Za-z_][A-Za-z_0-9]*\s*[=({])":LR"(\s+[*&]*\s*[A-Za-z_][A-Za-z_0-9]*\s*(?:[;=({,)]|$))")||after.starts_with(L'&')||after.starts_with(L'*')||
        begins(after,LR"(\s+(?:deduce|conserva)\b[^.!?]{0,40}\btipo\b)")||has(lower(before),LR"((?:convertir\w*|conversión|deducción)\b[^.!?]{0,40}\s(?:a|con|de)\s+$)")||begins(after,LR"(\s+(?:es|puede ser)\s+(?:una palabra|un tipo)\b)");
    if(word==L"do"){auto remaining=after;while(!remaining.empty()&&space(remaining.front()))remaining.remove_prefix(1);return remaining.starts_with(L'{')||after.starts_with(L"-while")||after.starts_with(L"-cuerpo-while");}
    return false;
}
inline bool selected_manual(std::wstring_view text,std::size_t begin,std::size_t end,bool code,std::string_view locale){
    const auto word=text.substr(begin,end-begin);if(!manual_term(word))return false;
    const auto before=tail(text,begin,100),after=text.substr(end,std::min<std::size_t>(100,text.size()-end));const auto lowered=lower(before);
    const bool qualified=has(before,LR"((?:^|[^A-Za-z_0-9])std::(?:[A-Za-z_][A-Za-z_0-9]*::)*$)");
    const bool header=before.ends_with(L'<')&&after.starts_with(L'>');const bool templated=template_context(word,before,after);
    const bool call=after.starts_with(L'(');auto spaced_after=after;while(!spaced_after.empty()&&space(spaced_after.front()))spaced_after.remove_prefix(1);
    const bool member=has(before,LR"([A-Za-z_0-9)\]]\s*(?:\.|->)\s*$)")&&spaced_after.starts_with(L'(');
    const bool french=locale=="fr";const bool english=locale=="en"||locale=="en-US"||locale=="en-GB";
    if(french){
        if(word_in(word,L"include iostream cout cin return"))return true;
        if(word_in(word,L"string break set flush"))return code||qualified||header||templated;
        const bool callable=word_in(word,L"main getline size empty push_back pop_back begin end at front back data reserve resize clear insert erase find sort swap move format setw setfill setprecision flush ws fail good bad eof close get str what seed reset assert isalpha isdigit isspace tolower toupper");
        const bool type=word_in(word,L"int char auto bool double float void string unsigned signed short long const string_view vector array map set optional size_t numeric_limits pair istream ostream ifstream ofstream istringstream ostringstream ios mt19937 uniform_int_distribution uniform_real_distribution random_device result_type exception runtime_error logic_error invalid_argument out_of_range bad_alloc");
        const bool header_word=word_in(word,L"iostream string string_view vector array map set optional sstream fstream ios istream ostream iomanip stdexcept cctype cstdio cstddef cstdint cassert algorithm limits utility cmath memory random format");
        const bool instruction=word_in(word,L"return if else for while do switch case break continue try catch throw using new delete assert");
        const bool keyword=instruction||word_in(word,L"int char bool float double void long short signed unsigned const constexpr auto true false class struct enum public private protected virtual override final namespace template typename static_cast dynamic_cast reinterpret_cast const_cast nullptr noexcept");
        const bool named_callable=word!=L"sort"||has(lowered,LR"(\b(?:fonction|m[ée]thode|manipulateur|membre)\s+(?:nomm[ée]e?|appel[ée]e?)\s+$)");
        auto line_before=before.substr(before.find_last_of(L'\n')==before.npos?0:before.find_last_of(L'\n')+1);auto trimmed=before;while(!trimmed.empty()&&space(trimmed.back()))trimmed.remove_suffix(1);
        const bool declaration=std::ranges::all_of(line_before,space)||(!trimmed.empty()&&std::wstring_view(L";{}").find(trimmed.back())!=std::wstring_view::npos);
        const bool role=(callable&&named_callable&&has(lowered,LR"(\b(?:fonction|m[ée]thode|manipulateur)\s+(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?$)"))||
            ((callable||word==L"result_type")&&named_callable&&has(lowered,LR"(\bmembre\s+(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?$)"))||
            (type&&has(lowered,LR"(\btype(?:s)?(?: de donn[ée]es)?\s+(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?$)"))||
            (type&&!word_in(word,L"int char auto bool double float void unsigned signed short long const")&&has(lowered,LR"(\b(?:classe|conteneur)\s+(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?$)"))||
            (header_word&&has(lowered,LR"(\b(?:biblioth[èe]que|en-t[êe]te|fichier d['’]en-t[êe]te)\s+(?:standard\s+)?(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?$)"))||
            (keyword&&has(lowered,LR"(\b(?:mots?[- ]cl[ée]s?|mots? r[ée]serv[ée]s?|sp[ée]cificateur|modificateur)\s+(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?$)"))||
            (instruction&&has(lowered,LR"(\b(?:instruction|boucle|condition|branche|clause)\s+(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?$)"))||
            (type&&declaration&&begins(after,LR"(\s+[*&]*\s*[A-Za-z_][A-Za-z_0-9]*\s*[;=({])"))||
            (word_in(word,L"public private protected")&&declaration&&spaced_after.starts_with(L':'))||
            (word==L"final"&&type_cue(word,before,after))||templated;
        const bool directive=word_in(word,L"include define ifdef ifndef endif elif undef pragma")&&has(lowered,LR"((?:#\s*|\bdirective\s+(?:(?:du|de) pr[ée]processeur\s+)?(?:(?:nomm[ée]e?|appel[ée]e?)\s+)?)$)");
        // This is the same context intersection used by ManualEnglishTerms.
        if(word_in(word,L"define ifdef ifndef endif elif undef pragma"))return directive;
        if(word_in(word,L"vector array map set optional memory"))return qualified||header||role;
        if(word_in(word,L"int char auto do"))return code||qualified||header||(call&&(word_in(word,L"if for while switch catch")||callable))||role;
        if(word_in(word,L"end at get str what"))return code||qualified||(call&&callable)||role;
        if(word_in(word,L"data reserve"))return qualified||(member&&call&&callable)||role;
        if(word==L"for")return code||call||role;if(word==L"case")return code||role;if(word==L"final")return role;
        return code||qualified||header||(member&&call&&callable)||(call&&(callable||word_in(word,L"if for while switch catch")))||role||directive;
    }
    const bool nominal=has(lowered,LR"((?:tipo(?:s| de dato)?|contenedor|clase|cabecera|biblioteca|palabra (?:clave|reservada)|especificador|modificador)\s+(?:denominad[oa]\s+|llamad[oa]\s+)?$)")||templated||type_cue(word,before,after);
    bool selected=true;
    if(word_in(word,L"int char auto do"))selected=code||qualified||header||spaced_after.starts_with(L'(')||nominal;
    else if(word==L"for")selected=code||spaced_after.starts_with(L'(')||nominal;
    else if(word==L"case")selected=code||nominal;
    else if(word==L"final")selected=nominal;
    else if(word_in(word,L"define ifdef ifndef endif elif undef pragma"))selected=has(before,LR"((?:#\s*|directiva\s+(?:almohadilla\s+)?)$)");
    else if(word_in(word,L"vector array map set optional memory"))selected=qualified||header||nominal;
    else if(word_in(word,L"end at get str what"))selected=code||qualified||spaced_after.starts_with(L'(')||nominal;
    else if(word_in(word,L"data reserve"))selected=qualified||member||nominal;
    if(!selected||!english)return selected;
    return word_in(word,L"iostream cout cin cerr endl nullptr constexpr noexcept size_t bool int wchar_t")||word.find(L'_')!=word.npos||
        has(before,LR"(\b(?:function|method|operator|keyword|namespace|type|class|header|library|directive|identifier|variable|object|pointer|reference|entry point|body of|returned by)\s+(?:(?:named|called)\s+)?$)",true)||
        begins(after,LR"(\s+(?:function|method|operator|keyword|type|header|directive|identifier)\b|\s+is\s+(?:the|an?)\s+entry point\b)",true)||call||header;
}
inline std::size_t skip_whitespace(std::wstring_view text,std::size_t at){while(at<text.size()&&space(text[at]))++at;return at;}
inline std::size_t spanish_quote_end(std::wstring_view text,std::size_t start){
    for(auto at=start+1;at<text.size();++at){if(text[at]==L'\\'){++at;continue;}if(text[at]==text[start])return at+1;if(text[at]==L'\n')return start;}return start;
}
inline std::size_t spanish_balanced_end(std::wstring_view text,std::size_t start,wchar_t opening,wchar_t closing){
    int depth=0;for(auto at=start;at<text.size();++at){if(text[at]==L'"'||text[at]==L'\''){const auto end=spanish_quote_end(text,at);if(end==at)return start;at=end-1;continue;}
        if(text[at]==opening)++depth;if(text[at]==closing&&--depth==0)return at+1;if(opening==L'<'&&text[at]==L'\n')return start;}return start;
}
inline std::size_t spanish_expression_end(std::wstring_view text,std::size_t start){
    for(auto end=start;end<text.size();++end){const auto value=text[end];if(value==L';')return end+1;
        if(value==L'\n'||value==L':'||value==L'!'||value==L'?'||(value==L'.'&&(end+1==text.size()||!digit(text[end+1]))))return end;
        if(value==L'\''||value==L'"'){const auto quoted=spanish_quote_end(text,end);if(quoted==end)return end;end=quoted-1;continue;}
        if(end>start&&space(value)){const auto word=skip_whitespace(text,end);auto last=word;while(last<text.size()&&letter(text[last]))++last;
            if(last<text.size()&&space(text[last])&&word_in(text.substr(word,last-word),L"y o pero porque cuando para sin con"))return end;}
    }return text.size();
}
inline std::size_t spanish_suffix_end(std::wstring_view text,std::size_t initial,bool templated=false){
    auto end=initial,next=skip_whitespace(text,end);
    if(templated&&at(text,next,L'<')){const auto group=spanish_balanced_end(text,next,L'<',L'>');if(group>next){end=group;next=skip_whitespace(text,end);}}
    if(next<text.size()&&std::wstring_view(L"({[").find(text[next])!=std::wstring_view::npos){const auto opening=text[next];const auto group=spanish_balanced_end(text,next,opening,opening==L'('?L')':opening==L'{'?L'}':L']');if(group>next){end=group;next=skip_whitespace(text,end);}}
    if(at(text,next,L';'))return next+1;
    for(const auto op:{L"<<=",L">>=",L"+=",L"-=",L"*=",L"/=",L"%=",L"&=",L"|=",L"^=",L"="})if(text.substr(next).starts_with(op)){
        if(std::wstring_view(op)==L"="&&text.substr(next).starts_with(L"=="))break;return spanish_expression_end(text,next);}
    return end;
}
inline std::size_t spanish_declaration_end(std::wstring_view text,std::size_t begin){
    auto current=begin,end=begin;std::vector<Range> modifiers;std::wstring_view word;
    for(;;){end=current;while(end<text.size()&&identifier(text[end]))++end;word=text.substr(current,end-current);
        if(!word_in(word,L"const unsigned signed long short"))break;const auto next=skip_whitespace(text,end);if(next==end)break;modifiers.emplace_back(current,end);current=next;}
    if(!word_in(word,L"int double float bool char auto void long short")){
        if(modifiers.empty()||!word_in(text.substr(modifiers.back().first,modifiers.back().second-modifiers.back().first),L"long short"))return begin;
        current=modifiers.back().first;end=modifiers.back().second;
    }
    auto next=skip_whitespace(text,end);
    if(at(text,next,L'*')||at(text,next,L'&')){while(at(text,next,L'*')||at(text,next,L'&'))++next;
        auto name=skip_whitespace(text,next);if(name<text.size()&&(letter(text[name])||text[name]==L'_')){++name;while(name<text.size()&&identifier(text[name]))++name;next=name;}return next;}
    if(next==end||next>=text.size()||(!letter(text[next])&&text[next]!=L'_'))return begin;
    auto name=next+1;while(name<text.size()&&identifier(text[name]))++name;next=skip_whitespace(text,name);
    return next<text.size()&&std::wstring_view(L"({=;").find(text[next])!=std::wstring_view::npos?name:begin;
}
struct Protection {std::vector<Range> mathematics,links;};
inline Protection protection(std::wstring_view text,std::span<const Range> protected_mathematics={}){
    Protection result;
    for(auto index=std::size_t{0};index<text.size();++index){
        std::wstring_view opening,closing;
        if(text.substr(index).starts_with(L"\\(")){opening=L"\\(";closing=L"\\)";}
        else if(text.substr(index).starts_with(L"\\[")){opening=L"\\[";closing=L"\\]";}
        else if(text.substr(index).starts_with(L"$$")){opening=L"$$";closing=L"$$";}
        else if(text[index]==L'$'){opening=L"$";closing=L"$";}
        if(opening.empty())continue;auto end=text.find(closing,index+opening.size());
        if(opening==L"$$"&&end==text.npos){opening=L"$";closing=L"$";end=text.find(L'$',index+1);}
        if(end==text.npos||(opening==L"$"&&(end==index+1||text.substr(index+1,end-index-1).find_first_of(L"\r\n")!=text.npos)))continue;
        result.mathematics.emplace_back(index,end+closing.size());index=end+closing.size()-1;
    }
    for(const auto& range:protected_mathematics)if(range.first<range.second&&range.second<=text.size())result.mathematics.push_back(range);
    for(auto index=std::size_t{0};index<text.size();++index){if(text[index]!=L'h'&&text[index]!=L'H'&&text[index]!=L'w'&&text[index]!=L'W')continue;
        const auto prefix=lower(text.substr(index,std::min<std::size_t>(8,text.size()-index)));
        if(!prefix.starts_with(L"http://")&&!prefix.starts_with(L"https://")&&!prefix.starts_with(L"www."))continue;
        auto end=index;while(end<text.size()&&!space(text[end])&&text[end]!=L'`')++end;result.links.emplace_back(index,end);index=end-1;
    }return result;
}
struct InlineCodeSpans {std::vector<Range> fragments,wrappers;};
inline InlineCodeSpans inline_code_spans(std::wstring_view source,std::span<const Range> mathematics={}){
    const auto protected_text=protection(source,mathematics);InlineCodeSpans result;
    for(auto cursor=std::size_t{0};cursor<source.size();){
        if(source[cursor]!=L'`'||overlaps({cursor,cursor+1},protected_text.mathematics)||overlaps({cursor,cursor+1},protected_text.links)){++cursor;continue;}
        const std::wstring_view delimiter=source.substr(cursor).starts_with(L"```")?L"```":L"`";
        const auto end=source.find(delimiter,cursor+delimiter.size());if(end==source.npos){cursor+=delimiter.size();continue;}
        if(!std::ranges::all_of(source.substr(cursor+delimiter.size(),end-cursor-delimiter.size()),space)&&
            !overlaps({cursor,end},protected_text.mathematics)){
            result.fragments.emplace_back(cursor+delimiter.size(),end);result.wrappers.emplace_back(cursor,cursor+delimiter.size());result.wrappers.emplace_back(end,end+delimiter.size());}
        cursor=end+delimiter.size();
    }return result;
}
struct Analysis {std::vector<Range> fragments,protected_ranges;};
inline std::string_view canonical_locale(std::string_view locale){
    if(locale.size()<2||(locale.size()>2&&locale[2]!='-'&&locale[2]!='_'))return "es";
    const auto ascii=[](char value){return value>='A'&&value<='Z'?static_cast<char>(value-'A'+'a'):value;};
    if(ascii(locale[0])=='f'&&ascii(locale[1])=='r')return "fr";
    if(ascii(locale[0])=='e'&&ascii(locale[1])=='n')return "en";
    return "es";
}
struct SyntaxProjection {
    std::wstring text;std::vector<std::size_t> boundaries{0};
    explicit SyntaxProjection(std::wstring_view source,bool normalize_identifiers=true){
        const auto append=[&](wchar_t value,std::size_t end){text+=value;boundaries.push_back(end);};
        for(auto index=std::size_t{0};index<source.size();){
            const auto begin=index;const auto current=source[index];
            if(current==L'"'||current==L'\''){
                const auto end=closed_quote_end(source,index,source.size());
                if(end!=source.npos&&end-index>128){append(current,index+1);append(L'Q',end-1);append(current,end);index=end;continue;}
            }
            if(letter(current)||current==L'_'){++index;while(index<source.size()&&identifier(source[index]))++index;
                if(index-begin>128){append(L'Z',index);continue;}}
            else if(digit(current)){++index;while(index<source.size()&&digit(source[index]))++index;if(index-begin>128){append(L'0',index);continue;}}
            else if(space(current)){const bool horizontal=current==L' '||current==L'\t';++index;
                while(index<source.size()&&space(source[index])&&((source[index]==L' '||source[index]==L'\t')==horizontal))++index;
                if(index-begin>128){append(current,index);continue;}}
            else ++index;
            for(auto at=begin;at<index;++at)append(normalize_identifiers&&source[at]>127&&identifier(source[at])?L'_':source[at],at+1);
        }
    }
};
inline Analysis analyze(std::wstring_view text,std::string_view locale="es",std::span<const Range> explicit_code={},std::span<const Range> protected_mathematics={}){
    locale=canonical_locale(locale); // Explicit metadata only; never inspect prose to guess a language.
    Analysis result;if(text.empty())return result;
    auto protected_text=protection(text,protected_mathematics);const auto& mathematics=protected_text.mathematics;const auto& links=protected_text.links;
    result.protected_ranges=mathematics;result.protected_ranges.insert(result.protected_ranges.end(),links.begin(),links.end());
    const auto add=[&](std::size_t begin,std::size_t end,bool explicit_range=false){
        if(begin>=end||end>text.size()||overlaps({begin,end},mathematics)||(!explicit_range&&overlaps({begin,end},links)))return;
        if(std::ranges::all_of(text.substr(begin,end-begin),space))return;result.fragments.emplace_back(begin,end);
    };
    std::vector<bool> code(text.size(),false),frontend_code(text.size(),false),protected_word(text.size(),false),closed_literal(text.size(),false),double_literal(text.size(),false),character_literal(text.size(),false);
    const auto own_code=inline_code_spans(text,protected_mathematics);std::vector<Range> code_ranges(explicit_code.begin(),explicit_code.end());
    code_ranges.insert(code_ranges.end(),own_code.fragments.begin(),own_code.fragments.end());
    for(const auto& range:code_ranges)if(range.first<range.second&&range.second<=text.size()){
        add(range.first,range.second,true);std::fill(code.begin()+range.first,code.begin()+range.second,true);std::fill(frontend_code.begin()+range.first,frontend_code.begin()+range.second,true);
    }
    for(auto index=std::size_t{0};index<text.size();){
        if(text[index]!=L'`'||overlaps({index,index+1},mathematics)){++index;continue;}
        const std::wstring_view delimiter=text.substr(index).starts_with(L"```")?L"```":L"`";
        const auto closing=text.find(delimiter,index+delimiter.size()),end=closing==text.npos?text.size():closing;
        std::fill(code.begin()+index+delimiter.size(),code.begin()+end,true);
        if(closing!=text.npos)std::fill(frontend_code.begin()+index+delimiter.size(),frontend_code.begin()+end,true);
        index=closing==text.npos?text.size():closing+delimiter.size();
    }
    for(const auto& range:result.protected_ranges)std::fill(protected_word.begin()+range.first,protected_word.begin()+range.second,true);
    // ManualEnglishTerms protects literal data, raw strings, numeric apostrophe
    // separators and French elisions before consulting its finite term table.
    for(auto index=std::size_t{0};index<text.size();++index)if(text[index]==L'"'||text[index]==L'\''){
        if(text[index]==L'\''&&index&&index+1<text.size()&&digit(text[index-1])&&digit(text[index+1]))continue;
        if(locale=="fr"&&text[index]==L'\''&&index+1<text.size()&&letter(text[index+1])&&!(index+2<text.size()&&text[index+2]==L'\'')){
            auto start=index;while(start&&letter(text[start-1]))--start;
            if(word_in(lower(text.substr(start,index-start)),L"l d j m n s t c qu jusqu lorsqu puisqu quoiqu quelqu aujourd"))continue;
        }
        const auto closed=closed_quote_end(text,index,text.size()),end=closed==text.npos?text.size():closed;
        if(text[index]==L'"')std::fill(double_literal.begin()+index,double_literal.begin()+end,true);
        if(closed!=text.npos&&(text[index]==L'"'||((!index||!identifier(text[index-1]))&&(end==text.size()||!identifier(text[end])))))std::fill(closed_literal.begin()+index,closed_literal.begin()+end,true);
        std::fill(protected_word.begin()+index,protected_word.begin()+end,true);index=end-1;
    }
    for(std::size_t index=0;index<text.size();++index)if(text[index]==L'“'||text[index]==L'«'){
        const auto closing=text[index]==L'“'?L'”':L'»';auto end=index+1;for(;end<text.size();++end){if(text[end]==L'\\'&&end+1<text.size()){++end;continue;}if(text[end]==closing){++end;break;}}
        std::fill(double_literal.begin()+index,double_literal.begin()+end,true);index=end-1;
    }
    if(locale=="fr")for(std::size_t index=0;index<text.size();++index)if(std::wstring_view(L"'‘’").find(text[index])!=std::wstring_view::npos&&(!index||(!letter(text[index-1])&&!digit(text[index-1])))){
        const auto closing=text[index]==L'\''?L'\'':L'’';auto end=index+1;
        for(;end<text.size();++end){if(text[end]==L'\\'&&end+1<text.size()){++end;continue;}
            if(text[end]==closing&&!(end&&letter(text[end-1])&&end+1<text.size()&&letter(text[end+1])))break;}
        if(end<text.size()){std::fill(character_literal.begin()+index,character_literal.begin()+end+1,true);index=end;}
    }
    // Preserve UTF-16 offsets while permitting the Unicode identifiers accepted
    // by Android's \p{L}/\p{M}/\p{N} syntax. Cue matching still uses original text.
    const SyntaxProjection syntax(text);
    const auto syntactic=[&](std::wstring_view pattern,auto action){matches(syntax.text,pattern,[&](const auto& match){
        const auto first=static_cast<std::size_t>(match.position()),last=first+match.length();
        const auto start=syntax.boundaries[first],end=syntax.boundaries[last];if(start&&identifier(text[start-1]))return;action(match,start,end);
    });};
    for(auto index=std::size_t{0};index<text.size();++index)if(text[index]==L'#'&&(!index||!identifier(text[index-1]))){
        auto begin=index+1;while(begin<text.size()&&space(text[begin]))++begin;auto end=begin;while(end<text.size()&&identifier(text[end]))++end;
        const auto directive=text.substr(begin,end-begin);if(directive==L"include"){
            const auto argument=skip_spaces(text,end);if(at(text,argument,L'<')){const auto close=text.find_first_of(L">\r\n",argument+1);if(close!=text.npos&&text[close]==L'>')end=close+1;}
            else if(at(text,argument,L'"')){const auto close=closed_quote_end(text,argument,text.size());if(close!=text.npos&&text.substr(argument,close-argument).find_first_of(L"\r\n")==text.npos)end=close;}
            add(index,end);
        }else if(word_in(directive,L"define undef ifdef ifndef endif elif else pragma error warning")){
            auto argument=skip_spaces(text,end);if(argument>end&&argument<text.size()&&(letter(text[argument])||text[argument]==L'_')){++argument;while(argument<text.size()&&identifier(text[argument]))++argument;end=argument;}add(index,end);
        }
    }
    std::size_t qualified_end=0;
    for(auto index=std::size_t{0};index<text.size();){
        if(!letter(text[index])&&text[index]!=L'_'){++index;continue;}const auto begin=index++;while(index<text.size()&&identifier(text[index]))++index;
        if(begin&&identifier(text[begin-1]))continue;
        if(begin>=qualified_end&&text.substr(begin,index-begin)==L"std"){
            auto colon=skip_spaces(text,index);if(text.substr(colon).starts_with(L"::")){
                auto end=colon+2;while(end<text.size()&&(letter(text[end])||text[end]==L'_')){++end;while(end<text.size()&&identifier(text[end]))++end;
                    if(!text.substr(end).starts_with(L"::")||end+2>=text.size()||(!letter(text[end+2])&&text[end+2]!=L'_'))break;end+=2;}add(begin,suffix_end(text,end));qualified_end=end;
            }
            colon=index;while(colon<text.size()&&space(text[colon]))++colon;
            if(text.substr(colon).starts_with(L"::")){auto output=colon+2;while(output<text.size()&&space(text[output]))++output;
                if(text.substr(output).starts_with(L"cout")&&(output+4==text.size()||!identifier(text[output+4])))add(begin,suffix_end(text,output+4));}
        }
        if(std::any_of(protected_word.begin()+begin,protected_word.begin()+index,[](bool value){return value;}))continue;
        if(selected_manual(text,begin,index,code[begin],locale))add(begin,suffix_end(text,index));
    }
    std::size_t next_declaration=0;
    for(auto begin=std::size_t{0};begin<text.size();++begin){
        if(begin<next_declaration)continue;
        if((begin&&identifier(text[begin-1]))||(!letter(text[begin])&&text[begin]!=L'_'))continue;
        auto current=begin,end=begin;std::wstring_view base;
        for(;;){end=current;while(end<text.size()&&identifier(text[end]))++end;base=text.substr(current,end-current);
            if(!word_in(base,L"const constexpr static volatile unsigned signed"))break;
            auto next=end;while(next<text.size()&&space(text[next]))++next;if(next==end)break;current=next;}
        next_declaration=current+1;
        if(!word_in(base,L"auto bool char char8_t char16_t char32_t wchar_t short int long float double size_t"))continue;
        current=end;while(current<text.size()&&space(text[current]))++current;
        if(current==end&&!at(text,current,L'*')&&!at(text,current,L'&'))continue;
        while(at(text,current,L'*')||at(text,current,L'&'))current=skip_spaces(text,current+1);
        if(current>=text.size()||(!letter(text[current])&&text[current]!=L'_'))continue;
        end=current+1;while(end<text.size()&&identifier(text[end]))++end;
        const auto next=skip_spaces(text,end);if(next>=text.size()||std::wstring_view(L"=;{([,").find(text[next])==std::wstring_view::npos)continue;
        const auto value=text.substr(begin,end-begin);const auto first=value.substr(0,value.find(L' '));const bool weak=word_in(first,L"long short double float");
        auto prefix=text.substr(0,begin);const auto newline=prefix.find_last_of(L'\n');if(newline!=prefix.npos)prefix.remove_prefix(newline+1);while(!prefix.empty()&&space(prefix.back()))prefix.remove_suffix(1);
        if(!weak||prefix.empty()||std::wstring_view(L":;{`").find(prefix.back())!=std::wstring_view::npos||
            has(tail(prefix,prefix.size(),128),LR"(\b(?:function|method|operator|keyword|namespace|type|class|header|library|directive|identifier|variable|object|pointer|reference|entry point|body of|returned by)\s+(?:(?:named|called)\s+)?$)",true)||std::wstring_view(L"=({*&").find(text[next])!=std::wstring_view::npos){
            const auto statement=statement_end(text,end);if(statement>end)add(begin,statement);
        }
    }
    for(std::size_t index=0;index+3<=text.size();++index)if((text.substr(index).starts_with(L"C++")||text.substr(index).starts_with(L"c++"))&&(!index||!identifier(text[index-1]))){
        auto end=index+3;while(end<text.size()&&digit(text[end]))++end;if(end==text.size()||!identifier(text[end]))add(index,end);index=end-1;
    }
    syntactic(LR"((?:[A-Za-z_][A-Za-z_0-9]*|\d+)\s*(?:&&|\|\||==|!=|<<|>>|[+*/%&|^-]=)\s*(?:[A-Za-z_][A-Za-z_0-9]*|[+-]?\d+(?:[.]\d+)?)(?:[ \t]*;)?)",[&](const auto& match,auto begin,auto end){
        auto value=text.substr(begin,end-begin);while(!value.empty()&&space(value.back()))value.remove_suffix(1);
        if(has(match.str(),LR"(^(?:es|est|is|are)\s*(<<|>>)\s*(?:y|o|and|or|et|ou)$)",true)){
            const auto at=value.find(L"<<")==value.npos?value.find(L">>"):value.find(L"<<");add(begin+at,begin+at+2);
        }else add(begin,end);
    });
    syntactic(LR"([A-Za-z_][A-Za-z_0-9]*[ \t]*=(?!=))",[&](const auto&,auto begin,auto end){const auto statement=statement_end(text,end);if(statement>end)add(begin,statement);});
    syntactic(LR"((?:if|for|while|switch|catch)[ \t]*\()",[&](const auto&,auto begin,auto end){
        const auto condition=balanced_end(text,end-1,L'(',L')');if(condition==end-1)return;const auto body=skip_spaces(text,condition);
        const auto last=at(text,body,L'{')?balanced_end(text,body,L'{',L'}'):statement_end(text,body);if(last>body)add(begin,last);
    });
    syntactic(LR"(return\s+(?:[−+-]?\d+(?:[.]\d+)?|true|false|nullptr|"(?:\\.|[^"\\])*"))",[&](const auto&,auto begin,auto end){const auto next=skip_spaces(text,end);add(begin,at(text,next,L';')?next+1:end);});
    syntactic(LR"(([A-Za-z_][A-Za-z_0-9]*)\()",[&](const auto& match,auto begin,auto end){
        if(word_in(match[1].str(),L"sin cos tan log ln sqrt exp f g h"))return;
        const auto balanced=balanced_end(text,end-1,L'(',L')');if(balanced==end-1)return;const auto next=skip_spaces(text,balanced);if(at(text,next,L';'))add(begin,next+1);
    });
    // Symbol ownership mirrors the original-language frontends. Compound signs
    // keep exact ranges; semicolons require their shared grammatical rule.
    const bool spanish=locale!="fr"&&locale!="en"&&locale!="en-US"&&locale!="en-GB";
    const auto formula_character=[](wchar_t value){return digit(value)||space(value)||(value>=L'a'&&value<=L'z')||(value>=L'A'&&value<=L'Z')||(value>=L'α'&&value<=L'ω')||std::wstring_view(L".,;:π∞=<>≤≥≠≈→+−*/×÷·()[]{}|?²³⁴⁵⁶⁷⁸⁹⁰¹^_%!√-").find(value)!=std::wstring_view::npos;};
    bool adjoining_ascii=false;for(std::size_t index=1;index<text.size();++index){const auto ascii=[](wchar_t value){return (value>=L'A'&&value<=L'Z')||(value>=L'a'&&value<=L'z');};if(ascii(text[index-1])&&ascii(text[index])){adjoining_ascii=true;break;}}
    bool technical_number=false;syntactic(LR"((?:0[xX][0-9a-fA-F]+[uUlL]*|0[bB][01]+[uUlL]*|\d+(?:\.\d+)?[eE][+-]?\d+[fF]?|\d+(?:\.\d+)?[uUlLfF]+))",[&](const auto&,auto,auto end){if(end==text.size()||!identifier(text[end]))technical_number=true;});
    const bool pure_formula=spanish&&!technical_number&&!adjoining_ascii&&std::ranges::all_of(text,formula_character)&&
        std::ranges::any_of(text,[](wchar_t value){return letter(value)||digit(value);})&&!has(text,LR"(::|<=>|<<=|>>=|&&|\|\||==|!=|\+\+|--|[+*/%&|^-]=|\.\.|!(?=[A-Za-z_(]))");
    auto observed_code=frontend_code;
    const auto observe=[&](std::size_t first,std::size_t last){if(first<last&&last<=text.size())std::fill(observed_code.begin()+first,observed_code.begin()+last,true);};
    if(spanish&&!pure_formula){
        // SpanishTechnicalSpeech observes readCode's original atoms, not a
        // guessed language or a rewritten lesson. Its unmarked local calls and
        // balanced groups supply symbol context without marking adjacent prose.
        for(auto index=std::size_t{0};index<text.size();){
            if(overlaps({index,index+1},result.protected_ranges)){++index;continue;}
            if(text[index]==L'"'||text[index]==L'\''){
                const auto end=spanish_quote_end(text,index);if(end==index){++index;continue;}
                const auto inside=text.substr(index+1,end-index-2),whole=text.substr(index,end-index);
                const bool literal=text[index]==L'\''?(inside.size()==1||inside.starts_with(L'\\')):(inside.find(L'\\')!=inside.npos||inside.find_first_of(L"{}[]&|#*_%")!=inside.npos);
                const bool literal_boundary=!index||!identifier(text[index-1]);
                if((literal&&literal_boundary)||observed_code[index]){if(whole.find_first_of(L"#{}[];&|_^~\\<>+*/%=-!")!=whole.npos)add(index,end);index=end;continue;}
                ++index;continue;
            }
            if(text[index]==L'{'||text[index]==L'['){const auto end=spanish_balanced_end(text,index,text[index],text[index]==L'{'?L'}':L']');if(end>index&&!observed_code[index])observe(index,end);++index;continue;}
            if(text[index]==L'<'){const auto end=text.find(L'>',index+1);if(end!=text.npos&&word_in(text.substr(index+1,end-index-1),L"iostream string string_view vector array sstream stdexcept limits iomanip ios utility algorithm cmath cstdint cassert format fstream optional map set memory"))observe(index,end+1);}
            if((letter(text[index])||text[index]==L'_')&&(!index||!identifier(text[index-1])||observed_code[index])){
                const auto begin=index++;while(index<text.size()&&identifier(text[index]))++index;const auto word=text.substr(begin,index-begin);
                const bool ascii=std::ranges::all_of(word,[](wchar_t value){return value<128;});
                const bool underscored=ascii&&word.find(L'_')!=word.npos&&std::ranges::any_of(word,[](wchar_t value){return letter(value)||digit(value);});
                if(underscored)add(begin,index);
                if(!observed_code[begin]){
                    const auto declaration=spanish_declaration_end(text,begin);if(declaration>begin)observe(begin,spanish_suffix_end(text,declaration));
                    auto end=index;bool qualified=false;
                    while(text.substr(end).starts_with(L"::")&&end+2<text.size()&&(letter(text[end+2])||text[end+2]==L'_')){qualified=true;end+=3;while(end<text.size()&&identifier(text[end]))++end;}
                    auto next=end;while(next<text.size()&&space(text[next]))++next;
                    if((qualified||word_in(word,L"vector array map set optional numeric_limits static_cast dynamic_cast reinterpret_cast const_cast"))&&at(text,next,L'<')){
                        const auto balanced=spanish_balanced_end(text,next,L'<',L'>');if(balanced>next){end=balanced;next=skip_whitespace(text,end);}}
                    const bool call=at(text,next,L'(')&&(qualified||underscored||((next==index||word_in(word,L"for while if switch catch main"))&&
                        (word.size()>1||word_in(word,L"for while if switch catch main"))&&!word_in(word,L"sin cos tan cot sec csc arcsin arccos arctan sinh cosh tanh log ln exp sqrt abs")));
                    if(call||((qualified||underscored)&&next<text.size()&&std::wstring_view(L"{[").find(text[next])!=std::wstring_view::npos)){
                        const auto opening=text[next];const auto balanced=spanish_balanced_end(text,next,opening,opening==L'('?L')':opening==L'{'?L'}':L']');if(balanced>next)end=balanced;}
                    if(qualified||underscored||end>index)observe(begin,spanish_suffix_end(text,end));
                }continue;
            }++index;
        }
        syntactic(LR"(\d+(?:\.\d+)?[eE][+-]\d+[fF]?)",[&](const auto&,auto first,auto last){add(first,last);});
    }
    static constexpr std::wstring_view operators[]={L"<=>",L"<<=",L">>=",L"->*",L"::",L"##",L"&&",L"||",L"==",L"!=",L"<=",L">=",L"<<",L">>",L"++",L"--",L"+=",L"-=",L"*=",L"/=",L"%=",L"&=",L"|=",L"^=",L"->",L".*"};
    for(auto index=std::size_t{0};index<text.size();++index){
        if(overlaps({index,index+1},result.protected_ranges))continue;
        // A language label's plus signs never acquire the operator backdrop.
        if((text.substr(index).starts_with(L"C++")||text.substr(index).starts_with(L"c++"))&&(!index||!identifier(text[index-1]))){index+=2;continue;}
        bool compound=false;
        for(const auto op:operators)if(text.substr(index).starts_with(op)){
            const auto before=tail(text,index,80),after=text.substr(index+op.size());
            const bool cue=has(before,LR"(\b(?:símbolo|signo|carácter|operador|marca|symbol|sign|character|operator|token|symbole|signe|caractère|opérateur)s?\b[^.!?;\r\n]{0,64}$)",true);
            if(spanish&&(op==L"->"||op==L"->*"||op==L".*")&&!cue)continue;
            bool selected=true;
            if(spanish&&op==L"::")selected=cue;
            if(locale=="fr"&&op==L"##")selected=cue;
            if(locale=="fr"&&character_literal[index])selected=cue;
            if(!spanish&&locale!="fr"){
                const auto quoted=[&]{if(!index||index+op.size()>=text.size())return false;const auto left=text[index-1],right=text[index+op.size()];return (left==L'\''&&right==L'\'')||(left==L'"'&&right==L'"')||(left==L'‘'&&right==L'’')||(left==L'“'&&right==L'”');};
                const bool operator_cue=has(tail(text,index,32),LR"(\b(?:operators?|compound assignment)\s*$)",true)||begins(after.substr(0,20),LR"(\s+operators?\b)",true);
                if(op==L"--"){
                    auto trimmed=text;while(!trimmed.empty()&&space(trimmed.front()))trimmed.remove_prefix(1);while(!trimmed.empty()&&space(trimmed.back()))trimmed.remove_suffix(1);
                    const bool prefix=!index||space(text[index-1])||std::wstring_view(L"([=,:&|?{;").find(text[index-1])!=std::wstring_view::npos;
                    const bool suffix=after.empty()||space(after.front())||std::wstring_view(L")],;}:<>=+*/%&|^").find(after.front())!=std::wstring_view::npos;
                    const bool spaced=index&&space(text[index-1])&&!after.empty()&&space(after.front());
                    selected=frontend_code[index]||quoted()||operator_cue||trimmed==L"--"||(!spaced&&((prefix&&!after.empty()&&identifier(after.front()))||(suffix&&index&&identifier(text[index-1]))));
                }else if(op==L"##"||op==L"<="||op==L">=")selected=cue;
                else if(word_in(op,L"<<= >>= += -= *= /= %= &= |= ^=")){
                    auto left=text.substr(0,index);while(!left.empty()&&space(left.back()))left.remove_suffix(1);auto right=after;while(!right.empty()&&space(right.front()))right.remove_prefix(1);
                    const bool binary=!left.empty()&&!right.empty()&&(identifier(left.back())||std::wstring_view(L")]'\"").find(left.back())!=std::wstring_view::npos)&&
                        (identifier(right.front())||std::wstring_view(L"(['\"_!~*&+-").find(right.front())!=std::wstring_view::npos);
                    selected=cue||(!closed_literal[index]&&(frontend_code[index]||binary||operator_cue));
                }
            }
            if(selected&&!pure_formula)add(index,index+op.size());index+=op.size()-1;compound=true;break;
        }if(compound)continue;
        if(text[index]==L';'){
            bool selected=explicit_semicolon(text,index)||(spanish&&observed_code[index]);
            if(!spanish){auto trimmed=text;while(!trimmed.empty()&&space(trimmed.front()))trimmed.remove_prefix(1);while(!trimmed.empty()&&space(trimmed.back()))trimmed.remove_suffix(1);
                const auto before=tail(text,index,64),after=text.substr(index+1,std::min<std::size_t>(32,text.size()-index-1));
                if(locale=="fr"){
                    auto left=index,right=index+1;while(left&&space(text[left-1]))--left;while(right<text.size()&&space(text[right]))++right;
                    const wchar_t quote=left?text[left-1]:L'\0';const wchar_t close=quote==L'“'?L'”':quote==L'«'?L'»':quote;
                    const bool quoted_alone=left&&right<text.size()&&std::wstring_view(L"\"“«`").find(quote)!=std::wstring_view::npos&&text[right]==close;
                    bool frontend=quoted_alone;
                    if(!double_literal[index]&&!character_literal[index]){
                        frontend=frontend||frontend_code[index]||trimmed==L";"||at_word(before,LR"((?:symbole|signe|caractère|terminateur|opérateur)\s*$)",true)||
                            at_word(before,LR"((?:termin(?:e|er|ent|ée?|ées?|és?)|fin(?:it|ir|issent))\s+par\s*$)",true);
                        auto rest=text.substr(index+1);while(!rest.empty()&&space(rest.front()))rest.remove_prefix(1);
                        const bool action=at_word(before,LR"((?:ajout(?:e|er|ez|ons|ent)|mets|mettre|mettez|écris|écrire|écrivez|ins(?:ère|érer|érez)|place(?:r|z)?|supprim(?:e|er|ez)|retir(?:e|er|ez))(?:\s+pas)?\s*$)",true);
                        frontend=frontend||(action&&(rest.empty()||std::wstring_view(L".,!?").find(rest.front())!=std::wstring_view::npos||begins(rest,LR"((?:à|avant|après|dans|pour|entre|sur|comme|ici)(?=\s|$|[.,:;!?]))",true)));
                    }selected=selected||frontend;
                }else{
                    const bool quoted=index&&index+1<text.size()&&((text[index-1]==L'\''&&text[index+1]==L'\'')||(text[index-1]==L'"'&&text[index+1]==L'"')||(text[index-1]==L'‘'&&text[index+1]==L'’')||(text[index-1]==L'“'&&text[index+1]==L'”'));
                    const bool action=has(before,LR"(\b(?:add|append|insert|remove|delete|omit|write|type|use|place|put|include|with|without|semicolon|symbol|character|token|sign|terminator|punctuation(?: mark)?)\s*$)",true);
                    const bool subject=(!index||has(before,LR"(\b(?:the|a|an|this|that|each|every)\s*$)",true))&&begins(after,LR"(\s+(?:symbols?|characters?|tokens?|signs?|terminators?|punctuation|terminates?|separates?)\b)",true);
                    selected=selected||frontend_code[index]||quoted||trimmed==L";"||action||subject;
                }
            }
            if(selected)add(index,index+1);continue;}
        const auto before=tail(text,index,80);
        const bool named_symbol=std::wstring_view(L"#{}[]&|_^~\\<>+*/%=-!").find(text[index])!=std::wstring_view::npos&&
            has(before,LR"(\b(?:símbolo|signo|carácter|operador|marca|symbol|sign|character|operator|token|symbole|signe|caractère|opérateur)s?\b[^.!?;\r\n]{0,64}$)",true);
        if(!pure_formula&&spanish&&std::wstring_view(L"{}[]&|#_*").find(text[index])!=std::wstring_view::npos)add(index,index+1);
        else if(!spanish&&std::wstring_view(L"#{}[]").find(text[index])!=std::wstring_view::npos)add(index,index+1);
        bool negation=false,english_bang=false;
        if(text[index]==L'!'){
            negation=index+1<text.size()&&(identifier(text[index+1])||std::wstring_view(L"_(!").find(text[index+1])!=std::wstring_view::npos)&&
                (!index||space(text[index-1])||std::wstring_view(L"(=,;{?:&|").find(text[index-1])!=std::wstring_view::npos);
            if(locale=="fr"){const SyntaxProjection prefix_source(text.substr(0,index),false),suffix_source(text.substr(index+1),false);
                negation=!double_literal[index]&&!character_literal[index]&&(negation||frontend_code[index]||has(prefix_source.text,LR"((?:opérateurs?|operators?)\s*$)",true)||
                    ((!index||!digit(text[index-1]))&&begins(suffix_source.text,LR"(std\s*::)")));}
            auto first=index,last=index+1;while(first&&text[first-1]==L'!')--first;while(last<text.size()&&text[last]==L'!')++last;
            const bool prefix=!first||space(text[first-1])||std::wstring_view(L"([=,:&|?{;").find(text[first-1])!=std::wstring_view::npos;
            english_bang=frontend_code[index]||(prefix&&last<text.size()&&(identifier(text[last])||text[last]==L'('))||
                (index&&index+1<text.size()&&((text[index-1]==L'\''&&text[index+1]==L'\'')||(text[index-1]==L'"'&&text[index+1]==L'"')||(text[index-1]==L'‘'&&text[index+1]==L'’')||(text[index-1]==L'“'&&text[index+1]==L'”')));
            if((spanish||locale=="fr")&&negation)add(index,index+1);
        }
        bool named_minus=true;if(text[index]==L'-'){
            const bool quoted=index&&index+1<text.size()&&((text[index-1]==L'\''&&text[index+1]==L'\'')||(text[index-1]==L'"'&&text[index+1]==L'"')||(text[index-1]==L'‘'&&text[index+1]==L'’')||(text[index-1]==L'“'&&text[index+1]==L'”'));
            const bool numeric=index+1<text.size()&&digit(text[index+1])&&(!index||space(text[index-1])||digit(text[index-1])||std::wstring_view(L"([=+*/%&|^<>!?:,;{").find(text[index-1])!=std::wstring_view::npos);
            named_minus=frontend_code[index]||quoted||numeric||has(tail(text,index,32),LR"(\b(?:unary|binary|operator|minus|negative)\s*$)",true)||begins(text.substr(index+1,std::min<std::size_t>(18,text.size()-index-1)),LR"(\s+(?:signs?|tokens?|operators?)\b)",true);
        }
        if(named_symbol&&named_minus&&(text[index]!=L'!'||(spanish?negation:locale=="fr"?negation:english_bang))&&std::wstring_view(L"#{}[]&|_^~\\<>+*/%=-!").find(text[index])!=std::wstring_view::npos)add(index,index+1);
        if(spanish&&!pure_formula&&observed_code[index]&&std::wstring_view(L"#{}[];&|_^~\\<>+*/%=-!").find(text[index])!=std::wstring_view::npos)add(index,index+1);
        if(spanish&&(!index||!identifier(text[index-1]))&&text[index]==L'\\'&&index+1<text.size()&&std::wstring_view(L"ntr0abfv").find(text[index+1])!=std::wstring_view::npos&&
            (index+2==text.size()||!letter(text[index+2]))) {add(index,index+2);++index;}
    }
    std::sort(result.fragments.begin(),result.fragments.end());std::vector<Range> merged;
    for(const auto range:result.fragments){
        if(!merged.empty()&&(range.first<=merged.back().second||std::ranges::all_of(text.substr(merged.back().second,range.first-merged.back().second),[](wchar_t value){return value==L' '||value==L'\t';})))merged.back().second=std::max(merged.back().second,range.second);
        else merged.push_back(range);
    }
    result.fragments.clear();for(const auto& range:merged){auto begin=range.first;
        for(const auto& [first,last]:own_code.wrappers)if(first<range.second&&last>range.first){
            if(begin<first&&!std::ranges::all_of(text.substr(begin,first-begin),space))result.fragments.emplace_back(begin,first);begin=std::max(begin,last);}
        if(begin<range.second&&!std::ranges::all_of(text.substr(begin,range.second-begin),space))result.fragments.emplace_back(begin,range.second);
    }return result;
}
}
