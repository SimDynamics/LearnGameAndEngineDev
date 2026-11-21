#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum::code_format {

enum class Language { cpp, cmake, immutable };

inline bool space(char c) noexcept {
    return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f';
}
inline bool identifier_start(unsigned char c) noexcept {
    return c=='_'||std::isalpha(c)||c>=128;
}
inline bool identifier_part(unsigned char c) noexcept {
    return c=='_'||std::isalnum(c)||c>=128;
}
inline bool physical_splice(std::string_view s) {
    return s.find("\\\n")!=s.npos||s.find("\\\r\n")!=s.npos;
}
inline void push(std::vector<std::string>& out,std::string_view kind,std::string_view value) {
    out.emplace_back(std::string(kind)+std::string(value));
}
inline std::optional<std::size_t> quoted_end(std::string_view source,std::size_t opening) {
    const auto quote=source[opening];
    for(auto i=opening+1;i<source.size();++i) {
        if(source[i]=='\\') { if(++i>=source.size())return {}; }
        else if(source[i]==quote)return i+1;
    }
    return {};
}
inline std::size_t literal_suffix_end(std::string_view source,std::size_t end) {
    // A UDL suffix is lexically adjacent to its literal: spacing it away
    // changes the program even when every separately scanned character remains.
    if(end<source.size()&&source[end]=='_'){
        ++end;
        while(end<source.size()&&identifier_part(static_cast<unsigned char>(source[end])))++end;
    }
    return end;
}

inline bool cpp_import_starts_directive(std::string_view source,std::size_t at) {
    // An import followed by <...> is a header-name only at the beginning of
    // a logical line (also after the optional export). Else 'import' may be a
    // normal identifier: return import<4>1; contains two comparisons.
    const auto newline=source.rfind('\n',at);
    const auto start=newline==source.npos?0:newline+1;
    const auto prefix=source.substr(start,at-start);
    // The last line of a multiline comment can start before our line slice.
    // Keep the source protected if we cannot identify that comment's start.
    if(prefix.find("*/")!=prefix.npos&&prefix.find("/*")==prefix.npos)return true;
    std::size_t pos=0;
    bool exported=false;
    while(pos<prefix.size()) {
        if(space(prefix[pos])){++pos;continue;}
        if(prefix.substr(pos).starts_with("/*")) {
            const auto close=prefix.find("*/",pos+2);
            if(close==prefix.npos)return true;
            pos=close+2;
            continue;
        }
        if(!exported&&prefix.substr(pos).starts_with("export")&&
           (pos+6==prefix.size()||
            !identifier_part(static_cast<unsigned char>(prefix[pos+6])))) {
            exported=true;pos+=6;continue;
        }
        return false;
    }
    return true;
}


inline bool cpp_import_has_semicolon(std::string_view source,std::size_t after_header) {
    // Protect ambiguous/incomplete imports. A definite expression operand
    // directly after '>' (on the same physical line) proves that a contextual
    // 'import' is not a C++23 header-unit declaration.
    bool crossed_line=false;
    for(auto pos=after_header;pos<source.size();) {
        if(space(source[pos])) {
            crossed_line=crossed_line||source[pos]=='\r'||source[pos]=='\n';
            ++pos;continue;
        }
        if(source.substr(pos).starts_with("/*")) {
            const auto close=source.find("*/",pos+2);
            if(close==source.npos)return true;
            crossed_line=crossed_line||
                source.substr(pos,close+2-pos).find('\n')!=source.npos;
            pos=close+2;continue;
        }
        if(source.substr(pos).starts_with("//")) {
            const auto close=source.find('\n',pos+2);
            if(close==source.npos)return true;
            crossed_line=true;pos=close+1;continue;
        }
        if(source[pos]==';'||crossed_line)return true;
        const auto c=static_cast<unsigned char>(source[pos]);
        return !(identifier_part(c)||c=='('||c=='\''||c=='"'||
            c=='+'||c=='-'||c=='!'||c=='~');
    }
    return true;
}

