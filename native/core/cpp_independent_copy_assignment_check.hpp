#pragma once

#include "core/code_format_compat.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Original v2 compiler source owns the trusted class, declarations and main.
// The student may change only the body of its existing copy-assignment operator.
// All identifiers below are author-declared, never guessed from curriculum prose.
struct CppIndependentCopyAssignmentPolicy {
    std::string original_source, class_name, member_name, parameter_name;
    std::string has_method, value_method, access_method;
    std::string put_method, change_method, clear_method;
    bool operator==(const CppIndependentCopyAssignmentPolicy&) const = default;
};

inline constexpr std::string_view cpp_independent_copy_completion_marker =
    "LGAED_COPY_ASSIGNMENT_INDEPENDENT_V1";

namespace cpp_independent_copy_detail {
using Tokens = std::vector<std::string>;

inline bool identifier(std::string_view value) {
    if(value.empty() || value.size()>96 || value.starts_with("__"))return false;
    const auto first=[](unsigned char c) {
        return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';
    };
    if(!first(static_cast<unsigned char>(value.front())))return false;
    return std::ranges::all_of(value.substr(1),[&](unsigned char c) {
        return first(c)||(c>='0'&&c<='9');
    });
}

inline std::optional<Tokens> lex(std::string_view code) {
    if(code.empty() || code.size()>4*1024*1024 ||
       code.find('\0')!=code.npos || code_format::physical_splice(code))
        return {};
    const auto scanned=code_format::cpp_tokens(code);
    if(!scanned || code_format::cpp_position_sensitive_tokens(*scanned))return {};
    Tokens result;
    result.reserve(scanned->size());
    for(const auto& token:*scanned)
        if(!token.starts_with("comment:"))result.push_back(token);
    return result;
}

struct MethodRegion {
    std::size_t opening=0,closing=0;
};

inline std::optional<MethodRegion> method(const Tokens& tokens,
        const CppIndependentCopyAssignmentPolicy& spec) {
    const std::array<std::string,13> signature{{
        "id:"+spec.class_name,"op:&","id:"+spec.class_name,"op:::",
        "id:operator","op:=","op:(","id:const","id:"+spec.class_name,
        "op:&","id:"+spec.parameter_name,"op:)","op:{"}};
    std::optional<MethodRegion> found;
    for(std::size_t i=0;i+signature.size()<=tokens.size();++i) {
        if(!std::equal(signature.begin(),signature.end(),
            tokens.begin()+static_cast<std::ptrdiff_t>(i)))continue;
        if(found)return {};
        const auto opening=i+signature.size()-1;
        std::size_t depth=1;
        for(auto cursor=opening+1;cursor<tokens.size();++cursor) {
            if(tokens[cursor]=="op:{")++depth;
            if(tokens[cursor]=="op:}"&&--depth==0) {
                found=MethodRegion{opening,cursor};
                break;
            }
        }
        if(!found)return {};
    }
    return found;
}

inline Tokens without_body(const Tokens& source,const MethodRegion& region) {
    Tokens result;
    result.reserve(source.size());
    for(std::size_t i=0;i<source.size();++i)
        if(i<=region.opening || i>=region.closing)
            result.push_back(source[i]);
    return result;
}
inline bool directives_in_body(const Tokens& source,
        const MethodRegion& region) {
    for(auto i=region.opening+1;i<region.closing;++i)
        if(source[i].starts_with("directive:")||
           source[i]=="op:#"||source[i]=="op:##")return true;
    return false;
}
// The independent program reports completion only after its own lifetime and
// exception-safety checks. A submitted operator= cannot substitute that
// verdict by terminating the process or writing to the driver's output.
// Check actual C++ tokens exclusively inside the authorized method body:
// comments, ordinary literals and raw strings remain harmless data.
inline bool private_probe_neutral(const Tokens& tokens,
                                  const MethodRegion& region) {
    Tokens body;
    body.reserve(region.closing-region.opening-1);
    for(auto i=region.opening+1;i<region.closing;++i)
        body.push_back(tokens[i]);
    if(code_format::cpp_process_termination_tokens(body))return false;
    constexpr std::string_view forbidden[]={
        "lgaed_copy_qa","Watch","fail_next",
        "__FILE__","__FILEW__","__BASE_FILE__",
        "__COUNTER__","__INCLUDE_LEVEL__","__DATE__","__TIME__",
        "__TIMESTAMP__","__PRETTY_FUNCTION__","__FUNCSIG__",
        "__FUNCDNAME__","__builtin_FILE","__builtin_FUNCTION",
        "__builtin_source_location","source_location",
        "longjmp","_longjmp","siglongjmp","setjmp","sigsetjmp",
        "asm","__asm","__asm__","__pragma","_Pragma",
        "fputs","puts","printf","fprintf","fwrite","putchar",
        "freopen","freopen_s","fclose","stdout","stderr",
        "cout","cerr","clog","wcout","wcerr","wclog",
        "dup2","_dup2","SetStdHandle","WriteFile","WriteConsoleA",
        "WriteConsoleW","CreateProcessA","CreateProcessW","WinExec",
        "system","popen","_popen"
    };
    for(const auto& token:body) {
        if(token.starts_with("directive:")||token=="op:#"||
           token=="op:##"||token=="op:%:"||token=="op:%:%:")
            return false;
        if(!token.starts_with("id:"))continue;
        const auto name=std::string_view(token).substr(3);
        if(std::ranges::find(forbidden,name)!=std::end(forbidden))
            return false;
    }
    return true;
}
inline bool member_present(const Tokens& source,
        const CppIndependentCopyAssignmentPolicy& spec) {
    const std::array<std::string,7> pattern{{
        "id:std","op:::","id:unique_ptr","op:<",
        "id:int","op:>","id:"+spec.member_name}};
    for(std::size_t i=0;i+pattern.size()<=source.size();++i)
        if(std::equal(pattern.begin(),pattern.end(),
               source.begin()+static_cast<std::ptrdiff_t>(i)))
            return true;
    return false;
}
inline void substitute(std::string& source,std::string_view placeholder,
                       std::string_view value) {
    for(std::size_t at=0;(at=source.find(placeholder,at))!=source.npos;) {
        source.replace(at,placeholder.size(),value);
        at+=value.size();
    }
}
} // namespace cpp_independent_copy_detail

