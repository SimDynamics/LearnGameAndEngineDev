#pragma once

#include "core/code_format_compat.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum::cpp_class_structure {

using Tokens=std::vector<std::string>;
struct Scope {
    std::size_t opening=0,closing=0;
};

// Use tokens from an authenticated lesson, not a textual snippet. Comments
// are ignored and quoted/raw strings remain single, non-identifier tokens.
inline std::optional<Tokens> lexical(std::string_view source) {
    const auto scanned=code_format::cpp_tokens(source);
    if(!scanned||code_format::cpp_position_sensitive_tokens(*scanned))
        return {};
    Tokens result;
    result.reserve(scanned->size());
    for(const auto& token:*scanned)
        if(!token.starts_with("comment:"))result.push_back(token);
    return result;
}
inline bool open_brace(std::string_view token) {
    return token=="op:{"||token=="op:<%";
}
inline bool close_brace(std::string_view token) {
    return token=="op:}"||token=="op:%>";
}

// Bind a genuine class/struct definition and (when declared) direct public
// inheritance. Reject ambiguous duplicate definitions and malformed scopes.
inline std::optional<Scope> defined_class(const Tokens& code,
        std::string_view name,std::string_view public_base={}) {
    std::optional<Scope> found;
    const auto self="id:"+std::string(name);
    for(std::size_t i=0;i+2<code.size();++i) {
        if((code[i]!="id:class"&&code[i]!="id:struct")||
            code[i+1]!=self)continue;
        auto at=i+2;
        if(at<code.size()&&code[at]=="id:final")++at;
        if(!public_base.empty()) {
            if(at+3>=code.size()||code[at]!="op::"||
               code[at+1]!="id:public"||
               code[at+2]!="id:"+std::string(public_base))continue;
            at+=3;
        }
        if(at>=code.size()||!open_brace(code[at]))continue;
        if(found)return {};
        std::size_t depth=1;
        for(auto j=at+1;j<code.size();++j) {
            if(open_brace(code[j]))++depth;
            else if(close_brace(code[j])) {
                if(!--depth) {
                    if(j+1>=code.size()||code[j+1]!="op:;")return {};
                    found=Scope{at,j};
                    break;
                }
            }
        }
        if(!found)return {};
    }
    return found;
}

// Bind a class implementing several named public bases, in their declared
// order. This is deliberately a separate contract from single inheritance:
// an inherited interface cannot be substituted, omitted or made private.
inline std::optional<Scope> defined_public_bases(
        const Tokens& code,std::string_view name,
        std::initializer_list<std::string_view> bases) {
    if(bases.size()<2)return {};
    std::optional<Scope> found;
    const auto self="id:"+std::string(name);
    for(std::size_t i=0;i+3<code.size();++i) {
        if((code[i]!="id:class"&&code[i]!="id:struct")||
           code[i+1]!=self)continue;
        auto at=i+2;
        if(at<code.size()&&code[at]=="id:final")++at;
        if(at>=code.size()||code[at++]!="op::")continue;
        bool valid=true,first=true;
        for(const auto base:bases) {
            if(!first) {
                if(at>=code.size()||code[at++]!="op:,"){valid=false;break;}
            }
            first=false;
            if(at+1>=code.size()||code[at++]!="id:public"||
               code[at++]!="id:"+std::string(base)){valid=false;break;}
        }
        if(!valid||at>=code.size()||!open_brace(code[at]))continue;
        if(found)return {};
        std::size_t depth=1;
        for(auto j=at+1;j<code.size();++j) {
            if(open_brace(code[j]))++depth;
            else if(close_brace(code[j])) {
                if(!--depth) {
                    if(j+1>=code.size()||code[j+1]!="op:;")return {};
                    found=Scope{at,j};
                    break;
                }
            }
        }
        if(!found)return {};
    }
    return found;
}

// A declaration must be directly inside its owner, never in another method,
// a nested type, an unrelated global declaration or instructional prose.
inline bool direct_contains(const Tokens& code,Scope scope,
        std::initializer_list<std::string> pattern) {
    if(pattern.size()==0||scope.opening>=scope.closing||
       scope.closing>code.size())return false;
    std::size_t depth=1;
    for(auto i=scope.opening+1;i<scope.closing;++i) {
        if(depth==1&&i+pattern.size()<=scope.closing&&
           std::equal(pattern.begin(),pattern.end(),code.begin()+i))
            return true;
        if(open_brace(code[i]))++depth;
        else if(close_brace(code[i])) {
            if(!depth)return false;
            --depth;
        }
    }
    return false;
}

// Count each declared int field once at the direct class level. A comment,
// initializer expression or local variable cannot impersonate an owner field.
inline bool direct_int_fields(const Tokens& code,Scope scope,
        std::initializer_list<std::string_view> names) {
    if(names.size()==0)return false;
    for(const auto name:names) {
        unsigned matches=0;
        std::size_t depth=1;
        for(auto i=scope.opening+1;i<scope.closing;++i) {
            if(depth==1&&i+2<scope.closing&&
               code[i]=="id:int"&&code[i+1]=="id:"+std::string(name)&&
               (code[i+2]=="op:="||code[i+2]=="op:;"||
                code[i+2]=="op:{"||code[i+2]=="op:("))
                ++matches;
            if(open_brace(code[i]))++depth;
            else if(close_brace(code[i])) {
                if(!depth)return false;
                --depth;
            }
        }
        if(matches!=1)return false;
    }
    return true;
}

} // namespace lgaed::curriculum::cpp_class_structure
