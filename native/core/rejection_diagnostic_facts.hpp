#pragma once

#include "core/code_format_compat.hpp"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {

// A Spanish message can vary in wording while its observable facts stay fixed.
// This verifier is deliberately independent of native execution evidence: a
// caller must also prove the compiled source, stdout, exit status and stream
// parameter, rather than grading a plausible message on its own.
enum class RejectionOperation { boxes, balance };
struct RejectionFacts {
    int request = 0;
    int first = 0;  // pieces for boxes, charges for balance
    int second = 0; // refunds for balance
};
struct RejectionReview {
    bool correct = false;
    std::string reason;
};

namespace rejection_detail {
struct Word {
    std::string text;
    std::optional<int> number;
    std::size_t start = 0, end = 0; // offsets in the folded diagnostic
};
inline bool letter(char value) { return value >= 'a' && value <= 'z'; }
inline bool digit(char value) { return value >= '0' && value <= '9'; }
inline std::string fold_spanish(std::string_view source) {
    std::string out;
    out.reserve(source.size());
    for (std::size_t i=0; i<source.size(); ++i) {
        const auto c=static_cast<unsigned char>(source[i]);
        if(c==0xC3 && i+1<source.size()) {
            const auto mark=static_cast<unsigned char>(source[i+1]);
            char folded=0;
            switch(mark) {
            case 0x81: case 0xA1: folded='a'; break;
            case 0x89: case 0xA9: folded='e'; break;
            case 0x8D: case 0xAD: folded='i'; break;
            case 0x93: case 0xB3: folded='o'; break;
            case 0x9A: case 0xBA: folded='u'; break;
            case 0x9C: case 0xBC: folded='u'; break;
            case 0x91: case 0xB1: folded='n'; break;
            default: break;
            }
            if(folded){out+=folded; ++i; continue;}
        }
        // Common typographic hyphens/dashes and mathematical minus are
        // equivalent to ASCII '-' in a written integer or inclusive range.
        // Fold one complete UTF-8 scalar at a time: this also keeps Word
        // offsets consistent with the punctuation inspected by range().
        if(c==0xE2 && i+2<source.size()) {
            const auto second=static_cast<unsigned char>(source[i+1]);
            const auto third=static_cast<unsigned char>(source[i+2]);
            if((second==0x80 && third>=0x90 && third<=0x95) ||
                    (second==0x88 && third==0x92)) {
                out+='-';i+=2;continue;
            }
        }
        if(c>='A'&&c<='Z')out+=static_cast<char>(c-'A'+'a');
        else if(c<128)out+=static_cast<char>(c);
        else out+=' ';
    }
    return out;
}
inline std::vector<Word> words(std::string_view source) {
    const auto folded=fold_spanish(source);
    std::vector<Word> result;
    for (std::size_t i=0;i<folded.size();) {
        const auto beginning=i;
        if(letter(folded[i])) {
            while(i<folded.size()&&letter(folded[i]))++i;
            result.push_back({folded.substr(beginning,i-beginning),{},beginning,i});
        } else if(digit(folded[i])||
                  ((folded[i]=='-'||folded[i]=='+')&&i+1<folded.size()&&digit(folded[i+1]))) {
            if(folded[i]=='+'||folded[i]=='-')++i;
            while(i<folded.size()&&digit(folded[i]))++i;
            int value=0;auto number=std::string_view(folded).substr(beginning,i-beginning);
            if(number.starts_with('+'))number.remove_prefix(1);
            const auto parsed=std::from_chars(number.data(),number.data()+number.size(),value);
            if(parsed.ec==std::errc{}&&parsed.ptr==number.data()+number.size())
                result.push_back({{},value,beginning,i});
            else result.push_back({"invalid-number",{},beginning,i});
        } else ++i;
    }
    return result;
}
inline bool one_of(std::string_view word,std::initializer_list<std::string_view> names) {
    for(auto name:names)if(word==name)return true;
    return false;
}
inline bool mentions(const std::vector<Word>& words,std::initializer_list<std::string_view> names) {
    for(const auto& word:words)if(one_of(word.text,names))return true;
    return false;
}
// Word offsets belong to fold_spanish(source), not the original UTF-8 bytes.
// Keep numeric ranges such as 0..20 inside their clause.
inline bool clause_boundary(std::string_view folded,const Word& left,
        const Word& right,bool comma=false) {
    for(auto at=left.end;at<right.start;++at) {
        const auto character=folded[at];
        if(character==';'||character=='!'||character=='?'||character=='\n'||
                (comma&&character==','))return true;
        if(character=='.'&&(at==0||folded[at-1]!='.')&&
                (at+1==folded.size()||folded[at+1]!='.'))return true;
    }
    return false;
}
inline bool labelled(const std::vector<Word>& words,std::initializer_list<std::string_view> names,int expected) {
    bool found=false;
    for(std::size_t i=0;i<words.size();++i)if(one_of(words[i].text,names)) {
        // Allow "petición número 2", "cobros: 10" and "piezas recibidas -1".
        // Do not stop at the first correct occurrence: "peticion 1 ...
        // peticion 9" contradicts the observed facts even though one label
        // happens to match. An unnumbered prose reference is not a value.
        for(std::size_t j=i+1;j<words.size()&&j<=i+3;++j) {
            if(words[j].number){
                if(*words[j].number!=expected)return false;
                found=true;
                break;
            }
            if(!one_of(words[j].text,{"numero","num","de","del","recibidas","recibidos","son","es"}))break;
        }
    }
    return found;
}
inline bool range(std::string_view original,const std::vector<Word>& words,int upper,int lower_bound=0) {
    // The interval is a relationship, not two unrelated numeric fields.
    // In particular, "cobros=10 devoluciones=0 9" is NOT a statement
    // that the valid domain is 0..9. Require interval language and syntax.
    const auto source=fold_spanish(original);
    // Do not award a valid domain merely because its two numbers appeared.
    // "rango no permitido 0 a 9" denies the permitted interval. Likewise,
    // "no se admiten enteros entre 0 y 9" cannot be graded as instructions
    // giving the actual accepted range. Keep these checks attached to range
    // nouns, not an unrelated "piezas=-1 no válida" observation.
    const auto contradicts_allowed_domain=[&]() {
        const auto is_range=[](std::string_view value) {
            // "límite no permitido 21" and "valores no permitidos"
            // may describe rejected data, not the permitted domain.
            // Only a named domain can carry a global contradiction.
            return one_of(value,{"rango","intervalo","dominio"});
        };
        const auto invalid=[](std::string_view value) {
            return one_of(value,{"prohibido","prohibida","prohibidos",
                "prohibidas","invalido","invalida","invalidos","invalidas",
                "incorrecto","incorrecta","incorrectos","incorrectas",
                "excluido","excluida","excluidos","excluidas"});
        };
        const auto denied=[](std::string_view value) {
            return one_of(value,{"permitido","permitida","permitidos",
                "permitidas","valido","valida","validos","validas",
                "admitido","admitida","admitidos","admitidas",
                "admite","admiten","acepta","aceptan","permite","permiten"});
        };
        for(std::size_t i=0;i<words.size();++i) {
            if(is_range(words[i].text)) {
                for(auto j=i+1;j<words.size()&&j<=i+4;++j) {
                    if(words[j].number)break;
                    if(invalid(words[j].text))return true;
                    if(!one_of(words[j].text,{"no","nunca","jamas"}))
                        continue;
                    for(auto k=j+1;k<words.size()&&k<=j+3;++k) {
                        if(words[k].number)break;
                        if(denied(words[k].text))return true;
                    }
                }
            }
            if(one_of(words[i].text,{"no","nunca","jamas"})&&
                    i+2<words.size()&&
                    one_of(words[i+1].text,{"se"})&&
                    one_of(words[i+2].text,{"admite","admiten","acepta",
                        "aceptan","permite","permiten"})) {
                // A denied range noun must immediately belong to the
                // negated verb, not to a later, separate valid statement.
                const auto end=std::min(words.size(),i+7);
                for(auto j=i+3;j<end;++j) {
                    if(words[j].number)break;
                    if(is_range(words[j].text))return true;
                    // A negated category matters only when it is followed
                    // by this actual bounded range, not when the message
                    // says "no se admiten valores -1" and then gives the
                    // true 0..9 domain in a separate clause.
                    if(one_of(words[j].text,{"enteros","valores"})&&
                            j+4<words.size()&&
                            one_of(words[j+1].text,{"entre","de","del"})&&
                            words[j+2].number==lower_bound&&
                            one_of(words[j+3].text,{"a","al","y","hasta"})&&
                            words[j+4].number==upper)
                        return true;
                }
            }
        }
        return false;
    };
    if(contradicts_allowed_domain())return false;
    // Range words may precede or follow the numeric interval. For example,
    // both "rango permitido de 0 a 20" and "0 a 20 es el rango permitido"
    // communicate the same bound. A standalone pair of numbers is not enough.
    const auto describes_range=[&](std::size_t at,std::size_t end) {
        // Nearby numbers in a *different sentence* do not belong to
        // this range, even when an earlier sentence said "rango 0..9".
        // Honor sentence/statement boundaries when looking for a noun.
        const auto same_clause=[&](std::size_t left,std::size_t right) {
            if(words[left].end>words[right].start)return false;
            const auto intervening=std::string_view(source).substr(
                words[left].end,words[right].start-words[left].end);
            return intervening.find_first_of(".;\n")==intervening.npos;
        };
        const auto first=at>5?at-5:0;
        for(std::size_t i=first;i<at;++i) {
            if(!same_clause(i,at))continue;
            if(one_of(words[i].text,{"rango","intervalo","dominio","limite","limites",
                    "entero","enteros","entre","desde","permitido","permitidos",
                    "permitida","permitidas","admite","admiten","acepta","aceptan",
                    "valido","validos","valida","validas","debe","deben"}))return true;
        }
        for(std::size_t i=end+1;i<words.size()&&i<=end+5;++i) {
            if(!same_clause(end,i))continue;
            if(one_of(words[i].text,{"rango","intervalo","dominio","limite","limites",
                    "entero","enteros","permitido","permitidos","permitida",
                    "permitidas","valido","validos","valida","validas"}))return true;
        }
        return false;
    };
    // Any explicitly named bound must agree with the contract even if a
    // valid 0..N spelling occurs elsewhere in the same diagnostic. Check
    // contradictions before accepting the compact interval expression.
    // Also recognize a genuinely bounded domain stated by its two named
    // endpoints: "mínimo 0; máximo 9" or "límite inferior 0, límite
    // superior 9". Both bounds are necessary; a pair of unlabeled observed
    // values or a single lower limit is not a complete allowed interval.
    bool lower_found=false,upper_found=false;
    for(std::size_t i=0;i<words.size();++i) {
        const auto& word=words[i].text;
        const bool lower=one_of(word,{"minimo","minima"})||
            (word=="inferior"&&i>0&&
                one_of(words[i-1].text,{"limite","limites"}));
        const bool higher=one_of(word,{"maximo","maxima"})||
            (word=="superior"&&i>0&&
                one_of(words[i-1].text,{"limite","limites"}));
        if(!lower&&!higher)continue;
        std::size_t j=i+1;
        // Permit ordinary filler between an endpoint and its integer,
        // but never skip another number or an unrelated field label.
        while(j<words.size()&&j<=i+3&&one_of(words[j].text,
                {"es","de","del","permitido","permitida","admitido","admitida"}))
            ++j;
        if(j>=words.size()||j>i+3||!words[j].number)return false;
        if(lower) {
            if(*words[j].number!=lower_bound)return false;
            lower_found=true;
        }else{
            if(*words[j].number!=upper)return false;
            upper_found=true;
        }
    }

    bool found_allowed_interval=false;
    for(std::size_t i=0;i<words.size();++i)if(words[i].number) {
        // Inspect every explicitly described interval, not just the first
        // correct 0..N. "rango 0..9; rango permitido 0..8" contradicts
        // itself even though a valid pair appeared earlier.
        std::size_t end=i;
        bool interval=false;
        std::int64_t stated_upper=0;
        if(i+2<words.size()&&one_of(words[i+1].text,{"a","al","y","hasta"})
                &&words[i+2].number) {
            end=i+2;interval=true;stated_upper=*words[i+2].number;
        }else if(i+1<words.size()) {
            const auto& next=words[i+1];
            if(next.number&&*next.number<0&&
                    next.start==words[i].end&&
                    next.start<source.size()&&source[next.start]=='-') {
                // No spaces in "0-9": the shared lexer treats "-9" as
                // one signed number. It is the upper bound of a hyphenated
                // range, not a negative upper limit.
                // Widen before negating: INT_MIN is a valid token, but its
                // positive magnitude does not fit in the original int type.
                interval=true;stated_upper=-static_cast<std::int64_t>(*next.number);
            }else if(next.number&&next.start>=words[i].end) {
                const auto connector=std::string_view(source).substr(words[i].end,
                    next.start-words[i].end);
                const auto trim=[](std::string_view value) {
                    while(!value.empty()&&std::isspace(
                            static_cast<unsigned char>(value.front())))
                        value.remove_prefix(1);
                    while(!value.empty()&&std::isspace(
                            static_cast<unsigned char>(value.back())))
                        value.remove_suffix(1);
                    return value;
                };
                const auto punctuation=trim(connector);
                // "0 - 9" is an ordinary inclusive range when presented
                // with interval wording, just as "0-9" and "0..9" are.
                // A bare comma is not enough: accept "[0, 9]" only if
                // both enclosing brackets are really present, so two data
                // fields in prose cannot impersonate a range.
                bool bracketed=false;
                if(punctuation==",") {
                    const auto before=trim(std::string_view(source).substr(
                        0,words[i].start));
                    const auto after=trim(std::string_view(source).substr(
                        next.end));
                    bracketed=!before.empty()&&before.back()=='['&&
                        !after.empty()&&after.front()==']';
                }
                interval=punctuation=="-"||bracketed||
                    connector.find("..")!=connector.npos;
                if(interval)stated_upper=*next.number;
            }
            if(interval)end=i+1;
        }
        if(interval&&describes_range(i,end)) {
            if(*words[i].number!=lower_bound||stated_upper!=upper)
                return false;
            // The qualifier can follow the written bounds:
            // "rango 0..9 no válido" contradicts "permitido 0..9".
            // Do not confuse this with "0..9; no se permite 21":
            // only deny an immediate adjective about the interval.
            const auto prohibited=[](std::string_view value) {
                return one_of(value,{"prohibido","prohibida","prohibidos",
                    "prohibidas","invalido","invalida","invalidos",
                    "invalidas","incorrecto","incorrecta","incorrectos",
                    "incorrectas","excluido","excluida"});
            };
            const auto qualified=[](std::string_view value) {
                return one_of(value,{"permitido","permitida","permitidos",
                    "permitidas","valido","valida","validos","validas",
                    "admitido","admitida","aceptado","aceptada"});
            };
            if(end+1<words.size()&&prohibited(words[end+1].text))
                return false;
            if(end+2<words.size()&&
                    one_of(words[end+1].text,{"no","nunca","jamas"})&&
                    (qualified(words[end+2].text)||
                     (end+3<words.size()&&
                      one_of(words[end+2].text,{"es","esta","son","estan"})&&
                      qualified(words[end+3].text))))
                return false;
            found_allowed_interval=true;
        }
    }
    return found_allowed_interval||(lower_found&&upper_found);
}
// Only real infinitives/imperatives and their direct-object pronouns request
// corrective action. A shared root alone also matches nouns ("revisor"),
// adjectives ("verificable") and abstract terms ("revisionismo") which are
// NOT instructions to repair the learner's rejected input. Keep nominal
// "revisión necesaria" requests separate in actionable().
inline bool corrective_instruction_verb(std::string_view word) {
    constexpr std::string_view forms[]{
        "revisar","revisa","revise","revisen","revisad","revisemos",
        "verificar","verifica","verifique","verifiquen","verificad","verifiquemos",
        "comprobar","comprueba","compruebe","comprueben","comprobad","comprobemos",
        "corregir","corrige","corrija","corrijan","corregid","corrijamos",
        "rectificar","rectifica","rectifique","rectifiquen","rectificad","rectifiquemos",
        "modificar","modifica","modifique","modifiquen","modificad","modifiquemos",
        "ajustar","ajusta","ajuste","ajusten","ajustad","ajustemos"
    };
    for(const auto form:forms) {
        if(!word.starts_with(form))continue;
        const auto rest=word.substr(form.size());
        if(rest.empty()||one_of(rest,{"lo","la","los","las"}))return true;
    }
    return false;
}
inline bool actionable(const std::vector<Word>& words) {
    for(std::size_t i=0;i<words.size();++i){
        const bool nominal_request=one_of(words[i].text,
            {"revision","verificacion","comprobacion",
             "rectificacion","modificacion"});
        if(!nominal_request&&!corrective_instruction_verb(words[i].text))
            continue;
        // An affirmative nominal request ("verificacion necesaria") also
        // directs the reader to review rejected data. It is not the same as
        // reporting a completed verification or saying it is unnecessary.
        if(nominal_request) {
            const bool necessary=i+1<words.size()&&one_of(words[i+1].text,
                {"necesaria","necesario","obligatoria","obligatorio",
                 "requerida","requerido"});
            // "Se requiere una revision" and "es necesaria una revision"
            // are instructions too. Do not require the adjective to follow
            // its noun, but do require a nearby affirmative modal expression.
            const auto modal=[](std::string_view value) {
                return one_of(value,{"requiere","requieren","recomienda",
                    "recomiendan","aconseja","aconsejan","solicita",
                    "solicite","necesaria","necesario","obligatoria",
                    "obligatorio","haga","realice"});
            };
            const bool modal_before=
                (i&&modal(words[i-1].text))||
                (i>=2&&one_of(words[i-1].text,{"una","un","la","el"})&&
                    modal(words[i-2].text));
            bool denied=i&&one_of(words[i-1].text,{"no","sin","ninguna","ningun"});
            const auto start=i>7?i-7:0;
            for(std::size_t j=start;j<i;++j) {
                if(one_of(words[j].text,{"ya","hizo","hicieron","realizo",
                        "realizaron","completo","completaron","fue","estaba"}))
                    denied=true;
                if(!one_of(words[j].text,{"no","sin","nunca","jamas"}))continue;
                // A negated modal belongs to the same instruction only if
                // its intervening words are grammatical auxiliaries. For
                // example "no se recomienda una revision" is negated, while
                // "no modifique el informe; revision necesaria" is not.
                bool same_clause=true;
                for(std::size_t k=j+1;k<i;++k)
                    if(!one_of(words[k].text,{"se","es","son","hay","hace",
                        "falta","que","de","una","un","la","el","necesidad",
                        "necesaria","necesario","obligatoria","obligatorio",
                        "hacer","realizar","requiere","requieren","recomienda",
                        "recomiendan","aconseja","aconsejan","conviene"})){
                        same_clause=false;
                        break;
                    }
                if(same_clause)denied=true;
            }
            for(std::size_t j=i+1;j<words.size()&&j<=i+6;++j)
                if(one_of(words[j].text,{"no","nunca","jamas","ya","fue",
                        "realizada","realizado","hecha","hecho","completada",
                        "completado","cancelada","cancelado"}))
                    denied=true;
            if((necessary||modal_before)&&!denied)return true;
            continue;
        }
        // A retrospective report or another noun does not instruct the
        // reader to correct the rejected input. Genuine infinitives and
        // imperatives still count, unless they are negated.
        if(one_of(words[i].text,{
            "revision","revisiones","revisado","revisada","revisados","revisadas",
            "revisando","reviso","revisaron","revisaba","revisabamos",
            "verificacion","verificaciones","verificado","verificada",
            "verificados","verificadas","verificando","verificaron","verificamos",
            "comprobacion","comprobaciones","comprobado","comprobada",
            "comprobados","comprobadas","comprobando","comprobamos","comprobaron",
            "corregido","corregida","corregidos","corregidas","corregimos",
            "rectificacion","rectificaciones","rectificado","rectificada",
            "modificacion","modificaciones","modificado","modificada",
            "ajustes","ajustado","ajustada","ajustados","ajustadas","ajustando"
        }))continue;
        // "No revise" and "sin revisar" describe the opposite of the
        // required corrective instruction. A later positive instruction
        // ("No modifique los hechos; revise los datos") is still accepted.
        if(i&&one_of(words[i-1].text,{"no","sin","nunca","jamas"}))continue;
        const auto start=i>7?i-7:0;
        bool negated=false;
        for(std::size_t j=start;j<i;++j)
            if(one_of(words[j].text,{"no","sin","nunca","jamas"})){
                // The negation may span impersonal advice or a modal clause:
                // "no se recomienda revisar", "no hay que comprobar" and
                // "sin necesidad de volver a verificar" are NOT instructions
                // to correct anything. Do not consume a later affirmative
                // "no modifique los hechos; revise los datos" as negated.
                bool only_auxiliary=true;
                for(std::size_t k=j+1;k<i;++k)
                    if(!one_of(words[k].text,{"se","es","son","hay","hace","falta",
                        "necesario","necesaria","necesidad","preciso","precisa",
                        "de","que","a","volver","debe","deben","puede","pueden",
                        "requiere","requieren","conviene","recomienda","recomiendan",
                        "recomiendo","recomendamos","aconseja","aconsejan","aconsejo",
                        "aconsejamos"})){only_auxiliary=false;break;}
                if(only_auxiliary){negated=true;break;}
            }
        if(!negated)return true;
    }
    return false;
}

// This checks claims made by a diagnostic, independently of its request to
// review the input. A later imperative cannot make an invented action true.
// Lexical classes supply grammatical roles: no individual mention of a
// correction or of inventory is itself an incompatible assertion.
inline bool correction_noun(std::string_view word) {
    return one_of(word,{"correccion","correcciones","rectificacion",
        "rectificaciones","ajuste","ajustes","modificacion","modificaciones"});
}
inline bool input_subject(std::string_view word) {
    return one_of(word,{"dato","datos","entrada","entradas","cantidad",
        "cantidades","valor","valores","pieza","piezas","cobro","cobros",
        "devolucion","devoluciones"});
}
inline bool automatic_word(std::string_view word) {
    return one_of(word,{"automaticamente","automatico","automatica",
        "automaticos","automaticas"});
}
enum class ChangePredicate { none, completed, realization, automatic_present };
inline ChangePredicate change_predicate(std::string_view word) {
    if(one_of(word,{"corregido","corregida","corregidos","corregidas",
            "corrigio","corrigieron","corregi","corregimos",
            "rectificado","rectificada","rectificados","rectificadas",
            "rectifico","rectificaron","ajustado","ajustada","ajustados",
            "ajustadas","ajusto","ajustaron","modificado","modificada",
            "modificados","modificadas","modifico","modificaron"}))
        return ChangePredicate::completed;
    if(one_of(word,{"aplico","aplicaron","aplicado","aplicada","aplicados",
            "aplicadas","realizo","realizaron","realizado","realizada",
            "realizados","realizadas","efectuo","efectuaron","efectuado",
            "efectuada","efectuados","efectuadas","hizo","hicieron",
            "hecho","hecha","hechos","hechas","completo","completaron",
            "completado","completada","completados","completadas"}))
        return ChangePredicate::realization;
    if(one_of(word,{"corrige","corrigen","rectifica","rectifican",
            "ajusta","ajustan","modifica","modifican"}))
        return ChangePredicate::automatic_present;
    return ChangePredicate::none;
}
inline bool claim_link(std::string_view word) {
    return input_subject(word)||correction_noun(word)||automatic_word(word)||
        one_of(word,{"el","la","los","las","un","una","unos","unas",
            "lo","le","les","se","hay","ha","han","habia","habian","fue",
            "fueron","es","son","esta","estan","era","eran","ser",
            "estar","sea","sean","sido","estado","ya","aun","todavia","de","del",
            "a","que","por","solo","solamente","unicamente","ninguna",
            "ningun","ninguno","ningunos","ningunas"});
}
struct ClaimQualification { bool negated=false,modal=false; };
inline ClaimQualification qualify_claim(const std::vector<Word>& words,
        std::size_t begin,std::size_t end,std::size_t predicate) {
    ClaimQualification result;
    // Follow auxiliaries, determiners and the predicate's own subject/object.
    // Another lexical verb ends the scope, so "no revise ... se corrigio"
    // cannot lend its negation to the second predicate.
    for(auto cursor=predicate;cursor>begin;) {
        const auto at=--cursor;
        const auto& word=words[at].text;
        if(one_of(word,{"no","nunca","jamas","sin","ni","ninguna",
                "ningun","ninguno","ningunos","ningunas"})) {
            // Focus can precede the verb ("no solo se corrigio") or qualify
            // its object ("no se corrigio solamente el dato"). Both affirm
            // the action. Stay inside this predicate's grammatical span;
            // a focus word in another clause cannot cancel its negation.
            bool focus=false;
            if(word=="no")for(auto next=at+1;next<end;++next) {
                if(one_of(words[next].text,{"solo","solamente","unicamente"})) {
                    focus=true;break;
                }
                if(next!=predicate&&!claim_link(words[next].text))break;
            }
            if(!focus)result.negated=true;
        }else if(one_of(word,{"debe","deben","deberia","deberian","puede",
                "pueden","podria","podrian","necesario","necesaria",
                "necesarios","necesarias","requiere","requieren",
                "recomienda","recomiendan","solicita","solicitan"}))
            result.modal=true;
        else if(!claim_link(word))break;
    }
    return result;
}
inline bool program_agent(const std::vector<Word>& words,std::size_t begin,
        std::size_t predicate) {
    // Clitics and adverbs can separate the program/reflexive subject from its
    // present-tense verb. Stop at another lexical word instead of borrowing
    // an actor from an unrelated statement or an informal imperative. The
    // caller already excluded negated claims, so a surviving "no" is focus.
    for(auto cursor=predicate;cursor>begin;) {
        const auto& word=words[--cursor].text;
        if(one_of(word,{"se","programa","sistema","cliente"}))return true;
        if(!automatic_word(word)&&!one_of(word,{"lo","la","los","las","le",
                "les","ya","aun","todavia","tambien","no","solo","solamente",
                "unicamente"}))break;
    }
    return false;
}
inline bool correction_object(const std::vector<Word>& words,std::size_t begin,
        std::size_t end,std::size_t predicate) {
    for(auto cursor=predicate;cursor>begin;) {
        const auto& word=words[--cursor].text;
        if(correction_noun(word))return true;
        if(!claim_link(word))break;
    }
    for(auto cursor=predicate+1;cursor<end;++cursor) {
        const auto& word=words[cursor].text;
        if(correction_noun(word))return true;
        if(!claim_link(word))break;
    }
    return false;
}
inline bool inventory_word(std::string_view word) {
    return one_of(word,{"stock","existencias","inventario","disponibilidad",
        "agotado","agotada","agotados","agotadas"});
}
inline bool inventory_state(std::string_view word) {
    return one_of(word,{"hay","habia","habian","existe","existen","falta",
        "faltan","faltaba","faltaban","escasez","sin","agotado","agotada",
        "agotados","agotadas","insuficiente","insuficientes"});
}
inline bool attribution_predicate(const std::vector<Word>& words,std::size_t begin,
        std::size_t end,std::size_t at) {
    const auto& word=words[at].text;
    if(one_of(word,{"atribuye","atribuyen","atribuyo","atribuyeron",
            "atribuido","atribuida","atribuibles","atribuir","atribuya",
            "atribuyan","provoca","provocan","provoco","provocaron",
            "provocado","provocada","causo","causaron","causado","causada"}))return true;
    if(word=="debe"&&at>begin&&words[at-1].text=="se"&&
            at+1<end&&words[at+1].text=="a")return true;
    if(one_of(word,{"es","son","fue","eran"})) {
        for(auto cursor=begin;cursor<end;++cursor)
            if(one_of(words[cursor].text,{"causa","motivo","razon"}))return true;
        // The rejection may have an implicit subject: "no es falta de..."
        // and "el rechazo no es por..." deny a causal identification. Bind
        // that complement to its copula before looking for a "por" relation;
        // otherwise the noun "rechazo" can be mistaken for a verb before no.
        for(auto next=at+1;next<end;++next) {
            if(one_of(words[next].text,{"por","falta","escasez"}))return true;
            if(!one_of(words[next].text,{"un","una","el","la","los","las",
                    "solo","solamente","unicamente"}))break;
        }
    }
    return false;
}
inline bool coordinated_claim(const std::vector<Word>& words,std::size_t at) {
    if(!one_of(words[at].text,{"y","e"}))return false;
    for(auto next=at+1;next<words.size()&&next<=at+7;++next) {
        if(change_predicate(words[next].text)!=ChangePredicate::none||
                inventory_state(words[next].text))return true;
        if(!claim_link(words[next].text)&&
                !one_of(words[next].text,{"no","nunca","jamas"}))break;
    }
    return false;
}
enum class UnsupportedClaim { none, correction, inventory };
inline UnsupportedClaim unsupported_claims(std::string_view folded,
        const std::vector<Word>& words) {
    for(std::size_t begin=0;begin<words.size();) {
        auto end=begin+1;
        while(end<words.size()&&!clause_boundary(folded,words[end-1],words[end],true)&&
                !one_of(words[end].text,{"pero","sino"})&&
                !coordinated_claim(words,end))++end;
        bool automatic=false,explicit_other_object=false,has_input=false;
        for(auto at=begin;at<end;++at) {
            automatic=automatic||automatic_word(words[at].text);
            has_input=has_input||input_subject(words[at].text);
            explicit_other_object=explicit_other_object||one_of(words[at].text,
                {"mensaje","diagnostico","redaccion","informe","texto","codigo"});
        }
        for(auto at=begin;at<end;++at) {
            const auto change=change_predicate(words[at].text);
            if(change==ChangePredicate::none)continue;
            const auto qualification=qualify_claim(words,begin,end,at);
            if(qualification.negated||qualification.modal)continue;
            if(change==ChangePredicate::realization&&!correction_object(words,begin,end,at))continue;
            if(change==ChangePredicate::automatic_present) {
                // A present-tense program/reflexive action is not the informal
                // imperative "corrige". Automaticity must belong to this clause.
                if(!automatic||!program_agent(words,begin,at))continue;
            }
            if(automatic||has_input||!explicit_other_object)
                return UnsupportedClaim::correction;
        }
        for(auto at=begin;at<end;++at) {
            if(!inventory_word(words[at].text))continue;
            std::optional<std::size_t> attribution;
            for(auto verb=begin;verb<end;++verb)
                if(attribution_predicate(words,begin,end,verb)&&
                        (!attribution||verb<at))attribution=verb;
            if(!attribution)for(auto relation=begin;relation<at;++relation) {
                if(words[relation].text!="por")continue;
                attribution=relation;
                for(auto verb=relation;verb>begin;) {
                    --verb;
                    if(one_of(words[verb].text,{"rechazo","rechaza","rechazan",
                            "rechazaron","falla","fallan","fallo"})) {
                        attribution=verb;break;
                    }
                }
            }
            if(attribution) {
                // The negation belongs to attribution, not to "existencias":
                // "no se atribuye ... falta de existencias" denies that cause.
                const auto qualification=qualify_claim(words,begin,end,*attribution);
                if(!qualification.negated&&!qualification.modal)
                    return UnsupportedClaim::inventory;
                continue;
            }
            for(auto state=begin;state<end;++state)if(inventory_state(words[state].text)) {
                // "No hay existencias" still asserts an unobserved inventory
                // state. Negating existence does not deny the factual claim.
                if(!qualify_claim(words,begin,end,state).modal)
                    return UnsupportedClaim::inventory;
            }
        }
        begin=end;
    }
    return UnsupportedClaim::none;
}
// Qualify each explicit validity claim by its own subject and clause.
// Mentioning both fields in a later domain instruction does not say that
// both observed values failed. Diagnostics need not make such a claim at all.
inline bool validity_claims_match(std::string_view line,const std::vector<Word>& words,
        const RejectionFacts& fact) {
    const auto source=fold_spanish(line);
    const auto field=[](std::string_view value)->unsigned {
        if(one_of(value,{"cobro","cobros"}))return 1;
        if(one_of(value,{"devolucion","devoluciones"}))return 2;
        return 0;
    };
    const auto boundary=[&](std::size_t left,std::size_t right) {
        if(clause_boundary(source,words[left],words[right]))return true;
        for(auto at=words[left].end;at<words[right].start;++at) {
            const auto c=source[at];
            // A field label may introduce its verdict ("cobros: invalidos").
            // The colon after the numeric preamble still starts a new clause.
            if(c==':'&&!field(words[left].text)&&
                    !one_of(words[left].text,{"ambos","ambas"}))return true;
        }
        return false;
    };
    const auto validity=[](std::string_view value)->std::optional<bool> {
        if(one_of(value,{"invalido","invalida","invalidos","invalidas",
                "incorrecto","incorrecta","incorrectos","incorrectas"}))return true;
        if(one_of(value,{"valido","valida","validos","validas",
                "correcto","correcta","correctos","correctas"}))return false;
        return {};
    };
    const auto starts_own_claim=[&](std::size_t subject) {
        for(auto at=subject+1;at<words.size();++at) {
            if(boundary(at-1,at))break;
            const auto& word=words[at];
            if(validity(word.text))return true;
            if(word.number||one_of(word.text,{"es","son","esta","estan",
                    "ser","estar","se","encuentra","encuentran","resulta","resultan",
                    "no","nunca","jamas","debe","deben","deberia","deberian",
                    "puede","pueden","podria","podrian","recibido","recibida",
                    "recibidos","recibidas"}))continue;
            break;
        }
        return false;
    };
    std::vector<bool> bound_subject(words.size(),false);
    for(std::size_t predicate=0;predicate<words.size();++predicate) {
        const auto asserted_invalid=validity(words[predicate].text);
        if(!asserted_invalid)continue;
        unsigned subjects=0;
        bool coordinated=false,modal=false,exclusive=false;
        std::optional<std::size_t> negation,collective;
        const auto scan=[&](bool backwards) {
            auto cursor=predicate;
            while(backwards?cursor>0:cursor+1<words.size()) {
                const auto next=backwards?cursor-1:cursor+1;
                if(boundary(std::min(cursor,next),std::max(cursor,next)))break;
                cursor=next;
                const auto& word=words[cursor];
                if(validity(word.text)||bound_subject[cursor])break;
                // In "son invalidos los cobros y son validas las devoluciones",
                // the first subject cannot belong to the second predicate.
                // Its following verb/negation likewise belongs to that next claim.
                if(!backwards&&subjects&&one_of(word.text,{"es","son","esta","estan",
                        "ser","estar","se","encuentra","encuentran","resulta","resultan",
                        "no","nunca","jamas","debe","deben","deberia","deberian",
                        "puede","pueden","podria","podrian"}))break;
                const auto named_field=field(word.text);
                if(named_field) {
                    // "invalidos los cobros y las devoluciones son validas"
                    // starts a new claim at the second field, not a joint subject.
                    if(!backwards&&subjects&&starts_own_claim(cursor))break;
                    if(subjects&&!(subjects&named_field)&&!coordinated)break;
                    subjects|=named_field;coordinated=false;
                    bound_subject[cursor]=true;continue;
                }
                if(one_of(word.text,{"ambos","ambas"})) {
                    subjects=3;collective=cursor;
                    bound_subject[cursor]=true;continue;
                }
                if(one_of(word.text,{"y","e"})){coordinated=true;continue;}
                if(one_of(word.text,{"no","nunca","jamas"})) {
                    if(!negation||cursor<*negation)negation=cursor;
                    continue;
                }
                if(one_of(word.text,{"solo","solamente","unicamente"})) {
                    exclusive=true;continue;
                }
                // A requirement or possibility is not a verdict on these
                // actual inputs: "ambos datos deben ser validos".
                if(one_of(word.text,{"debe","deben","deberia","deberian",
                        "puede","pueden","podria","podrian"})) {
                    modal=true;continue;
                }
                if(word.number||one_of(word.text,{"el","la","los","las","un","una",
                        "es","son","esta","estan","ser","estar","se","encuentra",
                        "encuentran","resulta","resultan","valor","valores",
                        "dato","datos","recibido","recibida","recibidos","recibidas",
                        "de","del"}))continue;
                break;
            }
        };
        scan(true);
        if(!subjects)scan(false); // "son invalidas las devoluciones"
        if(!subjects||modal)continue;
        const bool first_invalid=fact.first<0||fact.first>9;
        const bool second_invalid=fact.second<0||fact.second>9;
        // "No ambos son invalidos" denies the joint claim; it does not
        // assert that each operand is valid. Negation after the subject
        // ("ambos no son invalidos") qualifies each named operand.
        const bool denies_collective=negation&&collective&&*negation<*collective;
        const bool invalid=*asserted_invalid!=(negation.has_value()&&!denies_collective);
        bool matches=(!(subjects&1)||first_invalid==invalid)&&
                     (!(subjects&2)||second_invalid==invalid);
        if(denies_collective)matches=!matches;
        if(!matches)return false;
        if(exclusive&&!denies_collective&&
                ((subjects==1&&second_invalid==invalid)||
                 (subjects==2&&first_invalid==invalid)))return false;
    }
    return true;
}
inline RejectionReview examine(std::string_view line,RejectionOperation kind,const RejectionFacts& fact) {
    const auto tokens=words(line);
    const auto claim=unsupported_claims(fold_spanish(line),tokens);
    if(claim==UnsupportedClaim::inventory)
        return {false,"Se atribuyó el rechazo a existencias no observadas."};
    if(claim==UnsupportedClaim::correction)
        return {false,"Se afirmó una corrección de datos que el programa no realizó."};
    if(!mentions(tokens,kind==RejectionOperation::boxes
        ?std::initializer_list<std::string_view>{"caja","cajas"}
        :std::initializer_list<std::string_view>{"saldo"}))
        return {false,"Falta identificar la operación."};
    if(!labelled(tokens,{"peticion","solicitud","pedido","consulta","caso","entrada","operacion"},fact.request))
        return {false,"No se identifica correctamente el número de petición."};
    if(kind==RejectionOperation::boxes) {
        if(!labelled(tokens,{"piezas","pieza","cantidad"},fact.first))
            return {false,"Las piezas rechazadas no están asociadas a su valor."};
    }else {
        if(!labelled(tokens,{"cobros","cobro"},fact.first)||
           !labelled(tokens,{"devoluciones","devolucion"},fact.second))
            return {false,"Cobros y devoluciones no conservan sus propios valores."};
        if(!validity_claims_match(line,tokens,fact))
            return {false,"La validez atribuida a cobros o devoluciones contradice sus valores."};
    }
    if(!range(line,tokens,kind==RejectionOperation::boxes?20:9))
        return {false,"No se comunica el intervalo entero permitido."};
    if(!actionable(tokens))
        return {false,"Falta pedir una revisión o corrección de los datos."};
    return {true,{}};
}
} // namespace rejection_detail

