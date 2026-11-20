#pragma once

#include "core/code_format_compat.hpp"
#include "core/cpp_member_body_check.hpp"

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

// Root-authored, original-source-bound proof for two distinct *free function*
// bodies. This type preserves the entire original class/main and permits
// any equivalent student solution limited to the two original method bodies.
// A generated program privately proves real array ownership and exceptions.
struct CppArrayReturnTakePolicy {
    std::string original_source, class_name, owner_member, count_member;
    std::string copy_function, take_function, source_parameter;
    std::string index_parameter, value_parameter, destination_parameter;
    std::string offered_parameter, limit_parameter;
    std::string quantity_method, access_method, configure_method;
    std::string read_method, write_method, clear_method;
    bool operator==(const CppArrayReturnTakePolicy&) const = default;
};

inline constexpr std::string_view cpp_array_return_take_completion_marker =
    "LGAED_ARRAY_RETURN_TAKE_COMPLETE_V1";

namespace cpp_array_return_take_detail {
inline bool name(std::string_view value) {
    if(value.empty()||value.size()>96||value.starts_with("__")||
       (value.size()>1&&value[0]=='_'&&value[1]>='A'&&value[1]<='Z'))
        return false;
    const auto alpha=[](unsigned char c){
        return c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z');
    };
    if(!alpha(static_cast<unsigned char>(value.front())))return false;
    return std::ranges::all_of(value.substr(1),[&](unsigned char c){
        return alpha(c)||(c>='0'&&c<='9');
    });
}
inline std::string copy_signature(const CppArrayReturnTakePolicy& p) {
    return p.class_name+" "+p.copy_function+"(const "+
        p.class_name+"& "+p.source_parameter+", int "+
        p.index_parameter+", int "+p.value_parameter+")";
}
inline std::string take_signature(const CppArrayReturnTakePolicy& p) {
    return "bool "+p.take_function+"("+p.class_name+"& "+
        p.destination_parameter+", "+p.class_name+"&& "+
        p.offered_parameter+", int "+p.limit_parameter+") noexcept";
}
inline std::optional<CppMemberBodySpan> free_body_span(
        std::string_view code,std::string_view signature) {
    if(code.empty()||signature.empty()||signature.size()>512||
       code.size()>4*1024*1024||code.find('\0')!=code.npos||
       code_format::physical_splice(code))return {};
    const auto at=code.find(signature);
    if(at==code.npos||code.find(signature,at+1)!=code.npos)return {};
    const auto newline=code.rfind('\n',at);
    const auto line=newline==code.npos?0:newline+1;
    for(auto i=line;i<at;++i)
        if(!cpp_member_body_detail::horizontal_space(code[i]))return {};
    auto opening=at+signature.size();
    while(opening<code.size()&&code_format::space(code[opening]))++opening;
    if(opening>=code.size()||code[opening]!='{')return {};
    const auto close=cpp_member_body_detail::closing_brace(code,opening);
    if(!close||*close<=opening)return {};
    return CppMemberBodySpan{opening+1,*close};
}
inline bool owns_array(const CppArrayReturnTakePolicy& p) {
    const auto scan=code_format::cpp_tokens(p.original_source);
    if(!scan||code_format::cpp_position_sensitive_tokens(*scan))return false;
    const auto& tokens=*scan;
    std::optional<std::size_t> opening;
    for(std::size_t i=0;i+2<tokens.size();++i)
        if((tokens[i]=="id:class"||tokens[i]=="id:struct")&&
           tokens[i+1]=="id:"+p.class_name&&tokens[i+2]=="op:{") {
            if(opening)return false;
            opening=i+2;
        }
    if(!opening)return false;
    std::size_t depth=1,closing=0;
    for(auto i=*opening+1;i<tokens.size();++i) {
        if(tokens[i]=="op:{")++depth;
        else if(tokens[i]=="op:}"&&--depth==0) {
            closing=i;break;
        }
    }
    if(!closing)return false;
    const std::array<std::string,9> pointer{{
        "id:std","op:::","id:unique_ptr","op:<","id:int",
        "op:[","op:]","op:>","id:"+p.owner_member}};
    const std::array<std::string,2> count{{"id:int","id:"+p.count_member}};
    bool owner_found=false,count_found=false;
    for(auto i=*opening+1;i<closing;++i) {
        if(i+pointer.size()<=closing&&std::equal(
            pointer.begin(),pointer.end(),
            tokens.begin()+static_cast<std::ptrdiff_t>(i)))owner_found=true;
        if(i+count.size()<=closing&&std::equal(
            count.begin(),count.end(),
            tokens.begin()+static_cast<std::ptrdiff_t>(i)))count_found=true;
    }
    return owner_found&&count_found;
}
inline bool valid(const CppArrayReturnTakePolicy& p) {
    for(std::string_view id:{std::string_view(p.class_name),
            std::string_view(p.owner_member),std::string_view(p.count_member),
            std::string_view(p.copy_function),std::string_view(p.take_function),
            std::string_view(p.source_parameter),
            std::string_view(p.index_parameter),std::string_view(p.value_parameter),
            std::string_view(p.destination_parameter),
            std::string_view(p.offered_parameter),std::string_view(p.limit_parameter),
            std::string_view(p.quantity_method),std::string_view(p.access_method),
            std::string_view(p.configure_method),std::string_view(p.read_method),
            std::string_view(p.write_method),std::string_view(p.clear_method)})
        if(!name(id))return false;
    const auto copy=free_body_span(p.original_source,copy_signature(p));
    const auto take=free_body_span(p.original_source,take_signature(p));
    return copy&&take&&copy->end<take->begin&&owns_array(p);
}
inline bool safe_body(std::string_view source,const CppMemberBodySpan& body) {
    if(body.begin>body.end||body.end>source.size())return false;
    const auto code=source.substr(body.begin,body.end-body.begin);
    if(code_format::physical_splice(code))return false;
    const auto scanned=code_format::cpp_tokens(code);
    if(!scanned||code_format::cpp_position_sensitive_tokens(*scanned))return false;
    for(const auto& token:*scanned)
        if(token.starts_with("directive:")||token=="op:#"||token=="op:##"||
           token=="op:%:"||token=="op:%:%:")return false;
    return true;
}
inline void substitute(std::string& text,std::string_view key,std::string_view value) {
    for(std::size_t at=0;(at=text.find(key,at))!=text.npos;) {
        text.replace(at,key.size(),value);
        at+=value.size();
    }
}
} // namespace cpp_array_return_take_detail