inline bool cpp_independent_copy_matches(
        const CppIndependentCopyAssignmentPolicy& spec,
        std::string_view candidate) {
    namespace d=cpp_independent_copy_detail;
    for(std::string_view value:{std::string_view(spec.class_name),
            std::string_view(spec.member_name),
            std::string_view(spec.parameter_name),
            std::string_view(spec.has_method),
            std::string_view(spec.value_method),
            std::string_view(spec.access_method),
            std::string_view(spec.put_method),
            std::string_view(spec.change_method),
            std::string_view(spec.clear_method)})
        if(!d::identifier(value))return false;
    const auto original=d::lex(spec.original_source);
    const auto revised=d::lex(candidate);
    if(!original||!revised||!d::member_present(*original,spec))return false;
    const auto before=d::method(*original,spec);
    const auto after=d::method(*revised,spec);
    return before&&after&&!d::directives_in_body(*revised,*after)&&
        d::private_probe_neutral(*revised,*after)&&
        d::without_body(*original,*before)==d::without_body(*revised,*after);
}

// The separate driver runs AFTER the student program compiled and ran with
// its original public cases. This has no access to historical stdout receipts.
// It exercises identity, strong exception safety and cleanup with real objects.
inline std::optional<std::string> cpp_independent_copy_probe_source(
        const CppIndependentCopyAssignmentPolicy& spec,
        std::string_view candidate) {
    if(!cpp_independent_copy_matches(spec,candidate))return {};
    static constexpr std::string_view prelude=R"copy(#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
namespace lgaed_copy_qa {
struct Watch {
    inline static bool active=false,fail_next=false;
    inline static unsigned created=0,deleted=0;
    inline static const void* expected_deleted=nullptr;
    inline static bool saw_expected_deleted=false;
    static void expect_deletion(const void* address) {
        expected_deleted=address;saw_expected_deleted=false;
    }
    static void deleting(const void* address) noexcept {
        if(!active||!address)return;
        ++deleted;
        if(address==expected_deleted) {
            saw_expected_deleted=true;expected_deleted=nullptr;
        }
    }
};
}
void* operator new(std::size_t bytes) {
    if(lgaed_copy_qa::Watch::fail_next) {
        lgaed_copy_qa::Watch::fail_next=false;
        throw std::bad_alloc{};
    }
    if(void* address=std::malloc(bytes?bytes:1)) {
        if(lgaed_copy_qa::Watch::active)++lgaed_copy_qa::Watch::created;
        return address;
    }
    throw std::bad_alloc{};
}
void operator delete(void* address) noexcept {
    lgaed_copy_qa::Watch::deleting(address);std::free(address);
}
void operator delete(void* address,std::size_t) noexcept {
    ::operator delete(address);
}
void* operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete[](void* address) noexcept {::operator delete(address);}
void operator delete[](void* address,std::size_t) noexcept {
    ::operator delete(address);
}
#define main lgaed_original_student_main
)copy";
    static constexpr std::string_view driver=R"copy(
