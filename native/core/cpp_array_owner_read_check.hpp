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

// A published single-source editor may declare two writable method bodies.
// Their signatures, class, other methods and main remain byte-for-byte original.
// A separate compiled witness, not a text heuristic or stdout, grades semantics.
struct CppArrayOwnerReadPolicy {
    std::string original_source, class_name, owner_member, count_member;
    std::string quantity_parameter, index_parameter, output_parameter;
    std::string configure_method, read_method, quantity_method, access_method;
    std::string write_method, clear_method, sum_method;
    bool operator==(const CppArrayOwnerReadPolicy&) const = default;
};

inline constexpr std::string_view cpp_array_owner_read_completion_marker =
    "LGAED_ARRAY_OWNER_READ_COMPLETE_V1";

namespace cpp_array_owner_read_detail {
inline bool identifier(std::string_view value) {
    if(value.empty()||value.size()>96||value.starts_with("__")||
       (value.size()>1&&value[0]=='_'&&value[1]>='A'&&value[1]<='Z'))
        return false;
    const auto alpha=[](unsigned char c){
        return c=='_'||(c>='A'&&c<='Z')||(c>='a'&&c<='z');
    };
    if(!alpha(static_cast<unsigned char>(value.front())))return false;
    return std::ranges::all_of(value.substr(1),[&](unsigned char c){
        return alpha(c)||(c>='0'&&c<='9');
    });
}

inline std::string configure_signature(const CppArrayOwnerReadPolicy& spec) {
    return "bool "+spec.class_name+"::"+spec.configure_method+
        "(int "+spec.quantity_parameter+") noexcept";
}
inline std::string read_signature(const CppArrayOwnerReadPolicy& spec) {
    return "bool "+spec.class_name+"::"+spec.read_method+
        "(int "+spec.index_parameter+", int& "+spec.output_parameter+
        ") const noexcept";
}

inline bool original_fields(const CppArrayOwnerReadPolicy& p) {
    const auto scanned=code_format::cpp_tokens(p.original_source);
    if(!scanned||code_format::cpp_position_sensitive_tokens(*scanned))return false;
    const auto& tokens=*scanned;
    std::optional<std::size_t> opening;
    for(std::size_t i=0;i+2<tokens.size();++i)
        if(tokens[i]=="id:class"&&tokens[i+1]=="id:"+p.class_name&&
           tokens[i+2]=="op:{") {
            if(opening)return false;
            opening=i+2;
        }
    if(!opening)return false;
    std::size_t depth=1,closing=0;
    for(auto i=*opening+1;i<tokens.size();++i) {
        if(tokens[i]=="op:{")++depth;
        else if(tokens[i]=="op:}"&&--depth==0){closing=i;break;}
    }
    if(!closing)return false;
    const std::array<std::string,9> owner{{
        "id:std","op:::","id:unique_ptr","op:<","id:int",
        "op:[","op:]","op:>","id:"+p.owner_member}};
    const std::array<std::string,2> count{{
        "id:int","id:"+p.count_member}};
    bool has_owner=false,has_count=false;
    for(auto i=*opening+1;i<closing;++i) {
        if(i+owner.size()<=closing&&std::equal(
                owner.begin(),owner.end(),
                tokens.begin()+static_cast<std::ptrdiff_t>(i)))
            has_owner=true;
        if(i+count.size()<=closing&&std::equal(
                count.begin(),count.end(),
                tokens.begin()+static_cast<std::ptrdiff_t>(i)))
            has_count=true;
    }
    return has_owner&&has_count;
}

inline bool valid(const CppArrayOwnerReadPolicy& spec) {
    for(const auto field:{std::string_view(spec.class_name),
            std::string_view(spec.owner_member),std::string_view(spec.count_member),
            std::string_view(spec.quantity_parameter),
            std::string_view(spec.index_parameter),
            std::string_view(spec.output_parameter),
            std::string_view(spec.configure_method),
            std::string_view(spec.read_method),std::string_view(spec.quantity_method),
            std::string_view(spec.access_method),std::string_view(spec.write_method),
            std::string_view(spec.clear_method),std::string_view(spec.sum_method)})
        if(!identifier(field))return false;
    if(spec.original_source.empty()||spec.original_source.size()>4*1024*1024||
       spec.original_source.find('\0')!=spec.original_source.npos||
       code_format::physical_splice(spec.original_source)||
       !original_fields(spec))return false;
    const CppMemberBodyCheck first{spec.original_source,configure_signature(spec)};
    const CppMemberBodyCheck second{spec.original_source,read_signature(spec)};
    const auto config=cpp_member_body_span(first);
    const auto reader=cpp_member_body_span(second);
    return config&&reader&&config->end<reader->begin;
}

inline bool safe_body(std::string_view source,const CppMemberBodySpan& span) {
    if(span.begin>span.end||span.end>source.size())return false;
    const auto body=source.substr(span.begin,span.end-span.begin);
    if(code_format::physical_splice(body))return false;
    const auto parsed=code_format::cpp_tokens(body);
    if(!parsed)return false;
    for(const auto& token:*parsed)
        if(token.starts_with("directive:")||token=="op:#"||token=="op:##"||
           token=="op:%:"||token=="op:%:%:")return false;
    return !code_format::cpp_position_sensitive_tokens(*parsed);
}
inline void substitute(std::string& text,std::string_view key,std::string_view value) {
    for(std::size_t at=0;(at=text.find(key,at))!=text.npos;) {
        text.replace(at,key.size(),value);
        at+=value.size();
    }
}
} // namespace cpp_array_owner_read_detail

