#pragma once

#include "core/code_format_compat.hpp"
#include "core/cpp_member_body_check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// A generic, opt-in, original-owner single-method policy. The *only* editable
// part is the existing move-assignment body. A private compiled driver verifies
// semantic ownership, source invariants and cleanup, not the student's stdout.
struct CppMoveAssignmentInvariantPolicy {
    std::string original_source, class_name, member_name, label_member;
    std::string parameter_name, has_method, value_method, label_method;
    std::string access_method, invariant_method, put_method, change_method;
    std::string retag_method, clear_method;
    bool operator==(const CppMoveAssignmentInvariantPolicy&) const = default;
};

inline constexpr std::string_view cpp_move_invariant_completion_marker =
    "LGAED_MOVE_INVARIANT_ASSIGNMENT_COMPLETE_V1";

namespace cpp_move_invariant_detail {
inline bool identifier(std::string_view s) {
    if(s.empty()||s.size()>96||s.starts_with("__")||
       (s.size()>1&&s[0]=='_'&&s[1]>='A'&&s[1]<='Z'))return false;
    const auto alpha=[](unsigned char c) {
        return c=='_'||(c>='A'&&c<='Z')||(c>='a'&&c<='z');
    };
    if(!alpha(static_cast<unsigned char>(s.front())))return false;
    return std::ranges::all_of(s.substr(1),[&](unsigned char c){
        return alpha(c)||(c>='0'&&c<='9');
    });
}
inline std::string signature(const CppMoveAssignmentInvariantPolicy& p) {
    return p.class_name+"& "+p.class_name+"::operator=("+
        p.class_name+"&& "+p.parameter_name+")";
}
inline bool original_members(const CppMoveAssignmentInvariantPolicy& p) {
    const auto scanned=code_format::cpp_tokens(p.original_source);
    if(!scanned)return false;
    const auto& tokens=*scanned;
    std::optional<std::size_t> opening;
    for(std::size_t i=0;i+2<tokens.size();++i)
        if((tokens[i]=="id:class"||tokens[i]=="id:struct")&&
           tokens[i+1]=="id:"+p.class_name&&tokens[i+2]=="op:{") {
            if(opening)return false;
            opening=i+2;
        }
    if(!opening)return false;
    std::size_t closing=0,depth=1;
    for(std::size_t i=*opening+1;i<tokens.size();++i) {
        if(tokens[i]=="op:{")++depth;
        else if(tokens[i]=="op:}"&&--depth==0){closing=i;break;}
    }
    if(!closing)return false;
    const std::array<std::string,7> owner{{
        "id:std","op:::","id:unique_ptr","op:<","id:int",
        "op:>","id:"+p.member_name}};
    const std::array<std::string,2> label{{"id:int","id:"+p.label_member}};
    bool has_owner=false,has_label=false;
    for(std::size_t i=*opening+1;i<closing;++i) {
        if(i+owner.size()<=closing&&
           std::equal(owner.begin(),owner.end(),tokens.begin()+
               static_cast<std::ptrdiff_t>(i)))has_owner=true;
        if(i+label.size()<=closing&&
           std::equal(label.begin(),label.end(),tokens.begin()+
               static_cast<std::ptrdiff_t>(i)))has_label=true;
    }
    return has_owner&&has_label;
}
inline bool spec_valid(const CppMoveAssignmentInvariantPolicy& p) {
    for(const auto value:{std::string_view(p.class_name),
            std::string_view(p.member_name),std::string_view(p.label_member),
            std::string_view(p.parameter_name),std::string_view(p.has_method),
            std::string_view(p.value_method),std::string_view(p.label_method),
            std::string_view(p.access_method),std::string_view(p.invariant_method),
            std::string_view(p.put_method),std::string_view(p.change_method),
            std::string_view(p.retag_method),std::string_view(p.clear_method)})
        if(!identifier(value))return false;
    return original_members(p)&&cpp_member_body_span(
        CppMemberBodyCheck{p.original_source,signature(p)}).has_value();
}
inline void substitute(std::string& text,std::string_view key,std::string_view value) {
    for(std::size_t at=0;(at=text.find(key,at))!=text.npos;) {
        text.replace(at,key.size(),value);
        at+=value.size();
    }
}
} // namespace cpp_move_invariant_detail

