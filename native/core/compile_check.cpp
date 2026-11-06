#include "core/lesson.hpp"

#include <algorithm>
#include <array>
#include <exception>

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

enum class ProjectFamily { difference,envelopes,series,shelf };
enum class ProjectFault { unselected_definition,conflicting_return,missing_header,missing_declaration,duplicate_definition,unknown_call };
struct ProjectRecipe {std::string_view name,owner;ProjectFamily family;ProjectFault fault;};
constexpr std::array project_recipes{
    ProjectRecipe{"u6-difference-unselected-definition","block_0014_0002",ProjectFamily::difference,ProjectFault::unselected_definition},
    ProjectRecipe{"u6-difference-conflicting-return","block_0015_0002",ProjectFamily::difference,ProjectFault::conflicting_return},
    ProjectRecipe{"u6-difference-missing-header","block_0017_0002",ProjectFamily::difference,ProjectFault::missing_header},
    ProjectRecipe{"u6-envelopes-missing-header","block_0016_0002",ProjectFamily::envelopes,ProjectFault::missing_header},
    ProjectRecipe{"u6-envelopes-missing-declaration","block_0017_0002",ProjectFamily::envelopes,ProjectFault::missing_declaration},
    ProjectRecipe{"u6-envelopes-unselected-definitions","block_0018_0002",ProjectFamily::envelopes,ProjectFault::unselected_definition},
    ProjectRecipe{"u6-envelopes-duplicate-definition","block_0019_0002",ProjectFamily::envelopes,ProjectFault::duplicate_definition},
    ProjectRecipe{"u6-series-unknown-call","block_0021_0002",ProjectFamily::series,ProjectFault::unknown_call},
    ProjectRecipe{"u6-series-unselected-definitions","block_0022_0002",ProjectFamily::series,ProjectFault::unselected_definition},
    ProjectRecipe{"u6-series-duplicate-definition","block_0023_0002",ProjectFamily::series,ProjectFault::duplicate_definition},
    ProjectRecipe{"u6-shelf-unselected-definition","block_0013_0002",ProjectFamily::shelf,ProjectFault::unselected_definition},
    ProjectRecipe{"u6-shelf-conflicting-return","block_0014_0002",ProjectFamily::shelf,ProjectFault::conflicting_return}
};
struct ProjectShape {std::vector<std::string> files,sources;std::string header,implementation;};
ProjectShape project_shape(ProjectFamily family) {
    switch(family){
    case ProjectFamily::difference:return {{"diferencia.h","diferencia.cpp","main.cpp"},{"diferencia.cpp","main.cpp"},"diferencia.h","diferencia.cpp"};
    case ProjectFamily::envelopes:return {{"sobres.h","sobres.cpp","main.cpp"},{"sobres.cpp","main.cpp"},"sobres.h","sobres.cpp"};
    case ProjectFamily::series:return {{"serie.h","serie.cpp","main.cpp"},{"serie.cpp","main.cpp"},"serie.h","serie.cpp"};
    case ProjectFamily::shelf:return {{"estante.h","conteo.cpp","estado.cpp","main.cpp"},{"conteo.cpp","estado.cpp","main.cpp"},"estante.h","estado.cpp"};
    }
    return {};
}
bool same_tokens(std::string_view a,std::string_view b) {
    const auto left=lex(a),right=lex(b);
    if(!left||!right||left->size()!=right->size())return false;
    for(std::size_t n=0;n<left->size();++n)if((*left)[n].text!=(*right)[n].text)return false;
    return true;
}
std::string* project_file(CppProjectFiles& files,std::string_view name) {
    const auto found=std::ranges::find(files,name,[](const auto& file)->std::string_view{return file.first;});
    return found==files.end()?nullptr:&found->second;
}
std::optional<std::size_t> unique_token(const std::vector<Token>& tokens,std::string_view value) {
    std::optional<std::size_t> found;
    for(std::size_t n=0;n<tokens.size();++n)if(tokens[n].text==value){if(found)return {};found=n;}
    return found;
}
std::optional<std::size_t> closing_token(const std::vector<Token>& tokens,std::size_t opening,
        std::string_view first,std::string_view last) {
    if(opening>=tokens.size()||tokens[opening].text!=first)return {};
    std::size_t depth=1;
    for(auto n=opening+1;n<tokens.size();++n){
        if(tokens[n].text==first)++depth;
        else if(tokens[n].text==last&&!--depth)return n;
    }
    return {};
}
std::optional<std::pair<std::size_t,std::size_t>> output_statement(const std::vector<Token>& tokens,std::size_t call) {
    if(call>=tokens.size())return {};
    auto first=call,last=call;
    while(first&&tokens[first-1].text!=";"&&tokens[first-1].text!="{"&&tokens[first-1].text!="}")--first;
    while(last<tokens.size()&&tokens[last].text!=";"&&tokens[last].text!="{"&&tokens[last].text!="}")++last;
    if(last==tokens.size()||tokens[last].text!=";"||first+2>=call||tokens[first].text!="std"||
            tokens[first+1].text!="::"||tokens[first+2].text!="cout")return {};
    return std::pair{first,last};
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

std::optional<CompileDiagnosticSource> prepare_project_compile_diagnostic_source(const Block& block,
        const CppProjectFiles& candidate_files,const std::vector<std::string>& candidate_sources) {
    if(block.type!=BlockType::cpp_compiler||!block.compile_check||block.output_check||block.cmake||block.standard_input||
            block.project_files.empty()||candidate_sources!=block.source_files)return {};
    const auto recipe=std::ranges::find(project_recipes,block.compile_check->diagnostic,&ProjectRecipe::name);
    if(recipe==project_recipes.end()||block.stable_id!=recipe->owner)return {};
    const auto shape=project_shape(recipe->family);
    auto selected=shape.sources;
    if(recipe->fault==ProjectFault::unselected_definition)std::erase(selected,shape.implementation);
    if(block.source_files!=selected||block.project_files.size()!=shape.files.size()||candidate_files.size()!=shape.files.size())return {};
    try{validate_cpp_project(candidate_files,candidate_sources);}catch(const std::exception&){return {};}
    for(std::size_t n=0;n<shape.files.size();++n)
        if(block.project_files[n].first!=shape.files[n]||candidate_files[n].first!=shape.files[n]||
                !same_tokens(block.project_files[n].second,candidate_files[n].second))return {};

    CppProjectDiagnostic project;project.control_files=candidate_files;project.control_sources=candidate_sources;
    auto* main=project_file(project.control_files,"main.cpp");
    if(!main||!same_tokens(block.initial_code,*main))return {};
    const std::string original_main=*main;
    auto* header=project_file(project.control_files,shape.header);
    if(!header)return {};
    const auto main_tokens=lex(original_main);
    if(!main_tokens)return {};
    const auto interval=[&](std::string_view source,const std::vector<Token>& tokens,std::size_t first,std::size_t last){
        project.first_line=line_at(source,tokens[first].start);project.last_line=line_at(source,tokens[last].end-1);
    };
    switch(recipe->fault){
    case ProjectFault::unselected_definition:
        project.link=true;project.control_sources=shape.sources;project.error_codes={"LNK2019","LNK1120"};
        switch(recipe->family){
        case ProjectFamily::difference:project.symbols={"diferencia"};break;
        case ProjectFamily::envelopes:project.symbols={"descripcion_sobres","sobres_necesarios"};break;
        case ProjectFamily::series:project.symbols={"estado_serie","tareas_pendientes"};break;
        case ProjectFamily::shelf:project.symbols={"descripcion_carga"};break;
        }
        break;
    case ProjectFault::conflicting_return:{
        project.expected_file=recipe->family==ProjectFamily::difference?"diferencia.cpp":"conteo.cpp";
        auto* source=project_file(project.control_files,project.expected_file);if(!source)return {};
        const auto tokens=lex(*source);if(!tokens)return {};
        const auto name=unique_token(*tokens,recipe->family==ProjectFamily::difference?"diferencia":"espacios_ocupados");
        if(!name||!*name||(*tokens)[*name-1].text!="double")return {};
        const auto close=closing_token(*tokens,*name+1,"(",")");
        if(!close||*close+1>=tokens->size()||(*tokens)[*close+1].text!="{")return {};
        interval(*source,*tokens,*name-1,*close+1);
        source->replace((*tokens)[*name-1].start,(*tokens)[*name-1].end-(*tokens)[*name-1].start,"int");
        project.error_codes={"C2556","C2371"};break;
    }
    case ProjectFault::missing_header:
        project.expected_file="main.cpp";
        if(recipe->family==ProjectFamily::difference){
            const auto include=unique_token(*main_tokens,"\"diferencia_ausente.h\"");
            if(!include||*include<2||(*main_tokens)[*include-1].text!="include"||(*main_tokens)[*include-2].text!="#")return {};
            interval(original_main,*main_tokens,*include-2,*include);
            main->replace((*main_tokens)[*include].start,(*main_tokens)[*include].end-(*main_tokens)[*include].start,"\"diferencia.h\"");
            project.error_codes={"C1083"};
        }else{
            const auto first=unique_token(*main_tokens,"descripcion_sobres"),last=unique_token(*main_tokens,"sobres_necesarios");
            if(!first||!last||*first>*last)return {};
            const auto statement=output_statement(*main_tokens,*last);if(!statement||statement->first>*first)return {};
            interval(original_main,*main_tokens,statement->first,statement->second);
            main->insert(0,"#include \"sobres.h\"\n");project.error_codes={"C3861","C2593"};
        }
        break;
    case ProjectFault::missing_declaration:{
        const auto call=unique_token(*main_tokens,"sobres_necesarios");
        const auto tokens=lex(*header);
        if(!call||!tokens)return {};
        const auto declaration=unique_token(*tokens,"std");if(!declaration)return {};
        const auto statement=output_statement(*main_tokens,*call);if(!statement)return {};
        project.expected_file="main.cpp";interval(original_main,*main_tokens,statement->first,statement->second);
        header->insert((*tokens)[*declaration].start,"int sobres_necesarios(int tarjetas);\n");
        project.error_codes={"C3861","C2593"};break;
    }
    case ProjectFault::unknown_call:{
        const auto call=unique_token(*main_tokens,"estado_inexistente");if(!call)return {};
        const auto statement=output_statement(*main_tokens,*call);if(!statement)return {};
        project.expected_file="main.cpp";interval(original_main,*main_tokens,statement->first,statement->second);
        main->replace((*main_tokens)[*call].start,(*main_tokens)[*call].end-(*main_tokens)[*call].start,"estado_serie");
        project.error_codes={"C3861","C2593"};break;
    }
    case ProjectFault::duplicate_definition:{
        const auto symbol=recipe->family==ProjectFamily::envelopes?"sobres_necesarios":"tareas_pendientes";
        const auto tokens=lex(*header);if(!tokens)return {};
        const auto name=unique_token(*tokens,symbol);if(!name||!*name||(*tokens)[*name-1].text!="int")return {};
        const auto close=closing_token(*tokens,*name+1,"(",")");if(!close)return {};
        const auto end=closing_token(*tokens,*close+1,"{","}");if(!end)return {};
        const auto start=(*tokens)[*name-1].start,body=(*tokens)[*close+1].start,finish=(*tokens)[*end].end;
        const auto definition=header->substr(start,finish-start);
        header->replace(body,finish-body,";");
        auto* implementation=project_file(project.control_files,shape.implementation);if(!implementation)return {};
        implementation->insert(0,definition+"\n\n");
        project.link=true;project.error_codes={"LNK2005","LNK1169"};project.symbols={symbol};break;
    }
    }
    const auto* control_main=project_file(project.control_files,"main.cpp");if(!control_main)return {};
    CompileDiagnosticSource result;result.control_source=*control_main;result.candidate_source=original_main;
    result.first_line=project.first_line;result.last_line=project.last_line;
    result.expected_msvc_error=project.error_codes.front();result.project=std::move(project);
    return result;
}
} // namespace lgaed::curriculum
