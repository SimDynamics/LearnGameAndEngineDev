#include "core/lesson.hpp"

#include <algorithm>
#include <array>

namespace lgaed::curriculum {
namespace {
struct Diagnostic {
    std::string_view name, statement, error;
};
constexpr std::array diagnostics{
    Diagnostic{"const-member-call", "fija.cambiar_a(4,2);", "C2662"},
    Diagnostic{"explicit-copy-initialization", "Contador no_permitido = 3;", "C2440"},
    Diagnostic{"explicit-argument-conversion", "leer(3);", "C2664"},
    Diagnostic{"explicit-assignment", "destino = 3;", "C2679"},
    Diagnostic{"swapped-named-arguments", "const Caja mal{Alto{4}, Ancho{3}};", "C2440"}
};
struct Token { std::string_view text; std::size_t start, end; };
bool space(unsigned char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f'; }
bool identifier(unsigned char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
bool digit(unsigned char c) { return c>='0'&&c<='9'; }
// This is a conservative lexer for five closed source exercises, not a C++
// parser. Translation-phase splicing and alternative spellings are rejected;
// a fresh native control compilation additionally checks all accepted spacing.
std::optional<std::vector<Token>> lex(std::string_view source) {
    if(source.size()>8*1024*1024||source.find('\0')!=source.npos)return {};
    for(std::size_t i=0;i+1<source.size();++i)
        if(source[i]=='\\'&&(source[i+1]=='\n'||source[i+1]=='\r'))return {};
    std::vector<Token> tokens;
    for(std::size_t i=0;i<source.size();) {
        if(space(static_cast<unsigned char>(source[i]))){++i;continue;}
        if(source.substr(i).starts_with("//")) {
            const auto end=source.find_first_of("\r\n",i+2);i=end==source.npos?source.size():end;continue;
        }
        if(source.substr(i).starts_with("/*")) {
            const auto end=source.find("*/",i+2);if(end==source.npos)return {};i=end+2;continue;
        }
        const auto start=i;const auto c=static_cast<unsigned char>(source[i]);
        if(identifier(c)) {
            do{++i;}while(i<source.size()&&(identifier(static_cast<unsigned char>(source[i]))||digit(static_cast<unsigned char>(source[i]))));
        }else if(digit(c)) {
            do{++i;}while(i<source.size()&&(identifier(static_cast<unsigned char>(source[i]))||digit(static_cast<unsigned char>(source[i]))||source[i]=='.'));
        }else if(c=='\''||c=='"') {
            const auto quote=source[i++];bool closed=false;
            while(i<source.size()) {
                if(source[i]=='\r'||source[i]=='\n')return {};
                if(source[i]=='\\'){if(i+1==source.size())return {};i+=2;}
                else if(source[i++]==quote){closed=true;break;}
            }
            if(!closed)return {};
        }else {
            if(c>=0x80)return {};
            constexpr std::array<std::string_view,32> operators{
                "%:%:","<=>","<<=",">>=","->*","...","::","++","--","->",".*","+=","-=","*=","/=","%=",
                "^=","&=","|=","<<",">>","==","!=","<=",">=","&&","||","##","<:",":>","<%","%>"};
            const auto found=std::ranges::find_if(operators,[&](auto op){return source.substr(i).starts_with(op);});
            if(found!=operators.end())i+=found->size();
            else if(std::string_view("{}[]();:?~!+-*/%^&|=<>,.#").find(static_cast<char>(c))!=std::string_view::npos)++i;
            else return {};
        }
        tokens.push_back({source.substr(start,i-start),start,i});
        if(tokens.size()>100000)return {};
    }
    return tokens;
}
std::size_t line_at(std::string_view source,std::size_t offset) {
    std::size_t line=1;
    for(std::size_t i=0;i<offset;++i)
        if(source[i]=='\r'){++line;if(i+1<offset&&source[i+1]=='\n')++i;}
        else if(source[i]=='\n')++line;
    return line;
}
}
bool has_compiler_objective(const Block& block) {
    return block.type==BlockType::cpp_compiler&&(block.output_check.has_value()||block.compile_check.has_value());
}
std::optional<CompileDiagnosticSource> prepare_compile_diagnostic_source(const Block& block,std::string_view candidate) {
    if(block.type!=BlockType::cpp_compiler||!block.compile_check||block.output_check||!block.project_files.empty()||
            !block.source_files.empty()||block.cmake||block.standard_input)return {};
    const auto definition=std::ranges::find(diagnostics,block.compile_check->diagnostic,&Diagnostic::name);
    if(definition==diagnostics.end())return {};
    const auto initial=lex(block.initial_code),actual=lex(candidate),statement=lex(definition->statement);
    if(!initial||!actual||!statement||actual->size()!=initial->size()+statement->size())return {};
    std::size_t main_open=initial->size(),main_close=initial->size();
    constexpr std::array<std::string_view,5> main_prefix{"int","main","(",")","{"};
    for(std::size_t i=0;i+main_prefix.size()<=initial->size();++i) {
        bool matches=true;
        for(std::size_t n=0;n<main_prefix.size();++n)matches&=(*initial)[i+n].text==main_prefix[n];
        if(!matches)continue;
        if(main_open!=initial->size())return {};
        main_open=i+main_prefix.size()-1;std::size_t depth=1;
        for(auto n=main_open+1;n<initial->size();++n) {
            if((*initial)[n].text=="{")++depth;
            else if((*initial)[n].text=="}"&&!--depth){main_close=n;break;}
        }
    }
    if(main_close==initial->size())return {};
    for(std::size_t inserted=main_open+1;inserted<=main_close;++inserted) {
        const auto previous=(*initial)[inserted-1].text;
        if(previous!="{"&&previous!=";")continue;
        std::size_t depth=1;
        for(auto i=main_open+1;i<inserted;++i){if((*initial)[i].text=="{")++depth;else if((*initial)[i].text=="}")--depth;}
        if(depth!=1)continue;
        bool matches=true;
        for(std::size_t i=0;i<inserted&&matches;++i)matches=(*actual)[i].text==(*initial)[i].text;
        for(std::size_t i=0;i<statement->size()&&matches;++i)matches=(*actual)[inserted+i].text==(*statement)[i].text;
        for(std::size_t i=inserted;i<initial->size()&&matches;++i)matches=(*actual)[i+statement->size()].text==(*initial)[i].text;
        if(!matches)continue;
        const auto begin=(*actual)[inserted].start,end=(*actual)[inserted+statement->size()-1].end;
        CompileDiagnosticSource result{std::string(candidate),std::string(candidate),line_at(candidate,begin),line_at(candidate,end-1),std::string(definition->error)};
        // Keep physical line positions stable for independent control/candidate
        // diagnostics, including CRLF and comments inside the requested line.
        for(auto i=begin;i<end;++i)if(result.control_source[i]!='\r'&&result.control_source[i]!='\n')result.control_source[i]=' ';
        return result;
    }
    return {};
}
} // namespace lgaed::curriculum
