#pragma once

#include "core/code_format_compat.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {

// A general source-preservation prerequisite for defaulted class operations.
// The original is loaded from the authenticated curriculum, never the draft.
// Behavioral proof comes from a separately compiled, privately owned driver.
struct CppDefaultedMovePolicy {
    std::string original_source, class_name, member_name;
    std::string put_method, change_method, clear_method;
    bool operator==(const CppDefaultedMovePolicy&) const = default;
};
enum class CppDefaultedMoveProbe {
    success, rejected_copy_constructor, rejected_copy_assignment
};
inline constexpr std::string_view cpp_defaulted_move_completion_marker =
    "LGAED_DEFAULTED_MOVE_POLICY_COMPLETE_V1";

namespace cpp_defaulted_move_detail {
constexpr std::size_t source_limit = 4 * 1024 * 1024;
inline bool name(std::string_view value) {
    if(value.empty()||value.size()>96||value.starts_with("__"))return false;
    const auto start=[](unsigned char c) {
        return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';
    };
    if(!start(static_cast<unsigned char>(value.front())))return false;
    return std::ranges::all_of(value.substr(1),[&](unsigned char c) {
        return start(c)||(c>='0'&&c<='9');
    });
}
using Tokens=std::vector<std::string>;
inline std::optional<Tokens> tokens(std::string_view source) {
    if(source.empty()||source.size()>source_limit||
       source.find('\0')!=source.npos||
       code_format::physical_splice(source))return {};
    const auto scanned=code_format::cpp_tokens(source);
    if(!scanned||code_format::cpp_position_sensitive_tokens(*scanned))return {};
    Tokens result;
    result.reserve(scanned->size());
    for(const auto& token:*scanned)
        if(!token.starts_with("comment:"))result.push_back(token);
    return result;
}
struct ClassRegion {
    std::size_t opening=0,closing=0,public_start=0,public_end=0;
};
inline std::optional<ClassRegion> region(const Tokens& tokens,
                                          std::string_view class_name) {
    std::optional<ClassRegion> found;
    const auto expected="id:"+std::string(class_name);
    for(std::size_t i=0;i+2<tokens.size();++i) {
        if(tokens[i]!="id:class"&&tokens[i]!="id:struct")continue;
        if(tokens[i+1]!=expected)continue;
        auto opening=i+2;
        if(opening<tokens.size()&&tokens[opening]=="id:final")++opening;
        if(opening>=tokens.size()||tokens[opening]!="op:{"||found)return {};
        ClassRegion candidate;
        candidate.opening=opening;
        candidate.public_start=tokens[i]=="id:struct"?opening+1:tokens.size();
        candidate.public_end=tokens.size();
        std::size_t depth=1;
        for(auto j=opening+1;j<tokens.size();++j) {
            if(depth==1 && j+1<tokens.size() &&
               (tokens[j]=="id:public"||tokens[j]=="id:private"||
                tokens[j]=="id:protected") &&tokens[j+1]=="op::") {
                if(tokens[j]=="id:public") {
                    if(candidate.public_start!=tokens.size()&&
                       candidate.public_end==tokens.size())
                        candidate.public_end=j;
                    candidate.public_start=j+2;
                }else if(candidate.public_start!=tokens.size()&&
                         candidate.public_end==tokens.size())
                    candidate.public_end=j;
            }
            if(tokens[j]=="op:{")++depth;
            else if(tokens[j]=="op:}") {
                if(--depth==0) {
                    candidate.closing=j;
                    break;
                }
            }
        }
        if(!candidate.closing||candidate.closing+1>=tokens.size()||
           tokens[candidate.closing+1]!="op:;"||
           candidate.public_start==tokens.size())return {};
        if(candidate.public_end==tokens.size())
            candidate.public_end=candidate.closing;
        if(candidate.public_start>=candidate.public_end)return {};
        found=candidate;
    }
    return found;
}
struct Declaration {
    int kind=0;  // 1=default constructor, 2=move constructor, 3=move assignment
    std::size_t end=0;
};
inline std::optional<Declaration> operation(const Tokens& code,std::size_t first,
                                              std::string_view class_name) {
    const std::string self="id:"+std::string(class_name);
    std::size_t i=first;
    int kind=0;
    if(i<code.size()&&code[i]==self&&i+1<code.size()&&code[i+1]=="op:(") {
        i+=2;
        if(i<code.size()&&code[i]=="op:)") {
            ++i;
            kind=1;
        }else if(i+1<code.size()&&code[i]==self&&code[i+1]=="op:&&") {
            i+=2;
            if(i<code.size()&&code[i].starts_with("id:")&&code[i]!="id:noexcept")
                ++i; // Optional move-constructor parameter name.
            if(i>=code.size()||code[i++]!="op:)")return {};
            kind=2;
        }else return {};
    }else if(i+5<code.size()&&code[i]==self&&code[i+1]=="op:&"&&
             code[i+2]=="id:operator"&&code[i+3]=="op:="&&
             code[i+4]=="op:("&&code[i+5]==self) {
        i+=6;
        if(i>=code.size()||code[i++]!="op:&&")return {};
        if(i<code.size()&&code[i].starts_with("id:")&&code[i]!="id:noexcept")
            ++i;
        if(i>=code.size()||code[i++]!="op:)")return {};
        kind=3;
    }else return {};
    if(i<code.size()&&code[i]=="id:noexcept")++i;
    if(i+2>=code.size()||code[i]!="op:="||
       code[i+1]!="id:default"||code[i+2]!="op:;")return {};
    return Declaration{kind,i+3};
}
inline void substitute(std::string& source,std::string_view key,
                       std::string_view replacement) {
    for(std::size_t at=0;(at=source.find(key,at))!=source.npos;) {
        source.replace(at,key.size(),replacement);
        at+=replacement.size();
    }
}
} // namespace cpp_defaulted_move_detail