// Private execution is meaningful only if the student's method cannot
// terminate the driver, forge its completion marker or edit the private
// allocation counters. Inspect the authorized body, never the immutable
// main/class declarations. Comment and literal tokens are not executable.
inline bool cpp_move_invariant_probe_neutral(
        const CppMemberBodyCheck& owner,std::string_view candidate) {
    const auto span=cpp_member_body_span(owner);
    if(!span || !cpp_member_body_preserves_source(owner,candidate))return false;
    const auto preserved_suffix=owner.original_source.size()-span->end;
    const auto body=candidate.substr(
        span->begin,candidate.size()-preserved_suffix-span->begin);
    const auto parsed=code_format::cpp_tokens(body);
    if(!parsed || code_format::cpp_position_sensitive_tokens(*parsed) ||
       code_format::cpp_process_termination_tokens(*parsed))return false;
    constexpr std::string_view forbidden[]{
        "lgaed_move_invariant_qa","Watch","fail_next",
        "created","deleted","deleted_replaced",
        "__FILE__","__FILEW__","__BASE_FILE__",
        "__COUNTER__","__INCLUDE_LEVEL__","__DATE__","__TIME__",
        "__TIMESTAMP__","__PRETTY_FUNCTION__","__FUNCSIG__",
        "__FUNCDNAME__","__builtin_FILE","__builtin_FUNCTION",
        "__builtin_source_location","source_location",
        "longjmp","_longjmp","siglongjmp","setjmp","sigsetjmp",
        "asm","__asm","__asm__","__pragma","_Pragma",
        "malloc","calloc","realloc","free","_aligned_malloc","_aligned_free",
        "fputs","puts","printf","fprintf","fwrite","putchar",
        "freopen","freopen_s","fclose","stdout","stderr",
        "cout","cerr","clog","wcout","wcerr","wclog",
        "dup2","_dup2","SetStdHandle","WriteFile","WriteConsoleA",
        "WriteConsoleW","CreateProcessA","CreateProcessW","WinExec",
        "system","popen","_popen"
    };
    for(const auto& token:*parsed) {
        if(token.starts_with("directive:")||token=="op:#"||token=="op:##"||
           token=="op:%:"||token=="op:%:%:")return false;
        if(!token.starts_with("id:"))continue;
        const auto name=std::string_view(token).substr(3);
        if(std::ranges::find(forbidden,name)!=std::end(forbidden))
            return false;
    }
    return true;
}

inline bool cpp_move_invariant_matches(
        const CppMoveAssignmentInvariantPolicy& p,std::string_view candidate) {
    if(!cpp_move_invariant_detail::spec_valid(p))return false;
    const CppMemberBodyCheck owner{
        p.original_source,cpp_move_invariant_detail::signature(p)};
    return cpp_move_invariant_probe_neutral(owner,candidate);
}

