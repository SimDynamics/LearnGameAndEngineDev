#pragma once
// Preliminary native proof of an explicitly declared identifier-only rewrite.
// This is NOT a C++ semantic-equivalence, binding, or student-state migrator.
// A published state-layout archive must be authenticated separately before
// any curricular source change can be loaded or credited.
#include "core/code_format_compat.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum::identifier_rename {

struct SourceSlot {
    std::string id;
    code_format::Language language=code_format::Language::immutable;
    std::string source;
};
struct NamedRename {
    std::string original;
    std::string replacement;
    std::size_t occurrences=0;
};
struct SourcePlan {
    std::string slot;
    std::vector<NamedRename> names;
};

inline bool ascii_alpha(unsigned char c) noexcept {
    return (c>='A'&&c<='Z')||(c>='a'&&c<='z');
}
inline bool ascii_digit(unsigned char c) noexcept {
    return c>='0'&&c<='9';
}
inline bool identifier(std::string_view name) {
    // Match Python's explicit ASCII regex in every C/C++ locale. std::isalpha
    // is locale-sensitive and can admit non-ASCII bytes on Windows profiles,
    // producing different source-name permissions on the two validators.
    if(name.empty()||!ascii_alpha(static_cast<unsigned char>(name.front()))||
       !std::all_of(name.begin()+1,name.end(),[](unsigned char c){
            return ascii_alpha(c)||ascii_digit(c)||c=='_';}))return false;
    static const std::set<std::string_view> reserved{
        "alignas","alignof","and","and_eq","asm","auto","bitand","bitor",
        "bool","break","case","catch","char","char8_t","char16_t",
        "char32_t","class","compl","concept","const","consteval",
        "constexpr","constinit","const_cast","continue","co_await",
        "co_return","co_yield","decltype","default","delete","do",
        "double","dynamic_cast","else","enum","explicit","export",
        "extern","false","float","for","friend","goto","if","inline",
        "int","long","mutable","namespace","new","noexcept","not",
        "not_eq","nullptr","operator","or","or_eq","private",
        "protected","public","register","reinterpret_cast","requires",
        "return","short","signed","sizeof","static","static_assert",
        "static_cast","struct","switch","template","this",
        "thread_local","throw","true","try","typedef","typeid",
        "typename","union","unsigned","using","virtual","void",
        "volatile","wchar_t","while","xor","xor_eq",
        "import","module","final","override"
    };
    return !reserved.contains(name);
}
inline bool identifier_char(unsigned char c) noexcept {
    return ascii_alpha(c)||ascii_digit(c)||c=='_';
}
inline bool directive_mentions(std::string_view directive,std::string_view name) {
    for(auto start=directive.find(name);start!=directive.npos;
        start=directive.find(name,start+1)) {
        const auto end=start+name.size();
        const bool left=start==0||!identifier_char(
            static_cast<unsigned char>(directive[start-1]));
        const bool right=end==directive.size()||!identifier_char(
            static_cast<unsigned char>(directive[end]));
        if(left&&right)return true;
    }
    return false;
}
// A lexical identifier-only change can alter observable output through
// __func__/compiler function-name intrinsics or macro argument stringization.
// Guard both active and unaffected source slots: macros from another file may
// receive changed identifiers across the project.
inline bool spelling_sensitive_tokens(const std::vector<std::string>& tokens) {
    static constexpr std::string_view observers[]{
        "__func__", "__FUNCTION__", "__PRETTY_FUNCTION__",
        "__FUNCSIG__", "__FUNCDNAME__", "typeid"
    };
    for(const auto& token:tokens) {
        if(token.starts_with("id:")) {
            const auto name=std::string_view(token).substr(3);
            if(std::ranges::find(observers,name)!=std::end(observers))
                return true;
        }
        if(!token.starts_with("directive:"))continue;
        auto directive=std::string_view(token).substr(10);
        if(directive.starts_with("#"))directive.remove_prefix(1);
        else if(directive.starts_with("%:"))directive.remove_prefix(2);
        while(!directive.empty()&&(directive.front()==' '||
              directive.front()=='\t'))directive.remove_prefix(1);
        if(!directive.starts_with("define")||
           (directive.size()>6&&identifier_char(
                static_cast<unsigned char>(directive[6]))))
            continue;
        directive.remove_prefix(6);
        if(directive.find('#')!=directive.npos||
           directive.find("%:")!=directive.npos)return true;
    }
    return false;
}
// The caller must separately pin the complete block, answer/oracle metadata,
// stable identity, editorial proof, text semantics and storage lineage.
// Return true only if every changed C++ token is an explicitly counted
// identifier rewrite. Reject lexical ambiguities, macros and renamed sources
// with a stale reference in another, unreviewed source slot.
[[nodiscard]] inline bool declared_tokens_only(
    const std::vector<SourceSlot>& original,
    const std::vector<SourceSlot>& revised,
    const std::vector<SourcePlan>& declarations) {
    if(original.empty()||original.size()!=revised.size()||
       declarations.empty()||declarations.size()>original.size())return false;
    std::map<std::string,SourceSlot> old_slots,new_slots;
    for(const auto& slot:original)
        if(slot.id.empty()||!old_slots.emplace(slot.id,slot).second)return false;
    for(const auto& slot:revised)
        if(slot.id.empty()||!new_slots.emplace(slot.id,slot).second)return false;
    if(old_slots.size()!=new_slots.size())return false;
    for(const auto& [id,old]:old_slots) {
        const auto current=new_slots.find(id);
        if(current==new_slots.end()||current->second.language!=old.language)
            return false;
    }

    std::map<std::string,std::map<std::string,NamedRename>> declared;
    std::map<std::string,std::string> old_to_new,new_to_old;
    for(const auto& plan:declarations) {
        if(!old_slots.contains(plan.slot)||
           old_slots.at(plan.slot).language!=code_format::Language::cpp||
           plan.names.empty()||declared.contains(plan.slot))return false;
        auto& names=declared[plan.slot];
        for(const auto& entry:plan.names) {
            if(!identifier(entry.original)||!identifier(entry.replacement)||
               entry.original==entry.replacement||entry.occurrences==0||
               !names.emplace(entry.original,entry).second)return false;
            if(const auto old=old_to_new.find(entry.original);
               old!=old_to_new.end()&&old->second!=entry.replacement)return false;
            if(const auto old=new_to_old.find(entry.replacement);
               old!=new_to_old.end()&&old->second!=entry.original)return false;
            old_to_new[entry.original]=entry.replacement;
            new_to_old[entry.replacement]=entry.original;
        }
    }

    // An unchanged project file can contain a macro or a reference to a
    // renamed identifier from another translation unit. It cannot be skipped.
    for(const auto& [id,slot]:old_slots) {
        if(slot.language==code_format::Language::cmake) {
            // An unchanged CMake file may define macros for renamed C++
            // identifiers. Only live arguments are checked; a CMake comment
            // cannot expand into the compiler command line.
            const auto tokens=code_format::cmake_tokens(slot.source);
            if(!tokens)return false;
            for(const auto& token:*tokens) {
                if(!(token.starts_with("word:")||
                     token.starts_with("quoted:")||
                     (token.starts_with("bracket:")&&
                      !token.starts_with("bracket:#"))))continue;
                for(const auto& [before,after]:old_to_new)
                    if(directive_mentions(token,before)||
                       directive_mentions(token,after))return false;
            }
            continue;
        }
        if(slot.language!=code_format::Language::cpp)continue;
        const auto tokens=code_format::cpp_tokens(slot.source);
        if(!tokens||spelling_sensitive_tokens(*tokens))return false;
        for(const auto& token:*tokens) {
            if(token.starts_with("directive:")) {
                for(const auto& [before,after]:old_to_new)
                    if(directive_mentions(token,before)||
                       directive_mentions(token,after))return false;
            }
            if(!token.starts_with("id:"))continue;
            const auto symbol=token.substr(3);
            if(new_to_old.contains(symbol))return false;
            if(!declared.contains(id)&&old_to_new.contains(symbol))return false;
        }
    }

    for(const auto& [id,before]:old_slots) {
        const auto& after=new_slots.at(id);
        const auto plan=declared.find(id);
        if(plan==declared.end()) {
            if(after.source!=before.source)return false;
            continue;
        }
        if(before.language!=code_format::Language::cpp||
           after.source==before.source||
           std::count(before.source.begin(),before.source.end(),'\n')!=
                std::count(after.source.begin(),after.source.end(),'\n')||
           code_format::physical_splice(before.source)||
           code_format::physical_splice(after.source))return false;
        const auto a=code_format::cpp_tokens(before.source);
        const auto b=code_format::cpp_tokens(after.source);
        if(!a||!b||a->size()!=b->size()||
           code_format::cpp_position_sensitive_tokens(*a)||
           code_format::cpp_position_sensitive_tokens(*b)||
           std::ranges::find(*a,"op:\\")!=a->end()||
           std::ranges::find(*b,"op:\\")!=b->end())return false;
        std::map<std::string,std::size_t> seen,changed;
        for(std::size_t i=0;i<a->size();++i) {
            const auto& old=(*a)[i],&now=(*b)[i];
            if(old.starts_with("id:")) {
                const auto name=old.substr(3);
                if(plan->second.contains(name))++seen[name];
            }
            if(old==now)continue;
            if(!old.starts_with("id:"))return false;
            const auto entry=plan->second.find(old.substr(3));
            if(entry==plan->second.end()||
               now!="id:"+entry->second.replacement)return false;
            ++changed[entry->first];
        }
        for(const auto& [name,entry]:plan->second)
            if(seen[name]!=entry.occurrences||
               changed[name]!=entry.occurrences)return false;
    }
    return true;
}
} // namespace lgaed::curriculum::identifier_rename