inline RejectionReview evaluate_rejection_diagnostics(std::string_view stderr_text,
        RejectionOperation kind,std::span<const RejectionFacts> expected) {
    std::vector<std::string_view> lines;
    for(std::size_t start=0;start<stderr_text.size();) {
        const auto end=stderr_text.find('\n',start);
        auto line=stderr_text.substr(start,end==stderr_text.npos?end:end-start);
        if(line.ends_with('\r'))line.remove_suffix(1);
        if(line.empty())return {false,"Hay un diagnóstico vacío o una línea sobrante."};
        lines.push_back(line);
        if(end==stderr_text.npos)break;
        start=end+1;
    }
    if(lines.size()!=expected.size())return {false,"El número de diagnósticos no coincide con los rechazos."};
    for(std::size_t i=0;i<expected.size();++i) {
        const auto result=rejection_detail::examine(lines[i],kind,expected[i]);
        if(!result.correct)return {false,"Diagnóstico "+std::to_string(i+1)+": "+result.reason};
    }
    return {true,{}};
}

// An independently compiled probe redirects the very same student function
// to two alternative ostreams, separated by an unambiguous record marker.
// Writing directly to std::cerr, or changing the function signature, cannot
// pass the subsequent stream/semantic check. It never edits student files.
inline std::string rejection_stream_probe_source(RejectionOperation kind,std::string_view student_source) {
    std::string source="#define main lgaed_student_original_main\n";
    source+=student_source;
    source+="\n#undef main\n#include <cstdio>\n#include <iostream>\n#include <sstream>\n#include <string>\nint main() {\n"
        "    std::ostringstream primero, segundo;\n";
    if(kind==RejectionOperation::boxes)source+=
        "    informar_rechazo_cajas(primero, 1, -1);\n"
        "    informar_rechazo_cajas(segundo, 3, 21);\n";
    else source+=
        "    informar_rechazo_saldo(primero, 2, 10, 0);\n"
        "    informar_rechazo_saldo(segundo, 3, 0, -1);\n";
    // The trusted driver writes the two captured streams through C stdio.
    // Unlike std::cout, this is not silenced by a student's manipulation of
    // std::cout's failbit/rdbuf while the function is being evaluated.
    source+="    const auto emit = [](const std::string& value) {\n"
        "        return std::fwrite(value.data(), 1, value.size(), stdout) == value.size();\n"
        "    };\n"
        "    if (!emit(primero.str()) || std::fputc('\\x1e',stdout)==EOF ||\n"
        "        !emit(segundo.str()) || std::fflush(stdout)!=0) return 2;\n"
        "    return 0;\n}\n";
    return source;
}
inline RejectionReview evaluate_rejection_stream_probe(std::string_view stdout_text,
        std::string_view stderr_text,RejectionOperation kind) {
    if(!stderr_text.empty())return {false,"La función escribió fuera del std::ostream& proporcionado."};
    const auto divider=stdout_text.find('\x1e');
    if(divider==stdout_text.npos||stdout_text.find('\x1e',divider+1)!=stdout_text.npos)
        return {false,"No se separaron los dos diagnósticos del flujo alternativo."};
    if(kind==RejectionOperation::boxes) {
        const RejectionFacts first[]={{1,-1}},second[]={{3,21}};
        auto result=evaluate_rejection_diagnostics(stdout_text.substr(0,divider),kind,first);
        if(!result.correct)return result;
        return evaluate_rejection_diagnostics(stdout_text.substr(divider+1),kind,second);
    }
    const RejectionFacts first[]={{2,10,0}},second[]={{3,0,-1}};
    auto result=evaluate_rejection_diagnostics(stdout_text.substr(0,divider),kind,first);
    if(!result.correct)return result;
    return evaluate_rejection_diagnostics(stdout_text.substr(divider+1),kind,second);
}