inline bool cpp_array_owner_read_matches(const CppArrayOwnerReadPolicy& spec,
                                         std::string_view candidate) {
    namespace d=cpp_array_owner_read_detail;
    if(!d::valid(spec)||candidate.empty()||candidate.size()>4*1024*1024||
       candidate.find('\0')!=candidate.npos||
       code_format::physical_splice(candidate))return false;
    const auto original_config=cpp_member_body_span(
        CppMemberBodyCheck{spec.original_source,d::configure_signature(spec)});
    const auto original_read=cpp_member_body_span(
        CppMemberBodyCheck{spec.original_source,d::read_signature(spec)});
    const std::string revised(candidate);
    const auto candidate_config=cpp_member_body_span(
        CppMemberBodyCheck{revised,d::configure_signature(spec)});
    const auto candidate_read=cpp_member_body_span(
        CppMemberBodyCheck{revised,d::read_signature(spec)});
    if(!original_config||!original_read||!candidate_config||!candidate_read||
       original_config->end>=original_read->begin||
       candidate_config->end>=candidate_read->begin||
       !d::safe_body(candidate,*candidate_config)||
       !d::safe_body(candidate,*candidate_read))return false;
    // Compare three protected intervals exactly; both edited bodies are
    // excluded, but their original closing braces and every other byte stay.
    const auto& old=spec.original_source;
    return old.substr(0,original_config->begin)==
               candidate.substr(0,candidate_config->begin)&&
        old.substr(original_config->end,
                   original_read->begin-original_config->end)==
               candidate.substr(candidate_config->end,
                                candidate_read->begin-candidate_config->end)&&
        std::string_view(old).substr(original_read->end)==
               candidate.substr(candidate_read->end);
}

// Instrumentation occurs only after preserving the original class, and only
// in a temporary private source. The student's submitted source and protected
// curricular lesson are never rewritten or persisted with the QA friend.
inline std::optional<std::string> cpp_array_owner_read_private_driver(
        const CppArrayOwnerReadPolicy& spec,std::string_view candidate) {
    if(!cpp_array_owner_read_matches(spec,candidate))return {};
    const auto class_open="class "+spec.class_name+" {\n";
    auto source=std::string(candidate);
    const auto at=source.find(class_open);
    if(at==source.npos||source.find(class_open,at+class_open.size())!=source.npos)
        return {};
    source.insert(at+class_open.size(),
        "    friend struct lgaed_array_qa::Access;\n");
    static constexpr std::string_view prelude=R"lgaed(#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace lgaed_array_qa {
struct Access;
struct Watch {
    inline static bool active=false,fail_next_array=false;
    inline static unsigned arrays_new=0,arrays_delete=0;
    inline static unsigned singles_new=0,singles_delete=0;
    inline static const void* last_created_array=nullptr;
    inline static const void* expected_deleted=nullptr;
    inline static bool saw_expected_deleted=false;
    static void watch_delete(const void* ptr,bool array) noexcept {
        if(!active||!ptr)return;
        if(array)++arrays_delete; else ++singles_delete;
        if(array && ptr==expected_deleted) {
            saw_expected_deleted=true;
            expected_deleted=nullptr;
        }
    }
    static void expect_delete(const void* ptr) {
        expected_deleted=ptr;saw_expected_deleted=false;
    }
};
}
void* operator new(std::size_t bytes) {
    if(void* p=std::malloc(bytes?bytes:1)) {
        if(lgaed_array_qa::Watch::active)++lgaed_array_qa::Watch::singles_new;
        return p;
    }
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept {
    lgaed_array_qa::Watch::watch_delete(p,false);
    std::free(p);
}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void* operator new[](std::size_t bytes) {
    auto& failure=lgaed_array_qa::Watch::fail_next_array;
    if(failure){failure=false;throw std::bad_alloc{};}
    if(void* p=std::malloc(bytes?bytes:1)) {
        if(lgaed_array_qa::Watch::active) {
            ++lgaed_array_qa::Watch::arrays_new;
            lgaed_array_qa::Watch::last_created_array=p;
        }
        return p;
    }
    throw std::bad_alloc{};
}
void operator delete[](void* p) noexcept {
    lgaed_array_qa::Watch::watch_delete(p,true);
    std::free(p);
}
void operator delete[](void* p,std::size_t) noexcept {::operator delete[](p);}
#define main lgaed_original_lote_main
)lgaed";
    static constexpr std::string_view driver=R"lgaed(