inline std::optional<std::vector<std::string>> cpp_tokens(std::string_view source) {
    std::vector<std::string> result;
    std::optional<std::size_t> pending_import_end;
    constexpr std::string_view raw_prefixes[]={"u8R\"","uR\"","UR\"","LR\"","R\""};
    constexpr std::string_view string_prefixes[]={"u8\"","u\"","U\"","L\"","\"","u8'","u'","U'","L'","'"};
    constexpr std::string_view operators[]={"%:%:","<<=",">>=","<=>","->*","...","##","::","->",".*","++","--",
        "==","!=","<=",">=","&&","||","<<",">>","+=","-=","*=","/=","%=","&=","|=","^=","<:",":>","<%","%>","%:"};
    for(std::size_t i=0;i<source.size();) {
        if(space(source[i])){++i;continue;}
        const auto line=source.rfind('\n',i)==source.npos?0:source.rfind('\n',i)+1;
        if((source[i]=='#'||source.substr(i).starts_with("%:"))&&
           std::all_of(source.begin()+static_cast<std::ptrdiff_t>(line),
                source.begin()+static_cast<std::ptrdiff_t>(i),[](char c){return space(c);})) {
            auto end=i;
            while(true) {
                const auto newline=source.find('\n',end);
                if(newline==source.npos){end=source.size();break;}
                auto physical=source.substr(end,newline-end);
                if(physical.ends_with('\r'))physical.remove_suffix(1);
                end=newline+1;
                if(!physical.ends_with('\\'))break;
            }
            auto part=source.substr(i,end-i);
            while(!part.empty()&&(part.back()=='\n'||part.back()=='\r'))part.remove_suffix(1);
            push(result,"directive:",part);pending_import_end.reset();i=end;continue;
        }
        if(source.substr(i).starts_with("//")) {
            auto end=source.find('\n',i+2);if(end==source.npos)end=source.size();
            auto part=source.substr(i,end-i);if(part.ends_with('\r'))part.remove_suffix(1);
            push(result,"comment:",part);i=end;continue;
        }
        if(source.substr(i).starts_with("/*")) {
            auto end=source.find("*/",i+2);if(end==source.npos)return {};
            end+=2;push(result,"comment:",source.substr(i,end-i));i=end;continue;
        }
        // A C++20/23 <...> header import is a single header-name token.
        // Spaces inside that name change the selected header.
        if(source[i]=='<'&&pending_import_end) {
            const auto gap=source.substr(*pending_import_end,i-*pending_import_end);
            if(gap.find_first_of("\r\n")!=gap.npos)return {};
            const auto close=source.find('>',i+1);
            if(close!=source.npos&&
               source.substr(i,close-i).find_first_of("\r\n")==source.npos&&
               cpp_import_has_semicolon(source,close+1)) {
                push(result,"header:",source.substr(i,close-i+1));
                i=close+1;pending_import_end.reset();continue;
            }
        }
        pending_import_end.reset();
        bool matched=false;
        for(const auto prefix:raw_prefixes)if(source.substr(i).starts_with(prefix)){
            const auto start=i+prefix.size(),opening=source.find('(',start);
            if(opening==source.npos||opening-start>16)return {};
            const auto marker=source.substr(start,opening-start);
            if(std::any_of(marker.begin(),marker.end(),[](char c){
                return space(c)||c=='\\'||c=='('||c==')';}))return {};
            const auto terminator=")"+std::string(marker)+"\"";
            const auto close=source.find(terminator,opening+1);
            if(close==source.npos)return {};
            auto end=literal_suffix_end(source,close+terminator.size());
            push(result,"literal:",source.substr(i,end-i));i=end;matched=true;break;
        }
        if(matched)continue;
        for(const auto prefix:string_prefixes)if(source.substr(i).starts_with(prefix)) {
            const auto end=quoted_end(source,i+prefix.size()-1);if(!end)return {};
            const auto suffix=literal_suffix_end(source,*end);
            push(result,"literal:",source.substr(i,suffix-i));i=suffix;matched=true;break;
        }
        if(matched)continue;
        const auto ch=static_cast<unsigned char>(source[i]);
        if(identifier_start(ch)) {
            auto end=i+1;
            while(end<source.size()&&identifier_part(static_cast<unsigned char>(source[end])))++end;
            const auto word=source.substr(i,end-i);
            push(result,"id:",word);
            if(word=="import"&&cpp_import_starts_directive(source,i))pending_import_end=end;
            i=end;continue;
        }
        if((ch>='0'&&ch<='9')||(ch=='.'&&i+1<source.size()&&source[i+1]>='0'&&source[i+1]<='9')) {
            auto end=i+1;
            while(end<source.size()) {
                const auto c=static_cast<unsigned char>(source[end]);
                if(identifier_part(c)||c=='.'||c=='\'')++end;
                else if((c=='+'||c=='-')&&(source[end-1]=='e'||source[end-1]=='E'||source[end-1]=='p'||source[end-1]=='P'))++end;
                else break;
            }
            push(result,"number:",source.substr(i,end-i));i=end;continue;
        }
        for(const auto operation:operators)if(source.substr(i).starts_with(operation)) {
            push(result,"op:",operation);i+=operation.size();matched=true;break;
        }
        if(!matched){push(result,"op:",source.substr(i,1));++i;}
    }
    return result;
}
inline std::optional<std::vector<std::string>> cmake_tokens(std::string_view source) {
    std::vector<std::string> result;
    for(std::size_t i=0;i<source.size();) {
        if(space(source[i])){++i;continue;}
        const bool comment=source[i]=='#';
        const auto start=i+(comment?1:0);
        if(start<source.size()&&source[start]=='[') {
            auto at=start+1;
            while(at<source.size()&&source[at]=='=')++at;
            if(at<source.size()&&source[at]=='[') {
                const auto closing="]"+std::string(source.substr(start+1,at-start-1))+"]";
                const auto found=source.find(closing,at+1);if(found==source.npos)return {};
                const auto end=found+closing.size();
                push(result,"bracket:",source.substr(i,end-i));i=end;continue;
            }
        }
        if(comment) {
            auto end=source.find('\n',i);if(end==source.npos)end=source.size();
            auto part=source.substr(i,end-i);if(part.ends_with('\r'))part.remove_suffix(1);
            push(result,"comment:",part);i=end;continue;
        }
        if(source[i]=='"') {
            auto end=quoted_end(source,i);if(!end)return {};
            // Adjacent quoted arguments trigger a CMake syntax warning.
            // Formatting them apart would change the diagnostic behavior.
            if(*end<source.size()&&!space(source[*end])&&
               source[*end]!='('&&source[*end]!=')'&&source[*end]!='#')return {};
            push(result,"quoted:",source.substr(i,*end-i));i=*end;continue;
        }
        if(source[i]=='('||source[i]==')') {
            push(result,"paren:",source.substr(i,1));++i;continue;
        }
        auto end=i;
        bool legacy_quote=false;
        while(end<source.size()) {
            if(source[end]=='\\') {
                if(end+1>=source.size())return {};
                end+=2;
            }else if(source[end]=='"') {
                // Preserve one unquoted legacy token, including quoted pieces.
                // -Da="b c" is not the same argument list as -Da= "b c".
                legacy_quote=!legacy_quote;
                ++end;
            }else if(!legacy_quote&&source.substr(end).starts_with("$(")) {
                // Preserve make-style references as one unquoted token.
                const auto close=source.find(')',end+2);
                if(close==source.npos)return {};
                end=close+1;
            }else if(!legacy_quote&&(space(source[end])||
                      source[end]=='('||source[end]==')'||source[end]=='#')) {
                break;
            }else ++end;
        }
        if(end==i||legacy_quote)return {};
        push(result,"word:",source.substr(i,end-i));i=end;
    }
    return result;
}
// CMake has line-sensitive syntax independent of variables like
// CMAKE_CURRENT_LIST_LINE. Command names cannot be separated from their
// opening '(' by a newline, and consecutive commands need a newline.
inline std::optional<std::vector<unsigned char>> cmake_command_gaps(
        std::string_view source,const std::vector<std::string>& tokens) {
    std::vector<unsigned char> gaps;
    std::string_view previous;
    std::size_t offset=0,depth=0;
    bool completed_invocation=false;
    for(const auto& token:tokens) {
        const auto separator=token.find(':');
        if(separator==token.npos)return {};
        const std::string_view literal=std::string_view(token).substr(separator+1);
        const auto start=source.find(literal,offset);
        if(start==source.npos)return {};
        const bool line_break=source.substr(offset,start-offset).find('\n')!=source.npos;
        if(depth==0&&token=="paren:("&&previous.starts_with("word:"))
            gaps.push_back(line_break?1:0);
        // Comments do not erase the newline required between commands.
        // Only whitespace after the last comment counts; embedded newlines
        // within a bracket comment do not separate two commands.
        if(depth==0&&token.starts_with("word:")&&completed_invocation) {
            gaps.push_back(line_break?3:2);
            completed_invocation=false;
        }
        if(token=="paren:(")++depth;
        else if(token=="paren:)") {
            if(depth==0)return {};
            --depth;
            if(depth==0)completed_invocation=true;
        }
        previous=token;
        offset=start+literal.size();
    }
    if(depth!=0)return {};
    return gaps;
}
// Position-dependent queries must be detected as actual source tokens, not
// substrings of comments, ordinary string literals or raw strings. A macro
// directive containing one of these names stays conservatively protected:
// preprocessing can move the point where its replacement is expanded.
// Literal header names in #include are filenames, not evaluated macros.
// The entire directive is already one immutable source-format token.
// Unknown/macro includes still retain the conservative position guard.
inline bool cpp_literal_include(std::string_view token) {
    if(!token.starts_with("directive:"))return false;
    token.remove_prefix(10);
    if(token.starts_with('#'))token.remove_prefix(1);
    else if(token.starts_with("%:"))token.remove_prefix(2);
    else return false;
    while(!token.empty()&&space(token.front()))token.remove_prefix(1);
    if(!token.starts_with("include"))return false;
    token.remove_prefix(7);
    if(!token.empty()&&identifier_part(
        static_cast<unsigned char>(token.front())))return false;
    while(!token.empty()&&space(token.front()))token.remove_prefix(1);
    if(token.empty()||(token.front()!='<'&&token.front()!='"'))return false;
    const auto close=token.find(token.front()=='<'?'>':'"',1);
    if(close==token.npos||close==1)return false;
    const auto trailing=cpp_tokens(token.substr(close+1));
    return trailing&&std::ranges::all_of(*trailing,[](const std::string& part){
        return part.starts_with("comment:");
    });
}

