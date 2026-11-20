#pragma once

#include "core/cpp_member_body_check.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace lgaed::curriculum {

// A narrowly defined, reusable two-slot unique_ptr<int> runtime witness.
// All names and the original lesson source are supplied by trusted authoring,
// never by the student's edited code. No JSON fields or achievements are added.
// This returns a private program for an independently compiled execution,
// not a verdict from stdout or a source-only proof.
struct CppIndexedUniqueTransferProbe {
    CppMemberBodyCheck member;
    std::string class_name, first_member, second_member;
    std::string transfer_method, put_method, change_method, clear_method;
    bool operator==(const CppIndexedUniqueTransferProbe&) const = default;
};

namespace cpp_indexed_transfer_detail {
inline bool name(std::string_view text) {
    if(text.empty() || text.size()>96 || text.starts_with("__") ||
       (text.size()>1 && text[0]=='_' && text[1]>='A' && text[1]<='Z')) return false;
    const auto start=[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
    const auto next=[&](unsigned char c){return start(c)||(c>='0'&&c<='9');};
    return start(static_cast<unsigned char>(text.front())) &&
        std::ranges::all_of(text.substr(1),next);
}
inline void replace(std::string& source,std::string_view field,std::string_view value) {
    for(std::size_t at=0;(at=source.find(field,at))!=source.npos;) {
        source.replace(at,field.size(),value);
        at+=value.size();
    }
}
} // namespace cpp_indexed_transfer_detail

inline std::optional<std::string> cpp_indexed_unique_transfer_probe_source(
        const CppIndexedUniqueTransferProbe& spec,std::string_view candidate) {
    namespace detail=cpp_indexed_transfer_detail;
    for(const auto& name:{std::string_view(spec.class_name),
            std::string_view(spec.first_member),std::string_view(spec.second_member),
            std::string_view(spec.transfer_method),std::string_view(spec.put_method),
            std::string_view(spec.change_method),std::string_view(spec.clear_method)})
        if(!detail::name(name))return {};
    if(spec.first_member==spec.second_member ||
       spec.member.signature.find(spec.class_name+"::"+spec.transfer_method+"(")==
           spec.member.signature.npos ||
       !cpp_member_body_preserves_source(spec.member,candidate))return {};

    // Attach only a friend witness to the original class, not another owner,
    // another implementation or a replacement public API. The student source
    // was authenticated before this strictly private instrumentation.
    const std::string class_declaration="class "+spec.class_name+" {";
    const auto found=candidate.find(class_declaration);
    if(found==candidate.npos || candidate.find(class_declaration,found+1)!=candidate.npos)
        return {};
    const auto class_open=found+class_declaration.size()-1;
    const auto class_close=cpp_member_body_detail::closing_brace(candidate,class_open);
    if(!class_close || candidate.substr(*class_close+1).find(';')==std::string_view::npos)
        return {};
    std::string instrumented(candidate);
    instrumented.insert(class_open+1,"\n    friend struct lgaed_transfer_qa::Access;\n");

    static constexpr std::string_view prelude=R"probe(#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <new>

namespace lgaed_transfer_qa {
struct Access;
struct Watch {
    inline static bool active=false;
    inline static unsigned created=0,deleted=0;
    inline static const void* source=nullptr;
    inline static const void* destination=nullptr;
    inline static bool deleted_source=false,deleted_destination=false;
    static void begin(const void* from,const void* to) noexcept {
        created=deleted=0;
        source=from;destination=to;
        deleted_source=deleted_destination=false;
        active=true;
    }
    static void end() noexcept {
        active=false;
        source=destination=nullptr;
    }
    static void on_delete(const void* ptr) noexcept {
        if(!active || !ptr)return;
        ++deleted;
        if(ptr==source){deleted_source=true;source=nullptr;}
        if(ptr==destination){deleted_destination=true;destination=nullptr;}
    }
};
}
void* operator new(std::size_t amount) {
    if(void* p=std::malloc(amount?amount:1)){
        if(lgaed_transfer_qa::Watch::active)
            ++lgaed_transfer_qa::Watch::created;
        return p;
    }
    throw std::bad_alloc{};
}
void operator delete(void* ptr) noexcept {
    lgaed_transfer_qa::Watch::on_delete(ptr);
    std::free(ptr);
}
void operator delete(void* ptr,std::size_t) noexcept { ::operator delete(ptr); }
void* operator new[](std::size_t amount) {
    if(void* p=std::malloc(amount?amount:1)){
        if(lgaed_transfer_qa::Watch::active)
            ++lgaed_transfer_qa::Watch::created;
        return p;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* ptr) noexcept {
    lgaed_transfer_qa::Watch::on_delete(ptr);
    std::free(ptr);
}
void operator delete[](void* ptr,std::size_t) noexcept { ::operator delete[](ptr); }

#define main lgaed_student_original_main
)probe";

    static constexpr std::string_view private_driver=R"probe(
#undef main

namespace lgaed_transfer_qa {
struct Access {
    static const int* slot(const @CLASS@& d,int index) noexcept {
        return index==0?d.@FIRST@.get():d.@SECOND@.get();
    }
};
struct Snapshot {
    const int* first=nullptr;
    const int* second=nullptr;
    int first_value=0,second_value=0;
};
Snapshot snapshot(const @CLASS@& d) {
    Snapshot result;
    result.first=Access::slot(d,0);
    result.second=Access::slot(d,1);
    if(result.first)result.first_value=*result.first;
    if(result.second)result.second_value=*result.second;
    return result;
}
// Compare source identity while its object is alive; observe retirement
// inside operator delete, before freeing the former destination. Never
// compare or dereference a pointer after its object has been destroyed.
bool run(@CLASS@& d,int from,int to,bool should_accept) {
    const auto before=snapshot(d);
    const auto incoming=from==0?before.first:from==1?before.second:nullptr;
    const auto retired=to==0?before.first:to==1?before.second:nullptr;
    Watch::begin(should_accept?incoming:nullptr,should_accept?retired:nullptr);
    const bool accepted=d.@TRANSFER@(from,to);
    Watch::end();
    if(accepted!=should_accept || Watch::created!=0 || Watch::deleted_source)
        return false;
    if(!should_accept) {
        // If a rejected operation deleted any owner, do not inspect a
        // potentially dead pointer. The rejection is already decisive.
        if(Watch::deleted!=0)return false;
        const auto after=snapshot(d);
        return before.first==after.first && before.second==after.second &&
            before.first_value==after.first_value &&
            before.second_value==after.second_value;
    }
    if(!incoming || from==to || from<0 || from>1 || to<0 || to>1 ||
       Watch::deleted!=(retired?1u:0u) ||
       (retired && !Watch::deleted_destination))return false;
    const auto new_source=Access::slot(d,from);
    const auto new_destination=Access::slot(d,to);
    return !new_source && new_destination==incoming &&
        *new_destination==(from==0?before.first_value:before.second_value);
}
}

int main() {
    using lgaed_transfer_qa::run;
    using lgaed_transfer_qa::Watch;
    {
        @CLASS@ d;
        if(!run(d,0,1,true) || !run(d,0,1,false))return 1;
    }
    {
        @CLASS@ d;
        if(!d.@CHANGE@(1,4) || !run(d,0,1,true))return 2;
    }
    {
        @CLASS@ d;
        if(!d.@PUT@(0,0) || !run(d,0,1,true))return 3;
    }
    {
        @CLASS@ d;
        if(!run(d,0,0,false) || !run(d,1,1,false) ||
           !run(d,-1,0,false) || !run(d,0,2,false) ||
           !run(d,2,1,false) || !run(d,1,-1,false))return 4;
        if(!run(d,1,0,true) || !run(d,1,0,false))return 5;
        if(!d.@PUT@(1,6) || !run(d,1,0,true))return 6;
    }
    {
        @CLASS@ d;
        if(!d.@CLEAR@(1) || !run(d,0,1,true))return 7;
    }
    // Fresh construction + transfer + destruction must allocate twice and
    // release twice, including the replaced destination's original int.
    Watch::created=Watch::deleted=0;
    Watch::active=true;
    bool moved=false;
    {
        @CLASS@ d;
        moved=d.@TRANSFER@(0,1);
    }
    Watch::active=false;
    if(!moved || Watch::created!=2 || Watch::deleted!=2)return 8;
    std::fputs("PASS native independent two-owner transfer identity and cleanup\n",stderr);
    return 0;
}
)probe";

    std::string driver(private_driver);
    detail::replace(driver,"@CLASS@",spec.class_name);
    detail::replace(driver,"@FIRST@",spec.first_member);
    detail::replace(driver,"@SECOND@",spec.second_member);
    detail::replace(driver,"@TRANSFER@",spec.transfer_method);
    detail::replace(driver,"@PUT@",spec.put_method);
    detail::replace(driver,"@CHANGE@",spec.change_method);
    detail::replace(driver,"@CLEAR@",spec.clear_method);
    return std::string(prelude)+instrumented+driver;
}

} // namespace lgaed::curriculum