inline bool cpp_array_return_take_matches(const CppArrayReturnTakePolicy& p,
                                          std::string_view candidate) {
    namespace d=cpp_array_return_take_detail;
    if(!d::valid(p)||candidate.empty()||candidate.size()>4*1024*1024||
       candidate.find('\0')!=candidate.npos||
       code_format::physical_splice(candidate))return false;
    const auto copy=d::free_body_span(p.original_source,d::copy_signature(p));
    const auto take=d::free_body_span(p.original_source,d::take_signature(p));
    const auto revised_copy=d::free_body_span(candidate,d::copy_signature(p));
    const auto revised_take=d::free_body_span(candidate,d::take_signature(p));
    if(!copy||!take||!revised_copy||!revised_take||
       copy->end>=take->begin||revised_copy->end>=revised_take->begin||
       !d::safe_body(candidate,*revised_copy)||
       !d::safe_body(candidate,*revised_take))return false;
    // The full class, original constructors/operations, all constants,
    // signatures, main and tests are preserved byte-for-byte. No parser
    // permits hidden replacement statements outside the two function bodies.
    const auto& original=p.original_source;
    return std::string_view(original).substr(0,copy->begin)==
            candidate.substr(0,revised_copy->begin)&&
        std::string_view(original).substr(
            copy->end,take->begin-copy->end)==
            candidate.substr(revised_copy->end,
                             revised_take->begin-revised_copy->end)&&
        std::string_view(original).substr(take->end)==
            candidate.substr(revised_take->end);
}

