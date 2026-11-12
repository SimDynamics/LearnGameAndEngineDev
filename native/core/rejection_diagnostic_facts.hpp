#pragma once

#include "core/code_format_compat.hpp"

#include <charconv>
#include <cctype>
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
inline bool labelled(const std::vector<Word>& words,std::initializer_list<std::string_view> names,int expected) {
    for(std::size_t i=0;i<words.size();++i)if(one_of(words[i].text,names)) {
        // Allow "petición número 2", "cobros: 10" and "piezas recibidas -1".
        for(std::size_t j=i+1;j<words.size()&&j<=i+3;++j) {
            if(words[j].number)return *words[j].number==expected;
            if(!one_of(words[j].text,{"numero","num","de","del","recibidas","recibidos","son","es"}))break;
        }
    }
    return false;
}
inline bool range(std::string_view original,const std::vector<Word>& words,int upper) {
    // The interval is a relationship, not two unrelated numeric fields.
    // In particular, "cobros=10 devoluciones=0 9" is NOT a statement
    // that the valid domain is 0..9. Require interval language and syntax.
    const auto source=fold_spanish(original);
    const auto describes_range=[&](std::size_t at) {
        const auto first=at>5?at-5:0;
        for(std::size_t i=first;i<at;++i) {
            if(one_of(words[i].text,{"rango","intervalo","dominio","limite","limites",
                    "entero","enteros","entre","desde","permitido","permitidos",
                    "permitida","permitidas","admite","admiten","acepta","aceptan",
                    "valido","validos","valida","validas","debe","deben"}))return true;
        }
        return false;
    };
    for(std::size_t i=0;i<words.size();++i)if(words[i].number==0&&describes_range(i)) {
        // Example spellings: "0 a 9", "0 hasta 9", "0..9", "0-9".
        if(i+2<words.size()&&one_of(words[i+1].text,{"a","al","y","hasta"})
                &&words[i+2].number==upper)return true;
        if(i+1>=words.size())continue;
        const auto& next=words[i+1];
        if(next.number==upper&&next.start>=words[i].end) {
            const auto connector=std::string_view(source).substr(words[i].end,
                next.start-words[i].end);
            if(connector.find("..")!=connector.npos)return true;
        }
        if(next.number==-upper&&next.start==words[i].end&&
                next.start<source.size()&&source[next.start]=='-')return true;
    }
    return false;
}
inline bool actionable(const std::vector<Word>& words) {
    for(const auto& word:words)for(const auto stem:
        {"revis","verific","verifiqu","comprueb","comprob","correg","corrij","rectific","modific","ajust"}) {
        if(word.text.starts_with(stem))return true;
    }
    return false;
}
inline RejectionReview examine(std::string_view line,RejectionOperation kind,const RejectionFacts& fact) {
    const auto tokens=words(line);
    if(mentions(tokens,{"stock","existencias","agotado","agotadas","inventario","disponibilidad"}))
        return {false,"Se atribuyó el rechazo a existencias no observadas."};
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
        const bool first_invalid=fact.first<0||fact.first>9;
        const bool second_invalid=fact.second<0||fact.second>9;
        if(first_invalid!=second_invalid&&mentions(tokens,{"ambos","ambas"})&&
           mentions(tokens,{"invalidos","invalidas","incorrectos","incorrectas"}))
            return {false,"El diagnóstico dice que ambos operandos fallaron."};
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
    source+="\n#undef main\n#include <iostream>\n#include <sstream>\nint main() {\n"
        "    std::ostringstream primero, segundo;\n";
    if(kind==RejectionOperation::boxes)source+=
        "    informar_rechazo_cajas(primero, 1, -1);\n"
        "    informar_rechazo_cajas(segundo, 3, 21);\n";
    else source+=
        "    informar_rechazo_saldo(primero, 2, 10, 0);\n"
        "    informar_rechazo_saldo(segundo, 3, 0, -1);\n";
    source+="    std::cout << primero.str() << '\\x1e' << segundo.str();\n"
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
    while(source>>number) {
        if(numbers.size()==16)return {};
        numbers.push_back(number);
    }
    if(!source.eof()||(operation==RejectionOperation::balance&&numbers.size()%2))return {};
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
    return !code_format::forbidden_preprocessor_in_edit(body);
}

} // namespace lgaed::curriculum
