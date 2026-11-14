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
inline std::optional<std::vector<std::string>> cpp_tokens(std::string_view source) {
    std::vector<std::string> result;
    constexpr std::string_view raw_prefixes[]={"u8R\"","uR\"","UR\"","LR\"","R\""};
    constexpr std::string_view string_prefixes[]={"u8\"","u\"","U\"","L\"","\"","u8'","u'","U'","L'","'"};
    constexpr std::string_view operators[]={"%:%:","<<=",">>=","<=>","->*","...","##","::","->",".*","++","--",
        "==","!=","<=",">=","&&","||","<<",">>","+=","-=","*=","/=","%=","&=","|=","^=","<:",":>","<%","%>","%:"};
    for(std::size_t i=0;i<source.size();) {
        if(space(source[i])){++i;continue;}
        const auto line=source.rfind('\n',i)==source.npos?0:source.rfind('\n',i)+1;
        if(source[i]=='#'&&std::all_of(source.begin()+static_cast<std::ptrdiff_t>(line),
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
            push(result,"directive:",part);i=end;continue;
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
            push(result,"id:",source.substr(i,end-i));i=end;continue;
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
            push(result,"quoted:",source.substr(i,*end-i));i=*end;continue;
        }
        if(source[i]=='('||source[i]==')') {
            push(result,"paren:",source.substr(i,1));++i;continue;
        }
        auto end=i;
        while(end<source.size()&&!space(source[end])&&source[end]!='('&&source[end]!=')'
                &&source[end]!='#'&&source[end]!='"') {
            if(source[end]=='\\') {
                if(end+1>=source.size())return {};
                end+=2;
            } else ++end;
        }
        if(end==i)return {};
        push(result,"word:",source.substr(i,end-i));i=end;
    }
    return result;
}
inline bool equivalent(std::string_view before,std::string_view after,Language language) {
    if(before==after)return true;
    if(language==Language::immutable||physical_splice(before)||physical_splice(after))return false;
    // Physical line and column positions may change runtime values, not merely
    // diagnostics. Preserve them until a compiler-aware source-map proof exists.
    if(language==Language::cpp) {
        constexpr std::string_view position_queries[]{
            "__LINE__", "source_location", "__builtin_LINE", "__builtin_COLUMN"
        };
        for(const auto marker:position_queries)
            if(before.find(marker)!=before.npos||after.find(marker)!=after.npos)return false;
    }
    if(language==Language::cmake&&(before.find("CMAKE_CURRENT_LIST_LINE")!=before.npos||
        after.find("CMAKE_CURRENT_LIST_LINE")!=after.npos))return false;
    const auto a=language==Language::cmake?cmake_tokens(before):cpp_tokens(before);
    const auto b=language==Language::cmake?cmake_tokens(after):cpp_tokens(after);
    return a&&b&&*a==*b;
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