inline bool cpp_position_sensitive_tokens(const std::vector<std::string>& tokens) {
    constexpr std::string_view names[]{
        "__LINE__", "source_location", "__builtin_LINE", "__builtin_COLUMN",
        "__builtin_source_location"
    };
    for(const auto& token:tokens) {
        // An unrecognized # or %: may still introduce a directive after
        // leading block comments. Never approve formatting it as an operator.
        if(token=="op:#"||token=="op:##"||token=="op:%:"||token=="op:%:%:")
            return true;
        if(token.starts_with("id:")) {
            const auto name=std::string_view(token).substr(3);
            if(std::ranges::any_of(names,[&](std::string_view item){return name==item;}))
                return true;
        }else if(token.starts_with("directive:")&&!cpp_literal_include(token)) {
            if(std::ranges::any_of(names,[&](std::string_view item){
                    return token.find(item)!=std::string::npos;}))
                return true;
        }
    }
    return false;
}

// Private semantic probes write a completion marker only after checking all
// cases. An edited function cannot be allowed to forge that marker and then
// terminate the process with exit status zero. Check executable identifiers,
// not text in comments, quoted strings or raw literals. Function-pointer
// aliases still contain the original termination function name.
inline bool cpp_process_termination_tokens(const std::vector<std::string>& tokens) {
    constexpr std::string_view terminating[]{
        "exit", "_Exit", "_exit", "quick_exit", "ExitProcess",
        "TerminateProcess", "TerminateJobObject", "ExitThread",
        "_endthread", "_endthreadex", "_c_exit", "abort", "terminate",
        "raise"
    };
    return std::ranges::any_of(tokens, [&](const std::string& token) {
        if (!token.starts_with("id:")) return false;
        const std::string_view name(token.data()+3, token.size()-3);
        return std::ranges::find(terminating, name) != std::end(terminating);
    });
}
// CMake bracket arguments and both forms of comments never expand variables.
// Ordinary quoted and unquoted arguments can, and conditions can read a bare
// variable name. Keep those position-dependent expressions protected while
// allowing source spacing beside inert comments and bracket literals.
inline bool cmake_position_sensitive_tokens(const std::vector<std::string>& tokens) {
    constexpr std::string_view names[]{
        "CMAKE_CURRENT_LIST_LINE", "CMAKE_CURRENT_FUNCTION_LIST_LINE"
    };
    const auto command=[](std::string_view token,std::string_view name) {
        if(token.starts_with("word:"))token.remove_prefix(5);
        else if(token.starts_with("quoted:\"")&&token.ends_with('"')) {
            // cmake_language(CALL "file" "CONFIGURE" ...) accepts quoted names.
            token.remove_prefix(8);token.remove_suffix(1);
        }else if(token.starts_with("bracket:[")) {
            // cmake_language(CALL [=[file]=] [=[CONFIGURE]=] ...) also runs.
            token.remove_prefix(8);
            std::size_t opening=1;
            while(opening<token.size()&&token[opening]=='=')++opening;
            if(opening==token.size()||token[opening]!='[')return false;
            const auto terminator="]"+std::string(token.substr(1,opening-1))+"]";
            if(token.size()<opening+1+terminator.size()||
               !token.ends_with(terminator))return false;
            token=token.substr(opening+1,token.size()-(opening+1)-terminator.size());
        }else return false;
        if(token.size()!=name.size())return false;
        for(std::size_t i=0;i<name.size();++i) {
            const auto ch=token[i];
            if((ch>='A'&&ch<='Z'?ch-'A'+'a':ch)!=name[i])return false;
        }
        return true;
    };
    const auto dynamic_name=[](std::string_view token) {
        // CMake expands unquoted/quoted CALL arguments, including variables
        // containing the actual command name or CONFIGURE subcommand.
        return (token.starts_with("word:")||token.starts_with("quoted:"))&&
            (token.find("${")!=token.npos||token.find("$ENV{")!=token.npos||
             token.find("$CACHE{")!=token.npos);
    };
    // Bracket arguments ordinarily have no variable expansion, but can be
    // consumed later by string(CONFIGURE) or file(CONFIGURE). Even a template
    // first stored via set() can become position-dependent at configuration.
    bool configures_bracket_arguments=false;
    for(std::size_t i=0;i+2<tokens.size();++i) {
        if((command(tokens[i],"string")||command(tokens[i],"file"))&&
           tokens[i+1]=="paren:("&&command(tokens[i+2],"configure"))
            configures_bracket_arguments=true;
        if(i+4<tokens.size()&&command(tokens[i],"cmake_language")&&
           tokens[i+1]=="paren:("&&command(tokens[i+2],"call")&&
           (command(tokens[i+3],"string")||command(tokens[i+3],"file")||
            dynamic_name(tokens[i+3]))&&
           (command(tokens[i+4],"configure")||dynamic_name(tokens[i+4])))
            configures_bracket_arguments=true;
    }
    for(const auto& token:tokens) {
        const bool argument=token.starts_with("word:")||token.starts_with("quoted:");
        const bool configurable_bracket=configures_bracket_arguments&&
            token.starts_with("bracket:")&&!token.starts_with("bracket:#");
        if(!argument&&!configurable_bracket)continue;
        for(const auto name:names) {
            // ${VAR} expands in ordinary arguments; @VAR@ can be substituted
            // during string(CONFIGURE) or by older CMake parsing policies.
            if((argument&&token==std::string("word:")+std::string(name))||
               token.find("${"+std::string(name)+"}")!=std::string::npos||
               token.find("@"+std::string(name)+"@")!=std::string::npos)
                return true;
        }
    }
    return false;
}
inline bool equivalent(std::string_view before,std::string_view after,Language language) {
    if(before==after)return true;
    if(language==Language::immutable||physical_splice(before)||physical_splice(after))return false;
    const auto a=language==Language::cmake?cmake_tokens(before):cpp_tokens(before);
    const auto b=language==Language::cmake?cmake_tokens(after):cpp_tokens(after);
    if(!a||!b)return false;
    // A live backslash may belong to a universal-character-name identifier.
    // The shared lexer cannot prove those boundaries, so changed source with
    // such tokens cannot receive a formatting archive. Encapsulated literals,
    // comments and complete directives do not contain a live operator token.
    if(language==Language::cpp&&
       (std::ranges::find(*a,"op:\\")!=a->end()||
        std::ranges::find(*b,"op:\\")!=b->end()))return false;
    // A physical spacing review must not change observable line/column values.
    // Only executable/source argument tokens count, not quoted explanations or
    // CMake bracket literals that never expand their contents.
    if(language==Language::cpp&&
       (cpp_position_sensitive_tokens(*a)||cpp_position_sensitive_tokens(*b)))
        return false;
    if(language==Language::cmake&&
       (cmake_position_sensitive_tokens(*a)||cmake_position_sensitive_tokens(*b)))
        return false;
    if(language==Language::cmake) {
        const auto first=cmake_command_gaps(before,*a);
        const auto second=cmake_command_gaps(after,*b);
        if(!first||!second||*first!=*second)return false;
    }
    return *a==*b;
}