inline bool cpp_defaulted_move_policy_matches(const CppDefaultedMovePolicy& spec,
                                               std::string_view candidate) {
    namespace d=cpp_defaulted_move_detail;
    for(const auto& item:{std::string_view(spec.class_name),
           std::string_view(spec.member_name),
           std::string_view(spec.put_method),std::string_view(spec.change_method),
           std::string_view(spec.clear_method)})
        if(!d::name(item))return false;
    const auto before=d::tokens(spec.original_source);
    const auto after=d::tokens(candidate);
    if(!before||!after||!d::region(*before,spec.class_name))return false;
    const auto region=d::region(*after,spec.class_name);
    if(!region)return false;
    std::array<bool,4> seen{};
    d::Tokens protected_code;
    protected_code.reserve(after->size());
    std::size_t depth=0;
    for(std::size_t at=0;at<after->size();) {
        const auto& token=(*after)[at];
        if(at>=region->public_start&&at<region->public_end&&depth==1) {
            const auto added=d::operation(*after,at,spec.class_name);
            if(added) {
                if(seen[added->kind]||added->end>region->public_end)return false;
                seen[added->kind]=true;
                at=added->end;
                continue;
            }
        }
        protected_code.push_back(token);
        if(token=="op:{")++depth;
        if(token=="op:}") {
            if(!depth)return false;
            --depth;
        }
        ++at;
    }
    return seen[1]&&seen[2]&&seen[3]&&protected_code==*before;
}