#undef main
namespace lgaed_array_qa {
struct Access {
    static void set_window(@CLASS@& lot,int* p,int count) {
        lot.@OWNER@.reset(p);
        lot.@COUNT@=count;
    }
    static void restore_window(@CLASS@& lot,int* base) noexcept {
        (void)lot.@OWNER@.release();
        lot.@OWNER@.reset(base);
        lot.@COUNT@=4;
    }
};
struct Window {
    @CLASS@ lot;
    int* base;
    Window():lot{} {
        auto owned=std::make_unique<int[]>(4);
        base=owned.release();
        base[0]=91;
        base[1]=5;
        base[2]=7;
        base[3]=73;
        Access::set_window(lot,base+1,2);
    }
    ~Window() {Access::restore_window(lot,base);}
    Window(const Window&)=delete;
    Window& operator=(const Window&)=delete;
};
}

int main() {
    using lgaed_array_qa::Watch;
    Watch::active=true;
    {
        @CLASS@ lot;
        if(lot.@QUANTITY@()!=0||lot.@ACCESS@()!=nullptr)return 1;
        if(!lot.@CONFIGURE@(2)||lot.@QUANTITY@()!=2||!lot.@ACCESS@()||
           lot.@ACCESS@()!=Watch::last_created_array||
           Watch::arrays_new!=1||Watch::arrays_delete!=0||
           lot.@SUM@()!=0)return 2;
        int out=84;
        if(!lot.@READ@(0,out)||out!=0||!lot.@READ@(1,out)||out!=0)return 3;
        if(!lot.@WRITE@(0,5)||!lot.@WRITE@(1,7)||lot.@SUM@()!=12||
           !lot.@READ@(1,out)||out!=7)return 4;

        // Both logical invalid indices access safe padding in a separate
        // independent window; a faulty unchecked read never needs to touch
        // physically invalid storage in these controlled probes.
        {
            lgaed_array_qa::Window padded;
            int sentinel=88;
            if(padded.lot.@READ@(-1,sentinel)||sentinel!=88)return 5;
            sentinel=214;
            if(padded.lot.@READ@(2,sentinel)||sentinel!=214)return 6;
            sentinel=41;
            if(!padded.lot.@READ@(1,sentinel)||sentinel!=7)return 7;
        }
        const int* owner=lot.@ACCESS@();
        const auto created=Watch::arrays_new,deleted=Watch::arrays_delete;
        for(int invalid : {-2147483647-1,-1,5,2147483647}) {
            if(lot.@CONFIGURE@(invalid)||lot.@ACCESS@()!=owner||
               lot.@QUANTITY@()!=2||lot.@SUM@()!=12||
               Watch::arrays_new!=created||Watch::arrays_delete!=deleted)
                return 8;
        }
        if(!lot.@CONFIGURE@(2)||lot.@QUANTITY@()!=2||lot.@SUM@()!=0||
           lot.@ACCESS@()!=Watch::last_created_array||
           Watch::arrays_new!=created+1||Watch::arrays_delete!=deleted+1)
            return 9;
        if(!lot.@WRITE@(0,9)||!lot.@WRITE@(1,3))return 10;
        const int* before_failure=lot.@ACCESS@();
        const auto created_before_failure=Watch::arrays_new;
        const auto deleted_before_failure=Watch::arrays_delete;
        Watch::fail_next_array=true;
        bool accepted=false;
        try {accepted=lot.@CONFIGURE@(3);}
        catch (...) {Watch::fail_next_array=false;return 11;}
        Watch::fail_next_array=false;
        if(accepted||lot.@ACCESS@()!=before_failure||lot.@QUANTITY@()!=2||
           lot.@SUM@()!=12||Watch::arrays_new!=created_before_failure||
           Watch::arrays_delete!=deleted_before_failure)return 12;

        // Fresh four-element array, even after a preceding failed request.
        Watch::expect_delete(lot.@ACCESS@());
        const auto created_before_four=Watch::arrays_new;
        const auto deleted_before_four=Watch::arrays_delete;
        if(!lot.@CONFIGURE@(4)||lot.@QUANTITY@()!=4||lot.@SUM@()!=0||
           lot.@ACCESS@()!=Watch::last_created_array||
           Watch::arrays_new!=created_before_four+1||
           Watch::arrays_delete!=deleted_before_four+1||
           !Watch::saw_expected_deleted)return 13;
        for(int index=0;index<4;++index) {
            int value=9;
            if(!lot.@READ@(index,value)||value!=0)return 14;
        }
        if(!lot.@WRITE@(3,8)||lot.@SUM@()!=8)return 15;

        // Zero must release the exact previous array, without allocating.
        Watch::expect_delete(lot.@ACCESS@());
        const auto created_before_empty=Watch::arrays_new;
        const auto deleted_before_empty=Watch::arrays_delete;
        if(!lot.@CONFIGURE@(0)||lot.@QUANTITY@()!=0||lot.@ACCESS@()||
           Watch::arrays_new!=created_before_empty||
           Watch::arrays_delete!=deleted_before_empty+1||
           !Watch::saw_expected_deleted)return 16;
        const auto empty_created=Watch::arrays_new,empty_deleted=Watch::arrays_delete;
        if(!lot.@CONFIGURE@(0)||lot.@QUANTITY@()!=0||lot.@ACCESS@()||
           Watch::arrays_new!=empty_created||
           Watch::arrays_delete!=empty_deleted)return 17;
        if(!lot.@CONFIGURE@(1)||lot.@QUANTITY@()!=1||!lot.@ACCESS@()||
           lot.@SUM@()!=0)return 18;
        int valid=91;
        if(!lot.@READ@(0,valid)||valid!=0)return 19;

        // A rejected update or invalid write keeps present zero and capacity.
        const int* identity=lot.@ACCESS@();
        if(lot.@WRITE@(0,10)||lot.@WRITE@(1,9)||lot.@WRITE@(-1,2)||
           lot.@ACCESS@()!=identity||lot.@QUANTITY@()!=1||lot.@SUM@()!=0)
            return 20;

        // Unwinding a caller's exception frees the owned array exactly once.
        const auto created_before_throw=Watch::arrays_new;
        const auto deleted_before_throw=Watch::arrays_delete;
        bool caught=false;
        try {
            @CLASS@ temporary;
            if(!temporary.@CONFIGURE@(3))return 21;
            if(!temporary.@WRITE@(2,6))return 22;
            throw 31;
        }catch(int code){caught=(code==31);}
        if(!caught||Watch::arrays_new!=created_before_throw+1||
           Watch::arrays_delete!=deleted_before_throw+1)return 23;
    }
    Watch::active=false;
    if(Watch::arrays_new!=Watch::arrays_delete||
       Watch::singles_new!=Watch::singles_delete||
       Watch::fail_next_array)return 24;
    std::fputs("LGAED_ARRAY_OWNER_READ_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    auto private_driver=std::string(driver);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,9>{{
        {"@CLASS@",spec.class_name},{"@OWNER@",spec.owner_member},
        {"@COUNT@",spec.count_member},{"@CONFIGURE@",spec.configure_method},
        {"@READ@",spec.read_method},{"@QUANTITY@",spec.quantity_method},
        {"@ACCESS@",spec.access_method},{"@WRITE@",spec.write_method},
        {"@SUM@",spec.sum_method}}})
        cpp_array_owner_read_detail::substitute(private_driver,key,value);
    return std::string(prelude)+source+private_driver;
}
} // namespace lgaed::curriculum