// One observable input has an independently calculated result and rejection
// facts. The student implementation is never consulted to obtain either.
struct RejectionExpected {
    std::string standard_output;
    std::vector<RejectionFacts> rejected;
    int exit_code = 0;
};
inline std::optional<RejectionExpected> rejection_expected_for_input(
        RejectionOperation operation,std::string_view input) {
    std::istringstream source{std::string(input)};
    std::vector<int> numbers;
    int number=0;
    // Each task accepts up to eight complete requests. Boxes receive one
    // integer per request; balances receive two. Never grade additional
    // unrequested observations as though they were in the authored domain.
    const std::size_t maximum_numbers =
        operation==RejectionOperation::boxes ? 8 : 16;
    for(;;) {
        // Test for a clean end before extracting the next integer. An
        // out-of-range integer at EOF sets both failbit and eofbit; checking
        // eof() alone after extraction would wrongly accept that input.
        source>>std::ws;
        if(source.eof())break;
        if(!(source>>number))return {};
        if(numbers.size()==maximum_numbers)return {};
        numbers.push_back(number);
    }
    if(source.bad()||(operation==RejectionOperation::balance&&numbers.size()%2))return {};
    RejectionExpected expected;
    if(operation==RejectionOperation::boxes) {
        for(std::size_t i=0;i<numbers.size();++i) {
            const auto pieces=numbers[i];
            if(pieces<0||pieces>20) {
                expected.rejected.push_back({static_cast<int>(i+1),pieces});
            }else {
                expected.standard_output+="Cajas="+std::to_string((pieces+3)/4)+"\n";
            }
        }
    }else {
        for(std::size_t i=0;i<numbers.size();i+=2) {
            const auto charges=numbers[i],returns=numbers[i+1];
            if(charges<0||charges>9||returns<0||returns>9) {
                expected.rejected.push_back({static_cast<int>(i/2+1),charges,returns});
            }else {
                expected.standard_output+="Saldo="+std::to_string(charges-returns)+"\n";
            }
        }
    }
    expected.exit_code=expected.rejected.empty()?0:1;
    return expected;
}
// The balance task also requests three student-chosen pairs: a low rejection,
// a valid result and a high rejection, all distinct from the prepared sample.
// This case is selected by its domain, not by one hard-coded answer string.
inline std::optional<RejectionExpected> balance_three_pair_variation(std::string_view input) {
    std::istringstream source{std::string(input)};
    int numbers[6]{};
    for(int& number:numbers)if(!(source>>number))return {};
    std::string extra;
    if(source>>extra)return {};
    const auto allowed=[](int value){return value>=0&&value<=9;};
    const bool low=(numbers[0]<0&&allowed(numbers[1]))||(allowed(numbers[0])&&numbers[1]<0);
    const bool valid=allowed(numbers[2])&&allowed(numbers[3]);
    const bool high=(numbers[4]>9&&allowed(numbers[5]))||(allowed(numbers[4])&&numbers[5]>9);
    if(!low||!valid||!high)return {};
    for(std::size_t i=0;i<6;i+=2) {
        const auto a=numbers[i],b=numbers[i+1];
        if((a==2&&b==3)||(a==10&&b==0)||(a==0&&b==-1)||(a==5&&b==5))return {};
    }
    RejectionExpected value;
    value.standard_output="Saldo="+std::to_string(numbers[2]-numbers[3])+"\n";
    value.rejected={{1,numbers[0],numbers[1]},{3,numbers[4],numbers[5]}};
    value.exit_code=1;
    return value;
}
// The two original U3L6 programs must remain byte-identical except for the
// body of their one assigned diagnostic function. No other edit can borrow a
// normal-output or stream-probe receipt. This also rejects preprocessing
// directives injected into the otherwise editable function body.
inline bool rejection_function_only_edit(std::string_view original,
        std::string_view candidate,RejectionOperation operation) {
    constexpr std::string_view boxes="void informar_rechazo_cajas(";
    constexpr std::string_view balances="void informar_rechazo_saldo(";
    const auto marker=operation==RejectionOperation::boxes?boxes:balances;
    const auto position=original.find(marker);
    if(position==original.npos||original.find(marker,position+marker.size())!=original.npos)return false;
    const auto open=original.find('{',position+marker.size());
    if(open==original.npos)return false;
    constexpr std::string_view after="\n}\n\nint main()";
    const auto closing=original.find(after,open);
    if(closing==original.npos)return false;
    const auto prefix=original.substr(0,open+1),suffix=original.substr(closing+1);
    if(candidate.size()<prefix.size()+suffix.size()||!candidate.starts_with(prefix)||
        !candidate.ends_with(suffix))return false;
    const auto body=candidate.substr(prefix.size(),candidate.size()-prefix.size()-suffix.size());
    const auto tokens=code_format::cpp_tokens(body);
    if(!tokens || code_format::cpp_process_termination_tokens(*tokens))
        return false;
    // The function's one permitted output is the supplied std::ostream&.
    // Source identifiers are checked as tokens, never as substrings of
    // Spanish text, comments or raw strings. In particular, denying early
    // process termination is essential: the independent driver must run to
    // completion, rather than accepting a forged pair of diagnostics.
    constexpr std::string_view forbidden[]{
        "cout","cerr","clog","wcout","wcerr","wclog",
        "stdout","stderr","printf","fprintf","fputs","puts",
        "putchar","fputc","fwrite","freopen","fclose",
        "dup2","_dup2","SetStdHandle","WriteFile",
        "WriteConsoleA","WriteConsoleW","system","_popen","popen",
        "CreateProcessA","CreateProcessW","WinExec"
    };
    for(const auto& token:*tokens) {
        if(!token.starts_with("id:"))continue;
        const auto name=std::string_view(token).substr(3);
        for(const auto blocked:forbidden)
            if(name==blocked)return false;
    }
    return code_format::confined_function_body_edit(body);
}

} // namespace lgaed::curriculum