inline std::optional<std::string> cpp_move_invariant_private_driver(
        const CppMoveAssignmentInvariantPolicy& p,std::string_view candidate) {
    if(!cpp_move_invariant_matches(p,candidate))return {};
    static constexpr std::string_view prelude=R"lgaed(#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
namespace lgaed_move_invariant_qa {
struct Watch {
    inline static bool active=false,fail_next=false;
    inline static unsigned created=0,deleted=0;
    inline static const void* replaced=nullptr;
    inline static bool deleted_replaced=false;
    static void expect_delete(const void* pointer) {
        replaced=pointer;deleted_replaced=false;
    }
    static void note_delete(const void* pointer) noexcept {
        if(!active||!pointer)return;
        ++deleted;
        if(pointer==replaced) {
            deleted_replaced=true;replaced=nullptr;
        }
    }
};
}
void* operator new(std::size_t bytes) {
    using lgaed_move_invariant_qa::Watch;
    if(Watch::fail_next) {Watch::fail_next=false;throw std::bad_alloc{};}
    if(void* pointer=std::malloc(bytes?bytes:1)) {
        if(Watch::active)++Watch::created;
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept {
    lgaed_move_invariant_qa::Watch::note_delete(pointer);
    std::free(pointer);
}
void operator delete(void* pointer,std::size_t) noexcept {
    ::operator delete(pointer);
}
void* operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete[](void* pointer) noexcept {::operator delete(pointer);}
void operator delete[](void* pointer,std::size_t) noexcept {::operator delete(pointer);}
#define main lgaed_original_muestra_main
)lgaed";
    static constexpr std::string_view driver=R"lgaed(
#undef main
int main() {
    using lgaed_move_invariant_qa::Watch;
    Watch::active=true;
    {
        @CLASS@ source,target;
        if(!source.@PUT@(4,3)||!target.@PUT@(7,8)||
           !source.@INVARIANT@()||!target.@INVARIANT@()||
           Watch::created!=2)return 1;
        const int* owned=source.@ACCESS@();
        Watch::expect_delete(target.@ACCESS@());
        const auto created=Watch::created,deleted=Watch::deleted;
        @CLASS@& returned=(target=std::move(source));
        if(&returned!=&target||source.@HAS@()||source.@LABEL@()!=0||
           !source.@INVARIANT@()||!target.@INVARIANT@()||
           target.@ACCESS@()!=owned||target.@LABEL@()!=3||target.@VALUE@()!=4||
           Watch::created!=created||Watch::deleted!=deleted+1||
           !Watch::deleted_replaced)return 2;

        // Same int values do not make independently owned source/dest the same.
        @CLASS@ same_a,same_b;
        if(!same_a.@PUT@(5,6)||!same_b.@PUT@(5,9))return 3;
        const int* same_owner=same_a.@ACCESS@();
        Watch::expect_delete(same_b.@ACCESS@());
        const auto same_created=Watch::created,same_deleted=Watch::deleted;
        same_b=std::move(same_a);
        if(same_b.@ACCESS@()!=same_owner||same_b.@VALUE@()!=5||
           same_b.@LABEL@()!=6||!same_b.@INVARIANT@()||
           same_a.@HAS@()||same_a.@LABEL@()!=0||!same_a.@INVARIANT@()||
           Watch::created!=same_created||Watch::deleted!=same_deleted+1||
           !Watch::deleted_replaced)return 4;

        // Empty to occupied ends the prior object, with both empty afterward.
        @CLASS@ empty,nonempty;
        if(!nonempty.@PUT@(8,2))return 5;
        Watch::expect_delete(nonempty.@ACCESS@());
        const auto empty_created=Watch::created,empty_deleted=Watch::deleted;
        @CLASS@& empty_return=(nonempty=std::move(empty));
        if(&empty_return!=&nonempty||nonempty.@HAS@()||empty.@HAS@()||
           nonempty.@LABEL@()!=0||empty.@LABEL@()!=0||
           !nonempty.@INVARIANT@()||!empty.@INVARIANT@()||
           Watch::created!=empty_created||Watch::deleted!=empty_deleted+1||
           !Watch::deleted_replaced)return 6;

        // Self-move, including zero as an occupied value, preserves identity.
        @CLASS@ self;
        if(!self.@PUT@(0,9))return 7;
        const int* self_owner=self.@ACCESS@();
        @CLASS@& alias=self;
        const auto self_created=Watch::created,self_deleted=Watch::deleted;
        @CLASS@& self_return=(self=std::move(alias));
        if(&self_return!=&self||self.@ACCESS@()!=self_owner||self.@VALUE@()!=0||
           self.@LABEL@()!=9||!self.@INVARIANT@()||
           Watch::created!=self_created||Watch::deleted!=self_deleted)return 8;
        self.@CLEAR@();
        @CLASS@& empty_alias=self;
        const auto empty_self_created=Watch::created;
        const auto empty_self_deleted=Watch::deleted;
        @CLASS@& empty_self_return=(self=std::move(empty_alias));
        if(&empty_self_return!=&self||self.@HAS@()||self.@LABEL@()!=0||
           !self.@INVARIANT@()||Watch::created!=empty_self_created||
           Watch::deleted!=empty_self_deleted)return 9;

        // Deterministically deny the next allocation: a genuine move needs none.
        @CLASS@ noalloc_source,noalloc_target;
        if(!noalloc_source.@PUT@(0,5)||!noalloc_target.@PUT@(3,1))
            return 10;
        const int* expected=noalloc_source.@ACCESS@();
        Watch::expect_delete(noalloc_target.@ACCESS@());
        const auto noalloc_created=Watch::created,noalloc_deleted=Watch::deleted;
        Watch::fail_next=true;
        bool threw=false;
        try {noalloc_target=std::move(noalloc_source);}
        catch(const std::bad_alloc&) {threw=true;}
        Watch::fail_next=false;
        if(threw||noalloc_target.@ACCESS@()!=expected||
           noalloc_target.@VALUE@()!=0||noalloc_target.@LABEL@()!=5||
           noalloc_source.@HAS@()||noalloc_source.@LABEL@()!=0||
           !noalloc_target.@INVARIANT@()||!noalloc_source.@INVARIANT@()||
           Watch::created!=noalloc_created||Watch::deleted!=noalloc_deleted+1||
           !Watch::deleted_replaced)return 11;

        // The moved-from object remains reusable and rejected edits preserve it.
        if(!noalloc_source.@PUT@(2,4))return 12;
        const int* reused=noalloc_source.@ACCESS@();
        if(noalloc_source.@PUT@(2,0)||noalloc_source.@CHANGE@(10)||
           noalloc_source.@RETAG@(10)||noalloc_source.@ACCESS@()!=reused||
           noalloc_source.@VALUE@()!=2||noalloc_source.@LABEL@()!=4||
           !noalloc_source.@INVARIANT@())return 13;

        // Normal unwinding must free the source and destination exactly once.
        const auto before_exception_created=Watch::created;
        const auto before_exception_deleted=Watch::deleted;
        bool caught=false;
        try {
            @CLASS@ thrown_source,thrown_target;
            if(!thrown_source.@PUT@(1,2)||!thrown_target.@PUT@(2,3))
                return 14;
            thrown_target=std::move(thrown_source);
            if(thrown_source.@HAS@()||thrown_source.@LABEL@()!=0||
               !thrown_target.@INVARIANT@())return 15;
            throw 21;
        } catch(int number) {caught=number==21;}
        if(!caught||Watch::created!=before_exception_created+2||
           Watch::deleted!=before_exception_deleted+2)return 16;
    }
    Watch::active=false;
    if(Watch::created!=Watch::deleted||Watch::fail_next)return 17;
    std::fputs("LGAED_MOVE_INVARIANT_ASSIGNMENT_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    auto private_driver=std::string(driver);
    for(const auto& [key,value]:std::array<std::pair<std::string_view,std::string_view>,10>{{
        {"@CLASS@",p.class_name},{"@HAS@",p.has_method},
        {"@VALUE@",p.value_method},{"@LABEL@",p.label_method},
        {"@ACCESS@",p.access_method},{"@INVARIANT@",p.invariant_method},
        {"@PUT@",p.put_method},{"@CHANGE@",p.change_method},
        {"@RETAG@",p.retag_method},{"@CLEAR@",p.clear_method}}})
        cpp_move_invariant_detail::substitute(private_driver,key,value);
    // Never transform original student comments or string literals. All
    // substitutions above are applied only to the fixed, private driver.
    return std::string(prelude)+std::string(candidate)+private_driver;
}

} // namespace lgaed::curriculum