// Generate a separate read-only probe; no main, member or copied policy is
// rewritten in the submitted source. A privileged friend is added ONLY after
// the source-preservation prerequisite has accepted the exact original class.
inline std::optional<std::string> cpp_defaulted_move_policy_probe_source(
        const CppDefaultedMovePolicy& spec,std::string_view candidate,
        CppDefaultedMoveProbe mode=CppDefaultedMoveProbe::success) {
    if(!cpp_defaulted_move_policy_matches(spec,candidate))return {};
    const std::string class_pattern=
        R"(\b(class|struct)\s+)"+spec.class_name+R"(\s*\{)";
    const std::regex opening(class_pattern);
    std::match_results<std::string_view::const_iterator> match;
    if(!std::regex_search(candidate.begin(),candidate.end(),match,opening))return {};
    auto instrumented=std::string(candidate);
    instrumented.insert(static_cast<std::size_t>(match.position()+match.length()),
        "\nfriend struct lgaed_move_policy_qa::Access;\n");

    static constexpr std::string_view prelude=R"qa(#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>
#include <new>
namespace lgaed_move_policy_qa {
struct Access;
struct Watch {
    inline static bool active=false;
    inline static unsigned created=0,deleted=0;
    inline static const void* replaced=nullptr;
    inline static bool deleted_replaced=false;
    static void reset() noexcept {
        active=false;created=deleted=0;replaced=nullptr;deleted_replaced=false;
    }
    static void on_delete(const void* pointer) noexcept {
        if(!active||!pointer)return;
        ++deleted;
        if(pointer==replaced) {deleted_replaced=true;replaced=nullptr;}
    }
};
}
void* operator new(std::size_t bytes) {
    if(void* pointer=std::malloc(bytes?bytes:1)) {
        if(lgaed_move_policy_qa::Watch::active)
            ++lgaed_move_policy_qa::Watch::created;
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept {
    lgaed_move_policy_qa::Watch::on_delete(pointer);
    std::free(pointer);
}
void operator delete(void* pointer,std::size_t) noexcept { ::operator delete(pointer); }
void* operator new[](std::size_t bytes) {
    if(void* pointer=std::malloc(bytes?bytes:1)) {
        if(lgaed_move_policy_qa::Watch::active)
            ++lgaed_move_policy_qa::Watch::created;
        return pointer;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* pointer) noexcept {
    lgaed_move_policy_qa::Watch::on_delete(pointer);
    std::free(pointer);
}
void operator delete[](void* pointer,std::size_t) noexcept { ::operator delete[](pointer); }

#define main lgaed_student_original_main
)qa";
    static constexpr std::string_view positive=R"qa(
#undef main
namespace lgaed_move_policy_qa {
struct Access {
    static const int* pointer(const @CLASS@& value) noexcept {
        return value.@MEMBER@.get();
    }
};
}
static_assert(std::is_default_constructible_v<@CLASS@>);
static_assert(std::is_move_constructible_v<@CLASS@>);
static_assert(std::is_move_assignable_v<@CLASS@>);
static_assert(!std::is_copy_constructible_v<@CLASS@>);
static_assert(!std::is_copy_assignable_v<@CLASS@>);
int main() {
    using lgaed_move_policy_qa::Access;
    using lgaed_move_policy_qa::Watch;
    Watch::reset();
    Watch::active=true;
    {
        @CLASS@ source;
        @CLASS@ destination;
        const int* original=Access::pointer(source);
        const int* discarded=Access::pointer(destination);
        if(!original||!discarded||original==discarded||*original!=4||
           *discarded!=4||Watch::created!=2) return 1;
        @CLASS@ intermediate(std::move(source));
        if(Access::pointer(source)||Access::pointer(intermediate)!=original||
           Watch::created!=2||Watch::deleted!=0)return 2;
        Watch::replaced=discarded;
        @CLASS@& returned=(destination=std::move(intermediate));
        if(&returned!=&destination||Access::pointer(intermediate)||
           Access::pointer(destination)!=original||
           *Access::pointer(destination)!=4||Watch::created!=2||
           Watch::deleted!=1||!Watch::deleted_replaced)return 3;
    }
    Watch::active=false;
    if(Watch::created!=2||Watch::deleted!=2)return 4;
    Watch::reset();
    {
        @CLASS@ source;
        if(!source.@PUT@(0))return 5;
        @CLASS@ empty;
        if(!empty.@CLEAR@())return 6;
        const int* zero=Access::pointer(source);
        Watch::active=true;
        @CLASS@ moved(std::move(source));
        Watch::active=false;
        if(!zero||Access::pointer(source)||Access::pointer(moved)!=zero||
           *Access::pointer(moved)!=0||Watch::created!=0||
           Watch::deleted!=0)return 7;
        empty=std::move(moved);
        if(Access::pointer(moved)||Access::pointer(empty)!=zero)return 8;
        if(!empty.@CHANGE@(9)||*Access::pointer(empty)!=9)return 9;
    }
    std::fputs("LGAED_DEFAULTED_MOVE_POLICY_COMPLETE_V1",stderr);
    return 0;
}
)qa";
    static constexpr std::string_view copy_ctor=R"qa(
#undef main
int main() {
    @CLASS@ original;
    @CLASS@ copied(original);
    (void)copied;
}
)qa";
    static constexpr std::string_view copy_assign=R"qa(
#undef main
int main() {
    @CLASS@ original;
    @CLASS@ copied;
    copied=original;
}
)qa";
    std::string driver(mode==CppDefaultedMoveProbe::success?positive:
                       mode==CppDefaultedMoveProbe::rejected_copy_constructor?copy_ctor:
                       copy_assign);
    cpp_defaulted_move_detail::substitute(driver,"@CLASS@",spec.class_name);
    cpp_defaulted_move_detail::substitute(driver,"@MEMBER@",spec.member_name);
    cpp_defaulted_move_detail::substitute(driver,"@PUT@",spec.put_method);
    cpp_defaulted_move_detail::substitute(driver,"@CHANGE@",spec.change_method);
    cpp_defaulted_move_detail::substitute(driver,"@CLEAR@",spec.clear_method);
    return std::string(prelude)+instrumented+driver;
}

} // namespace lgaed::curriculum
