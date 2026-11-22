#pragma once

#include "core/code_format_compat.hpp"
#include "core/cpp_private_probe_neutral.hpp"
#include "core/cpp_member_body_check.hpp"
#include "core/cpp_class_structure_check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Root-authorized additional proof for exactly two member assignment bodies.
// The original class, constructors, helper functions and main stay preserved.
// Behavioral evidence is gathered only by compiling a separate private driver.
struct CppArrayCopyMovePolicy {
    std::string original_source, class_name, owner_member, count_member;
    std::string copy_parameter, move_parameter;
    std::string quantity_method, access_method, configure_method, read_method;
    std::string write_method, clear_method, sum_method;
    bool operator==(const CppArrayCopyMovePolicy&) const = default;
};

inline constexpr std::string_view cpp_array_copy_move_completion_marker =
    "LGAED_ARRAY_COPY_MOVE_SEMANTICS_V1";

namespace cpp_array_copy_move_detail {
inline bool identifier(std::string_view value) {
    if(value.empty()||value.size()>96||value.starts_with("__")||
       (value.size()>1&&value[0]=='_'&&value[1]>='A'&&value[1]<='Z'))
        return false;
    const auto letter=[](unsigned char ch) {
        return ch=='_'||(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z');
    };
    if(!letter(static_cast<unsigned char>(value.front())))return false;
    return std::ranges::all_of(value.substr(1),[&](unsigned char ch){
        return letter(ch)||(ch>='0'&&ch<='9');
    });
}
inline std::string copy_signature(const CppArrayCopyMovePolicy& p) {
    return p.class_name+"& "+p.class_name+"::operator=(const "+
        p.class_name+"& "+p.copy_parameter+")";
}
inline std::string move_signature(const CppArrayCopyMovePolicy& p) {
    return p.class_name+"& "+p.class_name+"::operator=("+
        p.class_name+"&& "+p.move_parameter+") noexcept";
}
inline bool owns_original_members(const CppArrayCopyMovePolicy& p) {
    const auto code=cpp_class_structure::lexical(p.original_source);
    if(!code)return false;
    const auto owner=cpp_class_structure::defined_class(*code,p.class_name);
    return owner &&
        cpp_class_structure::direct_field(*code,*owner,{
            "id:std","op:::","id:unique_ptr","op:<","id:int",
            "op:[","op:]","op:>","id:"+p.owner_member})&&
        cpp_class_structure::direct_field(*code,*owner,{
            "id:int","id:"+p.count_member});
}
inline bool policy_valid(const CppArrayCopyMovePolicy& p) {
    for(const auto item:{std::string_view(p.class_name),
            std::string_view(p.owner_member),std::string_view(p.count_member),
            std::string_view(p.copy_parameter),std::string_view(p.move_parameter),
            std::string_view(p.quantity_method),std::string_view(p.access_method),
            std::string_view(p.configure_method),std::string_view(p.read_method),
            std::string_view(p.write_method),std::string_view(p.clear_method),
            std::string_view(p.sum_method)})
        if(!identifier(item))return false;
    if(p.original_source.empty()||p.original_source.size()>4*1024*1024||
       p.original_source.find('\0')!=p.original_source.npos||
       code_format::physical_splice(p.original_source)||
       !owns_original_members(p))return false;
    const auto copy=cpp_member_body_span(
        CppMemberBodyCheck{p.original_source,copy_signature(p)});
    const auto move=cpp_member_body_span(
        CppMemberBodyCheck{p.original_source,move_signature(p)});
    return copy&&move&&copy->end<move->begin;
}
inline bool safe_body(std::string_view code,const CppMemberBodySpan& span) {
    if(span.begin>span.end||span.end>code.size())return false;
    const auto body=code.substr(span.begin,span.end-span.begin);
    if(code_format::physical_splice(body))return false;
    const auto lexical=code_format::cpp_tokens(body);
    return lexical &&
        cpp_private_probe_body_neutral(*lexical,"lgaed_array_copy_move_qa");
}
inline void substitute(std::string& text,std::string_view key,std::string_view replacement) {
    for(std::size_t at=0;(at=text.find(key,at))!=text.npos;) {
        text.replace(at,key.size(),replacement);
        at+=replacement.size();
    }
}
} // namespace cpp_array_copy_move_detail

inline bool cpp_array_copy_move_matches(const CppArrayCopyMovePolicy& p,
                                        std::string_view candidate) {
    namespace d=cpp_array_copy_move_detail;
    if(!d::policy_valid(p)||candidate.empty()||candidate.size()>4*1024*1024||
       candidate.find('\0')!=candidate.npos||
       code_format::physical_splice(candidate))return false;
    const auto before_copy=cpp_member_body_span(
        CppMemberBodyCheck{p.original_source,d::copy_signature(p)});
    const auto before_move=cpp_member_body_span(
        CppMemberBodyCheck{p.original_source,d::move_signature(p)});
    const std::string revised(candidate);
    const auto after_copy=cpp_member_body_span(
        CppMemberBodyCheck{revised,d::copy_signature(p)});
    const auto after_move=cpp_member_body_span(
        CppMemberBodyCheck{revised,d::move_signature(p)});
    if(!before_copy||!before_move||!after_copy||!after_move||
       before_copy->end>=before_move->begin||
       after_copy->end>=after_move->begin||
       !d::safe_body(candidate,*after_copy)||!d::safe_body(candidate,*after_move))
        return false;
    // Three protected intervals keep all class declarations, constructors,
    // other methods, and main byte-identical. Only two bodies may vary.
    const auto& old=p.original_source;
    return std::string_view(old).substr(0,before_copy->begin)==
           candidate.substr(0,after_copy->begin)&&
       std::string_view(old).substr(
           before_copy->end,before_move->begin-before_copy->end)==
           candidate.substr(after_copy->end,
                            after_move->begin-after_copy->end)&&
       std::string_view(old).substr(before_move->end)==
           candidate.substr(after_move->end);
}

inline std::optional<std::string> cpp_array_copy_move_private_driver(
        const CppArrayCopyMovePolicy& p,std::string_view candidate) {
    if(!cpp_array_copy_move_matches(p,candidate))return {};
    static constexpr std::string_view prelude=R"lgaed(#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace lgaed_array_copy_move_qa {
struct Watch {
    inline static bool active = false, fail_next_array = false;
    inline static unsigned array_allocs = 0, array_frees = 0;
    inline static unsigned single_allocs = 0, single_frees = 0;
    inline static const void* last_array = nullptr;
    inline static const void* to_release = nullptr;
    inline static bool released_expected = false;
    static void expect_release(const void* pointer) noexcept {
        to_release = pointer;
        released_expected = false;
    }
    static void on_delete(const void* pointer, bool array) noexcept {
        if (!active || !pointer) return;
        if (array) ++array_frees; else ++single_frees;
        if (array && pointer == to_release) {
            released_expected = true;
            to_release = nullptr;
        }
    }
};
}
void* operator new(std::size_t bytes) {
    if (void* pointer = std::malloc(bytes ? bytes : 1)) {
        if (lgaed_array_copy_move_qa::Watch::active)
            ++lgaed_array_copy_move_qa::Watch::single_allocs;
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept {
    lgaed_array_copy_move_qa::Watch::on_delete(pointer, false);
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}
void* operator new[](std::size_t bytes) {
    using lgaed_array_copy_move_qa::Watch;
    if (Watch::fail_next_array) {
        Watch::fail_next_array = false;
        throw std::bad_alloc{};
    }
    if (void* pointer = std::malloc(bytes ? bytes : 1)) {
        if (Watch::active) {
            ++Watch::array_allocs;
            Watch::last_array = pointer;
        }
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* pointer) noexcept {
    lgaed_array_copy_move_qa::Watch::on_delete(pointer, true);
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    ::operator delete[](pointer);
}
#define main lgaed_original_tramo_main
)lgaed";
    static constexpr std::string_view driver=R"lgaed(
#undef main
int main() {
    using lgaed_array_copy_move_qa::Watch;
    const auto equal=[](const @CLASS@& value, std::array<int,4> expected,
                        int length) noexcept -> bool {
        if (value.@QUANTITY@()!=length ||
            (value.@ACCESS@()!=nullptr)!=(length!=0)) return false;
        for (int i=0;i<length;++i) {
            int observed=-10;
            if (!value.@READ@(i,observed) || observed!=expected[i])return false;
        }
        return true;
    };
    Watch::active=true;
    {
        @CLASS@ source,destination;
        if (!source.@CONFIGURE@(3)||!destination.@CONFIGURE@(2)||
            !source.@WRITE@(0,5)||!source.@WRITE@(1,0)||
            !source.@WRITE@(2,8)||!destination.@WRITE@(0,9)||
            !destination.@WRITE@(1,7)||Watch::array_allocs!=2)return 1;
        const int* source_identity=source.@ACCESS@();
        Watch::expect_release(destination.@ACCESS@());
        const auto made=Watch::array_allocs,freed=Watch::array_frees;
        @CLASS@& copied=(destination=source);
        if (&copied!=&destination||!equal(source,{5,0,8,0},3)||
            !equal(destination,{5,0,8,0},3)||
            destination.@ACCESS@()==source_identity||
            source.@ACCESS@()!=source_identity||
            destination.@ACCESS@()!=Watch::last_array||
            Watch::array_allocs!=made+1||Watch::array_frees!=freed+1||
            !Watch::released_expected)return 2;

        // Equal size and values still require a freshly allocated array.
        Watch::expect_release(destination.@ACCESS@());
        const auto made_again=Watch::array_allocs,freed_again=Watch::array_frees;
        destination=source;
        if (!equal(destination,{5,0,8,0},3)||
            destination.@ACCESS@()==source_identity||
            destination.@ACCESS@()!=Watch::last_array||
            source.@ACCESS@()!=source_identity||
            Watch::array_allocs!=made_again+1||
            Watch::array_frees!=freed_again+1||
            !Watch::released_expected)return 3;

        // A self-copy, including an occupied array containing zero, is inert.
        @CLASS@& copy_alias=source;
        const auto self_owner=source.@ACCESS@();
        const auto self_made=Watch::array_allocs,self_freed=Watch::array_frees;
        @CLASS@& self_return=(source=copy_alias);
        if (&self_return!=&source||source.@ACCESS@()!=self_owner||
            !equal(source,{5,0,8,0},3)||
            Watch::array_allocs!=self_made||Watch::array_frees!=self_freed)
            return 4;

        @CLASS@ empty;
        @CLASS@& empty_alias=empty;
        const auto empty_self_made=Watch::array_allocs;
        const auto empty_self_freed=Watch::array_frees;
        @CLASS@& empty_self_return=(empty=empty_alias);
        if (&empty_self_return!=&empty||!equal(empty,{0,0,0,0},0)||
            Watch::array_allocs!=empty_self_made||
            Watch::array_frees!=empty_self_freed)return 5;

        // An empty source clears the target and frees its old array.
        Watch::expect_release(destination.@ACCESS@());
        const auto empty_made=Watch::array_allocs,empty_freed=Watch::array_frees;
        @CLASS@& empty_copy_return=(destination=empty);
        if (&empty_copy_return!=&destination||
            !equal(destination,{0,0,0,0},0)||
            !equal(empty,{0,0,0,0},0)||
            Watch::array_allocs!=empty_made||
            Watch::array_frees!=empty_freed+1||
            !Watch::released_expected)return 6;

        // A deterministic failed array allocation must not consume the old
        // destination or modify either source, even when values differ.
        @CLASS@ occupied;
        if(!occupied.@CONFIGURE@(2)||!occupied.@WRITE@(0,4)||
           !occupied.@WRITE@(1,6))return 7;
        const int* prior_target=occupied.@ACCESS@();
        const int* prior_source=source.@ACCESS@();
        const auto fail_made=Watch::array_allocs,fail_freed=Watch::array_frees;
        Watch::fail_next_array=true;
        bool caught=false;
        try { occupied=source; }
        catch(const std::bad_alloc&) {caught=true;}
        Watch::fail_next_array=false;
        if(!caught||occupied.@ACCESS@()!=prior_target||
           source.@ACCESS@()!=prior_source||
           !equal(occupied,{4,6,0,0},2)||!equal(source,{5,0,8,0},3)||
           Watch::array_allocs!=fail_made||Watch::array_frees!=fail_freed)
            return 8;

        // Move into an existing target: transfer the same object, release old
        // destination and leave the source owner empty (including count).
        Watch::expect_release(occupied.@ACCESS@());
        const auto move_made=Watch::array_allocs,move_freed=Watch::array_frees;
        const int* transferred=source.@ACCESS@();
        @CLASS@& moved=(occupied=std::move(source));
        if(&moved!=&occupied||!equal(occupied,{5,0,8,0},3)||
           occupied.@ACCESS@()!=transferred||
           !equal(source,{0,0,0,0},0)||
           Watch::array_allocs!=move_made||Watch::array_frees!=move_freed+1||
           !Watch::released_expected)return 9;

        // Empty move destroys old occupied destination without allocation.
        Watch::expect_release(occupied.@ACCESS@());
        const auto empty_move_made=Watch::array_allocs;
        const auto empty_move_freed=Watch::array_frees;
        @CLASS@& empty_move_return=(occupied=std::move(source));
        if(&empty_move_return!=&occupied||!equal(occupied,{0,0,0,0},0)||
           !equal(source,{0,0,0,0},0)||
           Watch::array_allocs!=empty_move_made||
           Watch::array_frees!=empty_move_freed+1||
           !Watch::released_expected)return 10;

        // Self move must preserve pointer, count and values.
        if(!source.@CONFIGURE@(2)||!source.@WRITE@(0,0)||
           !source.@WRITE@(1,9))return 11;
        const int* self_move_owner=source.@ACCESS@();
        @CLASS@& move_alias=source;
        const auto self_move_made=Watch::array_allocs;
        const auto self_move_freed=Watch::array_frees;
        @CLASS@& self_move_return=(source=std::move(move_alias));
        if(&self_move_return!=&source||source.@ACCESS@()!=self_move_owner||
           !equal(source,{0,9,0,0},2)||
           Watch::array_allocs!=self_move_made||
           Watch::array_frees!=self_move_freed)return 12;
        source.@CLEAR@();
        @CLASS@& self_empty_alias=source;
        const auto empty_self_move_made=Watch::array_allocs;
        const auto empty_self_move_freed=Watch::array_frees;
        @CLASS@& self_empty_move=(source=std::move(self_empty_alias));
        if(&self_empty_move!=&source||!equal(source,{0,0,0,0},0)||
           Watch::array_allocs!=empty_self_move_made||
           Watch::array_frees!=empty_self_move_freed)return 13;

        // Identical contents under distinct owners: moving must preserve the
        // original physical owner rather than manufacture an equivalent one.
        @CLASS@ same_a,same_b;
        if(!same_a.@CONFIGURE@(2)||!same_b.@CONFIGURE@(2)||
           !same_a.@WRITE@(0,0)||!same_a.@WRITE@(1,3)||
           !same_b.@WRITE@(0,0)||!same_b.@WRITE@(1,3))return 14;
        const int* same_identity=same_a.@ACCESS@();
        Watch::expect_release(same_b.@ACCESS@());
        const auto same_made=Watch::array_allocs,same_freed=Watch::array_frees;
        same_b=std::move(same_a);
        if(same_b.@ACCESS@()!=same_identity||!equal(same_b,{0,3,0,0},2)||
           !equal(same_a,{0,0,0,0},0)||
           Watch::array_allocs!=same_made||Watch::array_frees!=same_freed+1||
           !Watch::released_expected)return 15;

        // A moved-from array can be independently used again.
        if(!same_a.@CONFIGURE@(1)||!same_a.@WRITE@(0,7)||
           !equal(same_a,{7,0,0,0},1)||
           same_a.@ACCESS@()==same_b.@ACCESS@())return 16;

        // Exception unwinding must delete only live arrays once each.
        const auto pre_unwind_made=Watch::array_allocs;
        const auto pre_unwind_freed=Watch::array_frees;
        bool caught_unwind=false;
        try {
            @CLASS@ a,b;
            if(!a.@CONFIGURE@(3)||!b.@CONFIGURE@(1))return 17;
            b=std::move(a);
            if(!equal(a,{0,0,0,0},0)||!equal(b,{0,0,0,0},3))
                return 18;
            throw 37;
        }catch(int value){caught_unwind=value==37;}
        if(!caught_unwind||Watch::array_allocs!=pre_unwind_made+2||
           Watch::array_frees!=pre_unwind_freed+2)return 19;
    }
    Watch::active=false;
    if(Watch::array_allocs!=Watch::array_frees||
       Watch::single_allocs!=Watch::single_frees||
       Watch::fail_next_array)return 20;
    std::fputs("LGAED_ARRAY_COPY_MOVE_SEMANTICS_V1",stderr);
    return 0;
}
)lgaed";
    auto private_driver=std::string(driver);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,8>{{
        {"@CLASS@",p.class_name},
        {"@QUANTITY@",p.quantity_method},{"@ACCESS@",p.access_method},
        {"@CONFIGURE@",p.configure_method},{"@READ@",p.read_method},
        {"@WRITE@",p.write_method},{"@CLEAR@",p.clear_method},
        {"@SUM@",p.sum_method}}})
        cpp_array_copy_move_detail::substitute(private_driver,key,value);
    // Substitute only the trusted test harness, never student source/literals.
    return std::string(prelude)+std::string(candidate)+private_driver;
}

} // namespace lgaed::curriculum