inline std::optional<std::string> cpp_array_return_take_private_driver(
        const CppArrayReturnTakePolicy& p,std::string_view candidate) {
    if(!cpp_array_return_take_matches(p,candidate))return {};
    static constexpr std::string_view prelude=R"lgaed(#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
namespace lgaed_return_take_qa {
struct Watch {
    inline static bool active=false,fail_next_array=false;
    inline static unsigned allocated=0,freed=0;
    inline static const void* last_array=nullptr;
    inline static const void* expected_removed=nullptr;
    inline static bool removed=false;
    static void expect_removal(const void* value) noexcept {
        expected_removed=value;removed=false;
    }
    static void on_delete(const void* value) noexcept {
        if(!active||!value)return;
        ++freed;
        if(value==expected_removed) {
            expected_removed=nullptr;removed=true;
        }
    }
};
}
void* operator new(std::size_t count) {
    if(void* ptr=std::malloc(count?count:1))return ptr;
    throw std::bad_alloc{};
}
void operator delete(void* ptr) noexcept {std::free(ptr);}
void operator delete(void* ptr,std::size_t) noexcept {std::free(ptr);}
void* operator new[](std::size_t count) {
    using lgaed_return_take_qa::Watch;
    if(Watch::fail_next_array) {
        Watch::fail_next_array=false;throw std::bad_alloc{};
    }
    if(void* ptr=std::malloc(count?count:1)) {
        if(Watch::active) {
            ++Watch::allocated;
            Watch::last_array=ptr;
        }
        return ptr;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* ptr) noexcept {
    lgaed_return_take_qa::Watch::on_delete(ptr);
    std::free(ptr);
}
void operator delete[](void* ptr,std::size_t) noexcept {
    ::operator delete[](ptr);
}
#define main lgaed_published_original_main
)lgaed";
    static constexpr std::string_view driver=R"lgaed(
#undef main
int main() {
    using lgaed_return_take_qa::Watch;
    const auto equal=[](const @CLASS@& source,std::array<int,4> expected,
                        int count)->bool {
        if(source.@QUANTITY@()!=count ||
           (source.@ACCESS@()!=nullptr)!=(count!=0))return false;
        for(int i=0;i<count;++i) {
            int value=-100;
            if(!source.@READ@(i,value)||value!=expected[static_cast<size_t>(i)])
                return false;
        }
        return true;
    };
    Watch::active=true;
    {
        @CLASS@ source,target;
        if(!source.@CONFIGURE@(3)||!target.@CONFIGURE@(1)||
           !source.@WRITE@(0,1)||!source.@WRITE@(1,0)||
           !source.@WRITE@(2,4)||!target.@WRITE@(0,8))
            return 1;
        const int* source_pointer=source.@ACCESS@();
        const auto initial_allocs=Watch::allocated;
        @CLASS@ changed=@COPY@(source,1,7);
        if(!equal(source,{1,0,4,0},3)||source.@ACCESS@()!=source_pointer||
           !equal(changed,{1,7,4,0},3)||
           changed.@ACCESS@()==source_pointer||
           Watch::allocated<=initial_allocs)return 2;

        // Equal old/new value still requires an independent copied array.
        const auto prior_equal=Watch::allocated;
        @CLASS@ unchanged=@COPY@(source,1,0);
        if(!equal(unchanged,{1,0,4,0},3)||
           unchanged.@ACCESS@()==source_pointer||
           Watch::allocated<=prior_equal||
           source.@ACCESS@()!=source_pointer)return 3;

        // Returning a new owner, then assigning it to its own source, must
        // work: the RHS finishes while the original source still exists.
        const int* same_before=source.@ACCESS@();
        Watch::expect_removal(same_before);
        const auto prior_self=Watch::allocated;
        source=@COPY@(source,2,9);
        if(!equal(source,{1,0,9,0},3)||source.@ACCESS@()==same_before||
           Watch::allocated<=prior_self||!Watch::removed)
            return 4;

        // Source and destination survive a deliberately failed allocation.
        const int* before_failure_source=source.@ACCESS@();
        const int* before_failure_target=target.@ACCESS@();
        const auto allocated_before_failure=Watch::allocated;
        const auto freed_before_failure=Watch::freed;
        Watch::fail_next_array=true;
        bool bad_alloc_seen=false;
        try {target=@COPY@(source,0,6);}
        catch(const std::bad_alloc&) {bad_alloc_seen=true;}
        Watch::fail_next_array=false;
        if(!bad_alloc_seen||source.@ACCESS@()!=before_failure_source||
           target.@ACCESS@()!=before_failure_target||
           !equal(source,{1,0,9,0},3)||!equal(target,{8,0,0,0},1)||
           Watch::allocated!=allocated_before_failure||
           Watch::freed!=freed_before_failure)return 5;

        // The failure guarantee applies when RHS and destination are aliases.
        const int* self_before_failure=source.@ACCESS@();
        const auto old_created=Watch::allocated,old_deleted=Watch::freed;
        Watch::fail_next_array=true;
        bool self_bad_alloc=false;
        try {source=@COPY@(source,0,6);}
        catch(const std::bad_alloc&) {self_bad_alloc=true;}
        Watch::fail_next_array=false;
        if(!self_bad_alloc||source.@ACCESS@()!=self_before_failure||
           !equal(source,{1,0,9,0},3)||
           Watch::allocated!=old_created||Watch::freed!=old_deleted)
            return 6;

        // Insufficient and outside-domain limits never consume the offered
        // rvalue reference, and may not change the occupied target either.
        const int* before_reject_source=source.@ACCESS@();
        const int* before_reject_target=target.@ACCESS@();
        const auto rejected_allocs=Watch::allocated,rejected_frees=Watch::freed;
        for(int invalid:{-1,2,5}) {
            if(@TAKE@(target,std::move(source),invalid)||
               source.@ACCESS@()!=before_reject_source||
               target.@ACCESS@()!=before_reject_target||
               !equal(source,{1,0,9,0},3)||!equal(target,{8,0,0,0},1)||
               Watch::allocated!=rejected_allocs||
               Watch::freed!=rejected_frees)return 7;
        }
        // The same index/count may also be rejected when source==destination.
        for(int invalid:{-1,2,5}) {
            if(@TAKE@(source,std::move(source),invalid)||
               source.@ACCESS@()!=before_reject_source||
               !equal(source,{1,0,9,0},3)||
               Watch::allocated!=rejected_allocs||
               Watch::freed!=rejected_frees)return 8;
        }

        // The accepted operation transfers the same live array, releases
        // the displaced destination and requests no new allocation.
        Watch::expect_removal(target.@ACCESS@());
        const auto before_take_allocs=Watch::allocated,before_take_frees=Watch::freed;
        const int* transferred=source.@ACCESS@();
        if(!@TAKE@(target,std::move(source),3)||
           target.@ACCESS@()!=transferred||
           !equal(target,{1,0,9,0},3)||!equal(source,{0,0,0,0},0)||
           Watch::allocated!=before_take_allocs||
           Watch::freed!=before_take_frees+1||!Watch::removed)
            return 9;

        // Taking an empty owner clears an occupied target; no allocation.
        Watch::expect_removal(target.@ACCESS@());
        const auto empty_allocs=Watch::allocated,empty_frees=Watch::freed;
        if(!@TAKE@(target,std::move(source),0)||
           !equal(target,{0,0,0,0},0)||!equal(source,{0,0,0,0},0)||
           Watch::allocated!=empty_allocs||
           Watch::freed!=empty_frees+1||!Watch::removed)return 10;

        // Self transfer, occupied and empty, preserves pointer/count/values.
        if(!target.@CONFIGURE@(2)||!target.@WRITE@(0,0)||
           !target.@WRITE@(1,7))return 11;
        const int* owner=target.@ACCESS@();
        const auto self_allocs=Watch::allocated,self_frees=Watch::freed;
        if(!@TAKE@(target,std::move(target),2)||
           target.@ACCESS@()!=owner||!equal(target,{0,7,0,0},2)||
           Watch::allocated!=self_allocs||Watch::freed!=self_frees)return 12;
        if(!target.@CLEAR@())return 13;
        const auto empty_self_allocs=Watch::allocated;
        const auto empty_self_frees=Watch::freed;
        if(!@TAKE@(target,std::move(target),0)||
           !equal(target,{0,0,0,0},0)||
           Watch::allocated!=empty_self_allocs||
           Watch::freed!=empty_self_frees)return 14;

        // Equal values under different owners must still transfer identity.
        @CLASS@ equal_source,equal_target;
        if(!equal_source.@CONFIGURE@(2)||!equal_target.@CONFIGURE@(2)||
           !equal_source.@WRITE@(0,0)||!equal_source.@WRITE@(1,3)||
           !equal_target.@WRITE@(0,0)||!equal_target.@WRITE@(1,3))
            return 15;
        const int* equal_pointer=equal_source.@ACCESS@();
        Watch::expect_removal(equal_target.@ACCESS@());
        const auto equal_allocs=Watch::allocated,equal_frees=Watch::freed;
        if(!@TAKE@(equal_target,std::move(equal_source),4)||
           equal_target.@ACCESS@()!=equal_pointer||
           !equal(equal_target,{0,3,0,0},2)||
           !equal(equal_source,{0,0,0,0},0)||
           Watch::allocated!=equal_allocs||
           Watch::freed!=equal_frees+1||!Watch::removed)return 16;

        // Reuse a previously moved-from object and retain normal cleanup.
        if(!equal_source.@CONFIGURE@(1)||
           !equal_source.@WRITE@(0,6)||
           !equal(equal_source,{6,0,0,0},1)||
           equal_source.@ACCESS@()==equal_target.@ACCESS@())return 17;

        // Exiting after a caller exception cleans every live array only once.
        const auto prior_throw_new=Watch::allocated,prior_throw_del=Watch::freed;
        bool caught=false;
        try {
            @CLASS@ a,b;
            if(!a.@CONFIGURE@(2)||!b.@CONFIGURE@(1))return 18;
            if(!@TAKE@(b,std::move(a),2)||
               !equal(a,{0,0,0,0},0)||!equal(b,{0,0,0,0},2))
                return 19;
            throw 19;
        } catch(int value) {caught=value==19;}
        if(!caught||Watch::allocated!=prior_throw_new+2||
           Watch::freed!=prior_throw_del+2)return 20;
    }
    Watch::active=false;
    if(Watch::allocated!=Watch::freed||Watch::fail_next_array)return 21;
    std::fputs("LGAED_ARRAY_RETURN_TAKE_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    auto witness=std::string(driver);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,9>{{
        {"@CLASS@",p.class_name},{"@COPY@",p.copy_function},
        {"@TAKE@",p.take_function},{"@QUANTITY@",p.quantity_method},
        {"@ACCESS@",p.access_method},{"@CONFIGURE@",p.configure_method},
        {"@READ@",p.read_method},{"@WRITE@",p.write_method},
        {"@CLEAR@",p.clear_method}}})
        cpp_array_return_take_detail::substitute(witness,key,value);
    // Never apply substitutions to student code, its literals or comments.
    return std::string(prelude)+std::string(candidate)+witness;
}

} // namespace lgaed::curriculum