#undef main
int main() {
    using lgaed_copy_qa::Watch;
    Watch::active=true;
    {
        @CLASS@ source,destination;
        if(!source.@CHANGE@(8)||!destination.@CHANGE@(2)||
           Watch::created!=2)return 1;
        const int* source_address=source.@ACCESS@();
        Watch::expect_deletion(destination.@ACCESS@());
        const auto made=Watch::created,freed=Watch::deleted;
        @CLASS@& returned=(destination=source);
        if(&returned!=&destination||source.@ACCESS@()!=source_address||
           destination.@ACCESS@()==source_address||
           !destination.@HAS@()||destination.@VALUE@()!=8||
           source.@VALUE@()!=8||Watch::created!=made+1||
           Watch::deleted!=freed+1||!Watch::saw_expected_deleted)return 2;

        Watch::expect_deletion(destination.@ACCESS@());
        const auto made_equal=Watch::created,freed_equal=Watch::deleted;
        destination=source;
        if(source.@ACCESS@()!=source_address||
           destination.@ACCESS@()==source_address||
           Watch::created!=made_equal+1||Watch::deleted!=freed_equal+1||
           !Watch::saw_expected_deleted)return 3;

        if(!source.@CLEAR@()||source.@HAS@())return 4;
        Watch::expect_deletion(destination.@ACCESS@());
        const auto made_empty=Watch::created,freed_empty=Watch::deleted;
        @CLASS@& from_empty=(destination=source);
        if(&from_empty!=&destination||destination.@HAS@()||source.@HAS@()||
           Watch::created!=made_empty||Watch::deleted!=freed_empty+1||
           !Watch::saw_expected_deleted)return 5;

        @CLASS@ self;
        if(!self.@PUT@(0))return 6;
        const int* self_address=self.@ACCESS@();
        @CLASS@& alias=self;
        const auto made_self=Watch::created,freed_self=Watch::deleted;
        @CLASS@& self_return=(self=alias);
        if(&self_return!=&self||self.@ACCESS@()!=self_address||
           !self.@HAS@()||self.@VALUE@()!=0||
           Watch::created!=made_self||Watch::deleted!=freed_self)return 7;
        self.@CLEAR@();
        const auto made_none=Watch::created,freed_none=Watch::deleted;
        @CLASS@& empty_alias=self;
        @CLASS@& empty_return=(self=empty_alias);
        if(&empty_return!=&self||self.@HAS@()||
           Watch::created!=made_none||Watch::deleted!=freed_none)return 8;

        @CLASS@ occupied;
        if(!source.@PUT@(9)||!occupied.@CHANGE@(3))return 9;
        const int* source_before=source.@ACCESS@();
        const int* destination_before=occupied.@ACCESS@();
        const auto made_failure=Watch::created,freed_failure=Watch::deleted;
        Watch::fail_next=true;
        bool caught=false;
        try {occupied=source;}catch(const std::bad_alloc&){caught=true;}
        Watch::fail_next=false;
        if(!caught||source.@ACCESS@()!=source_before||
           occupied.@ACCESS@()!=destination_before||
           source.@VALUE@()!=9||occupied.@VALUE@()!=3||
           Watch::created!=made_failure||Watch::deleted!=freed_failure)
            return 10;

        @CLASS@ moved(std::move(source));
        if(source.@HAS@()||moved.@ACCESS@()!=source_before)return 11;
        const auto made_reuse=Watch::created;
        @CLASS@& reused=(source=moved);
        if(&reused!=&source||!source.@HAS@()||
           source.@VALUE@()!=moved.@VALUE@()||
           source.@ACCESS@()==moved.@ACCESS@()||
           Watch::created!=made_reuse+1)return 12;
    }
    Watch::active=false;
    if(Watch::created!=Watch::deleted||Watch::fail_next)return 13;
    std::fputs("LGAED_COPY_ASSIGNMENT_INDEPENDENT_V1",stderr);
    return 0;
}
)copy";
    // Replace placeholders exclusively in the trusted test harness. Never
    // rewrite student literals or comments that happen to contain @CLASS@.
    auto private_driver=std::string(driver);
    namespace d=cpp_independent_copy_detail;
    for(const auto& [key,value]:std::array<std::pair<std::string_view,std::string_view>,7>{{
            {"@CLASS@",spec.class_name},{"@HAS@",spec.has_method},
            {"@VALUE@",spec.value_method},{"@ACCESS@",spec.access_method},
            {"@PUT@",spec.put_method},{"@CHANGE@",spec.change_method},
            {"@CLEAR@",spec.clear_method}}})
        d::substitute(private_driver,key,value);
    return std::string(prelude)+std::string(candidate)+private_driver;
}
} // namespace lgaed::curriculum