// A curricular exercise may allow edits within one function or main, but
// not new preprocessing directives. Inspect lexical tokens rather than
// searching for "\n#": C++ accepts leading spaces and comments before '#',
// while '#' in a string or an ordinary comment is harmless.
// Malformed lexical input is not an authenticated source edit.
inline bool forbidden_preprocessor_in_edit(std::string_view body) {
    const auto tokens=cpp_tokens(body);
    if(!tokens)return true;
    return std::ranges::any_of(*tokens,[](const auto& token) {
        return token.starts_with("directive:") || token=="op:#" ||
            token=="op:##" || token=="op:%:" || token=="op:%:%:";
    });
}


// Enforce an authored function-body boundary, not merely a matching textual
// prefix and suffix. Otherwise a student could close main or the assigned
// diagnostic function early and introduce namespace-scope code before a
// different final '}' that happens to match the published suffix.
// Use lexical C++ tokens: braces inside comments, quoted/raw strings and
// character literals are ordinary data. The <% and %> brace digraphs are
// equivalent to '{' and '}' in C++ and must obey the same boundary.
inline bool confined_function_body_edit(std::string_view body) {
    if(forbidden_preprocessor_in_edit(body))return false;
    const auto tokens=cpp_tokens(body);
    if(!tokens)return false;
    std::size_t braces=1; // The authenticated function opening brace.
    for(const auto& token:*tokens) {
        if(token=="op:{"||token=="op:<%") {
            if(braces>=4096)return false; // Bound hostile nesting.
            ++braces;
        }else if(token=="op:}"||token=="op:%>") {
            if(braces==1)return false; // Would escape the assigned function.
            --braces;
        }
    }
    return braces==1; // Every nested brace must close inside the body.
}

} // namespace lgaed::curriculum::code_format
