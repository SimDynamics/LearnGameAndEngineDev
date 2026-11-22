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
    for(const auto& token:*scanned) {
        if(token.starts_with("comment:"))continue;
        if(token.starts_with("directive:")) {
            // Preprocessing can remove a definition entirely, replace an
            // owner identifier or generate one. Tokens alone cannot prove
            // which class/alias will exist after arbitrary #if/#define.
            // The current authored single-file lessons use only literal
            // includes, which introduce no local declaration tokens.
            if(!code_format::cpp_literal_include(token))return {};
            continue;
        }
        result.push_back(token);
    }
    return result;
}
inline bool open_brace(std::string_view token) {
    return token=="op:{"||token=="op:<%";
}
inline bool close_brace(std::string_view token) {
    return token=="op:}"||token=="op:%>";
}

// A named curriculum class must be defined at translation-unit scope.
// Track actual brace tokens, never braces in comments, strings or raw
// literals. Nested declarations belong to different scopes even when they
// have the same identifier as the original global class.
inline std::optional<std::vector<std::size_t>> brace_depths(const Tokens& code) {
    std::vector<std::size_t> result;
    result.reserve(code.size());
    std::size_t depth=0;
    for(const auto& token:code) {
        result.push_back(depth);
        if(open_brace(token))++depth;
        else if(close_brace(token)) {
            if(!depth)return {};
            --depth;
        }
    }
    if(depth)return {};
    return result;
}

// An enum class/struct is not a class with data members or virtual methods.
// Nor is a class template a concrete type usable by the authored driver.
// Inspect the top-level declaration prefix, not an arbitrary nearby
// "template" in a previous, already terminated declaration.
inline bool plain_class_declaration(const Tokens& code,
        const std::vector<std::size_t>& depths,std::size_t at) {
    if(at && code[at-1]=="id:enum")return false;
    for(auto i=at;i>0;) {
        --i;
        if(close_brace(code[i])&&depths[i]==1)break;
        if(depths[i]!=0)continue;
        if(code[i]=="op:;"||close_brace(code[i]))break;
        if(code[i]=="id:template")return false;
    }
    return true;
}

// Bind a genuine translation-unit class/struct definition and (when
// declared) direct public inheritance. Reject ambiguous duplicate global
// definitions, malformed scopes and nested lookalikes.
inline std::optional<Scope> defined_class(const Tokens& code,
        std::string_view name,std::string_view public_base={}) {
    std::optional<Scope> found;
    const auto self="id:"+std::string(name);
    const auto depths=brace_depths(code);
    if(!depths)return {};
    for(std::size_t i=0;i+2<code.size();++i) {
        if((*depths)[i]!=0||
           (code[i]!="id:class"&&code[i]!="id:struct")||
            code[i+1]!=self||!plain_class_declaration(code,*depths,i))continue;
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
    const auto depths=brace_depths(code);
    if(!depths)return {};
    for(std::size_t i=0;i+3<code.size();++i) {
        if((*depths)[i]!=0||
           (code[i]!="id:class"&&code[i]!="id:struct")||
           code[i+1]!=self||!plain_class_declaration(code,*depths,i))continue;
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

// A named alias required by the published source must be a genuine
// translation-unit declaration. A matching alias inside a function, class
// or namespace cannot impersonate the global API used by the student.
// Reject multiple top-level aliases and a wrong top-level definition even
// if an unrelated local declaration happens to match the expected tokens.
inline bool global_alias(const Tokens& code,std::string_view name,
        std::initializer_list<std::string> expected) {
    if(name.empty()||expected.size()<4)return false;
    std::size_t depth=0;
    bool found=false;
    for(std::size_t i=0;i<code.size();++i) {
        if(depth==0&&i+1<code.size()&&
           code[i]=="id:using"&&code[i+1]=="id:"+std::string(name)) {
            if(found||i+expected.size()>code.size()||
               !std::equal(expected.begin(),expected.end(),
                           code.begin()+static_cast<std::ptrdiff_t>(i)))
                return false;
            found=true;
        }
        if(open_brace(code[i]))++depth;
        else if(close_brace(code[i])) {
            if(!depth)return false;
            --depth;
        }
    }
    return found&&depth==0;
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

// A static or thread-local member may be referenced through an object, but
// does not belong to that individual object's lifetime. These source-policy
// checks require real per-instance RAII fields, not shared storage. Stop at
// the previous declaration, access label or member-function body boundary.
// C++ digraph braces (<% and %>) are the same boundaries as { and }.
inline bool non_instance_storage_modifier(const Tokens& code,
        std::size_t type_at,std::size_t owner_opening) {
    for(auto i=type_at;i>owner_opening+1;) {
        const auto& token=code[--i];
        if(token=="op:;"||open_brace(token)||close_brace(token)||
           token=="op::")break;
        if(token=="id:static"||token=="id:thread_local")
            return true;
    }
    return false;
}

// Require exactly one real direct data member of the selected owner. A
// same-spelled method return type, nested local variable or unrelated class
// cannot authenticate a published RAII field. Accept default, copy and brace
// initialization without relying on the author's formatting.
inline bool direct_field(const Tokens& code,Scope scope,
        std::initializer_list<std::string> type_and_name) {
    if(type_and_name.size()<2||scope.opening>=scope.closing||
       scope.closing>code.size())return false;
    std::size_t count=0,depth=1;
    for(auto i=scope.opening+1;i<scope.closing;++i) {
        const auto end=i+type_and_name.size();
        if(depth==1&&end<scope.closing&&
           std::equal(type_and_name.begin(),type_and_name.end(),
                      code.begin()+static_cast<std::ptrdiff_t>(i))&&
           (code[end]=="op:;"||code[end]=="op:="||
            code[end]=="op:{")&&
           !non_instance_storage_modifier(code,i,scope.opening))++count;
        if(open_brace(code[i]))++depth;
        else if(close_brace(code[i])) {
            if(!depth)return false;
            --depth;
        }
    }
    return count==1;
}

// Count each declared int field once at the direct class level. A comment,
// initializer expression, local variable, static field or zero-argument member
// function cannot impersonate an instance field. Parentheses after the name
// declare a function, not a valid non-static data member initializer.
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
                code[i+2]=="op:{")&&
               !non_instance_storage_modifier(code,i,scope.opening))
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
