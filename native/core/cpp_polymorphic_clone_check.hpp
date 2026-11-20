#pragma once

#include "core/cpp_abstract_interface_check.hpp"
#include "core/cpp_multi_body_check.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lgaed::curriculum {

// The published class hierarchy, factory, source collection, renderer and
// main remain immutable. Only two concrete clone bodies and one free
// collection-copy body are authored by the student.
struct CppPolymorphicClonePolicy {
    std::string original_source;
    std::string base_class, simple_class, identified_class;
    std::string vector_alias, clone_method, duplicate_function;
    std::string number_method, change_method, write_method, code_method;
    std::string source_parameter, stream_parameter;
    bool operator==(const CppPolymorphicClonePolicy&) const = default;
};

inline constexpr std::string_view cpp_polymorphic_clone_completion_marker =
    "LGAED_POLYMORPHIC_CLONE_COMPLETE_V1";

namespace cpp_polymorphic_clone_detail {

inline bool identifier(std::string_view text) {
    if(text.empty() || text.size()>96 || text.starts_with("__") ||
       (text.size()>1 && text[0]=='_' && text[1]>='A' && text[1]<='Z'))
        return false;
    const auto start=[](unsigned char c) {
        return c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z');
    };
    if(!start(static_cast<unsigned char>(text.front())))return false;
    return std::all_of(text.begin()+1,text.end(),[&](unsigned char c) {
        return start(c)||(c>='0'&&c<='9');
    });
}

// Authenticate the collection alias as a top-level C++ declaration, not
// quoted documentation, a comment, a namespace-local name or a disabled
// preprocessor branch. Compare tokens so ordinary spacing and comments do
// not create a false incompatibility in the unmodified authored source.
inline bool authentic_vector_alias(std::string_view source,
        std::string_view alias,std::string_view base) {
    const std::string declaration="using "+std::string(alias)+
        " = std::vector<std::unique_ptr<"+std::string(base)+">>;";
    const auto expected=code_format::cpp_tokens(declaration);
    const auto lexed=code_format::cpp_tokens(source);
    if(!expected||!lexed)return false;
    std::vector<std::string_view> tokens;
    tokens.reserve(lexed->size());
    for(const auto& token:*lexed) {
        if(token.starts_with("comment:"))continue;
        if(token.starts_with("directive:")) {
            // The source used by this lesson needs only #include directives.
            // #if / #define could otherwise manufacture a lexical alias
            // that is not part of the actual compiled program.
            if(!token.starts_with("directive:#include") &&
               !token.starts_with("directive:%:include"))return false;
            continue;
        }
        tokens.emplace_back(token);
    }
    std::size_t braces=0;
    bool found=false;
    for(std::size_t i=0;i<tokens.size();++i) {
        if(braces==0&&i+1<tokens.size()&&
           tokens[i]=="id:using"&&tokens[i+1]=="id:"+std::string(alias)) {
            if(found||i+expected->size()>tokens.size()||
               !std::equal(expected->begin(),expected->end(),
                           tokens.begin()+static_cast<std::ptrdiff_t>(i)))
                return false;
            found=true;
        }
        if(tokens[i]=="op:{"||tokens[i]=="op:<%")++braces;
        else if(tokens[i]=="op:}"||tokens[i]=="op:%>") {
            if(!braces)return false;
            --braces;
        }
    }
    return found&&braces==0;
}

inline std::optional<std::array<CppMemberBodySpan,3>> spans(
        const CppPolymorphicClonePolicy& policy,std::string_view text,
        bool check_contents) {
    namespace multi=cpp_multi_body_detail;
    namespace abstract=cpp_abstract_interface_detail;
    if(!multi::valid_source(text))return {};
    const auto method="std::unique_ptr<"+policy.base_class+"> "+
        policy.clone_method+"() const override";
    const auto simple=abstract::inline_body(
        text,policy.simple_class,policy.base_class,method);
    const auto identified=abstract::inline_body(
        text,policy.identified_class,policy.base_class,method);
    const auto duplicate=multi::find_body(text,{
        policy.vector_alias+" "+policy.duplicate_function+
        "(const "+policy.vector_alias+"& "+policy.source_parameter+")",false
    });
    if(!simple||!identified||!duplicate||
       simple->end>=identified->begin||identified->end>=duplicate->begin)
        return {};
    const std::array<CppMemberBodySpan,3> result{
        *simple,*identified,*duplicate
    };
    if(check_contents)
        for(const auto& span:result)
            if(!multi::safe_body(text,span))return {};
    return result;
}

inline bool valid(const CppPolymorphicClonePolicy& policy) {
    for(std::string_view name:{
            std::string_view(policy.base_class),
            std::string_view(policy.simple_class),
            std::string_view(policy.identified_class),
            std::string_view(policy.vector_alias),
            std::string_view(policy.clone_method),
            std::string_view(policy.duplicate_function),
            std::string_view(policy.number_method),
            std::string_view(policy.change_method),
            std::string_view(policy.write_method),
            std::string_view(policy.code_method),
            std::string_view(policy.source_parameter),
            std::string_view(policy.stream_parameter)})
        if(!identifier(name))return false;
    namespace abstract=cpp_abstract_interface_detail;
    const auto& src=policy.original_source;
    const auto getter="int "+policy.number_method+"() const noexcept override";
    const auto changer="void "+policy.change_method+"(int valor) noexcept override";
    const auto writer="void "+policy.write_method+
        "(std::ostream& "+policy.stream_parameter+") const override";
    // The original virtual contract must exist as code directly in the
    // actual base class. A copied signature in a comment, raw string or a
    // nested helper cannot authenticate a polymorphic ownership exercise.
    return spans(policy,src,false).has_value()&&
        abstract::direct_class_declaration(src,policy.base_class,
            "virtual ~"+policy.base_class+"() = default;")&&
        abstract::direct_class_declaration(src,policy.base_class,
            "virtual std::unique_ptr<"+policy.base_class+"> "+
                policy.clone_method+"() const = 0;")&&
        abstract::direct_class_declaration(src,policy.base_class,
            "virtual int "+policy.number_method+"() const noexcept = 0;")&&
        abstract::direct_class_declaration(src,policy.base_class,
            "virtual void "+policy.change_method+"(int valor) noexcept = 0;")&&
        abstract::direct_class_declaration(src,policy.base_class,
            "virtual void "+policy.write_method+
                "(std::ostream& "+policy.stream_parameter+") const = 0;")&&
        abstract::inline_body(src,policy.simple_class,policy.base_class,
            getter).has_value()&&
        abstract::inline_body(src,policy.identified_class,policy.base_class,
            getter).has_value()&&
        abstract::inline_body(src,policy.simple_class,policy.base_class,
            changer).has_value()&&
        abstract::inline_body(src,policy.identified_class,policy.base_class,
            changer).has_value()&&
        abstract::inline_body(src,policy.simple_class,policy.base_class,
            writer).has_value()&&
        abstract::inline_body(src,policy.identified_class,policy.base_class,
            writer).has_value()&&
        abstract::inline_body(src,policy.identified_class,policy.base_class,
            "int "+policy.code_method+"() const noexcept").has_value()&&
        authentic_vector_alias(src,policy.vector_alias,policy.base_class);
}

inline void substitute(std::string& text,std::string_view token,
                       std::string_view replacement) {
    for(std::size_t at=0;(at=text.find(token,at))!=text.npos;) {
        text.replace(at,token.size(),replacement);
        at+=replacement.size();
    }
}

} // namespace cpp_polymorphic_clone_detail

inline bool cpp_polymorphic_clone_matches(
        const CppPolymorphicClonePolicy& policy,std::string_view candidate) {
    namespace detail=cpp_polymorphic_clone_detail;
    if(!detail::valid(policy))return false;
    const auto old=detail::spans(policy,policy.original_source,false);
    const auto edited=detail::spans(policy,candidate,true);
    if(!old||!edited)return false;
    const std::string_view before=policy.original_source;
    std::size_t a=0,b=0;
    for(std::size_t i=0;i<old->size();++i) {
        const auto& left=old->at(i);
        const auto& right=edited->at(i);
        if(before.substr(a,left.begin-a)!=
           candidate.substr(b,right.begin-b))return false;
        a=left.end;b=right.end;
    }
    return before.substr(a)==candidate.substr(b);
}

// Independent C++ proof covers concrete dynamic types, deep ownership,
// virtual cloning of a third type, zero/negative/extreme values, mutation
// independence, source lifetime and a controlled exception from clone().
// The candidate's original main is renamed, never used as a semantic oracle.
inline std::optional<std::string> cpp_polymorphic_clone_private_driver(
        const CppPolymorphicClonePolicy& policy,std::string_view candidate) {
    if(!cpp_polymorphic_clone_matches(policy,candidate))return {};
    constexpr std::string_view prelude=R"lgaed(#include <array>
#include <climits>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#define main lgaed_published_original_main
)lgaed";
    constexpr std::string_view probe=R"lgaed(
#undef main
struct LgaedCloneCounts {
    int cloned=0, destroyed=0;
};
class LgaedNewKind final : public @BASE@ {
public:
    LgaedNewKind(int number,int extra,LgaedCloneCounts* counts,bool fail=false)
      : number_{number},extra_{extra},counts_{counts},fail_{fail} {}
    ~LgaedNewKind() override { ++counts_->destroyed; }
    int @NUMBER@() const noexcept override { return number_; }
    void @CHANGE@(int value) noexcept override { number_=value; }
    void @WRITE@(std::ostream& destination) const override {
        destination<<"Otro:"<<number_<<";Extra:"<<extra_;
    }
    std::unique_ptr<@BASE@> @CLONE@() const override {
        ++counts_->cloned;
        if(fail_)throw std::bad_alloc{};
        return std::make_unique<LgaedNewKind>(number_,extra_,counts_);
    }
    int extra() const noexcept { return extra_; }
private:
    int number_,extra_;
    LgaedCloneCounts* counts_;
    bool fail_;
};
int main() {
    {
        constexpr std::array<int,5> numbers{0,5,-9,INT_MIN,INT_MAX};
        for(int n:numbers)for(int code:{0,-7,21}) {
            @SIMPLE@ simple{n};
            auto simple_clone=simple.@CLONE@();
            if(!simple_clone||simple_clone.get()==&simple||
               !dynamic_cast<@SIMPLE@*>(simple_clone.get())||
               dynamic_cast<@IDENTIFIED@*>(simple_clone.get())||
               simple_clone->@NUMBER@()!=n)return 1;
            std::ostringstream simple_out;
            simple_clone->@WRITE@(simple_out);
            if(simple_out.str()!="Numero:"+std::to_string(n))return 2;
            simple.@CHANGE@(17);
            if(simple_clone->@NUMBER@()!=n)return 3;
            simple_clone->@CHANGE@(-31);
            if(simple.@NUMBER@()!=17)return 4;
            @IDENTIFIED@ identified{n,code};
            auto identified_clone=identified.@CLONE@();
            auto* typed=dynamic_cast<@IDENTIFIED@*>(identified_clone.get());
            if(!identified_clone||identified_clone.get()==&identified||
               !typed||dynamic_cast<@SIMPLE@*>(identified_clone.get())||
               identified_clone->@NUMBER@()!=n||
               typed->@CODE@()!=code)return 5;
            std::ostringstream identified_out;
            identified_clone->@WRITE@(identified_out);
            if(identified_out.str()!="Numero:"+std::to_string(n)+
                   ";Codigo:"+std::to_string(code))return 6;
            identified.@CHANGE@(11);
            if(identified_clone->@NUMBER@()!=n)return 7;
            identified_clone->@CHANGE@(-13);
            if(identified.@NUMBER@()!=11||typed->@CODE@()!=code)return 8;
        }
    }
    {
        // Build a larger collection with duplicates and both concrete types.
        @VECTOR@ originals;
        std::vector<const @BASE@*> identities;
        for(int i=0;i<28;++i) {
            const int value=i==0?INT_MIN:i==1?INT_MAX:
                            i%5==0?0:i%5==1?-9:7;
            const int code=i%4==0?0:i%4==1?INT_MAX:
                           i%4==2?INT_MIN:-12;
            if(i%2==0)originals.push_back(
                std::make_unique<@SIMPLE@>(value));
            else originals.push_back(
                std::make_unique<@IDENTIFIED@>(value,code));
            identities.push_back(originals.back().get());
        }
        const auto copies=@DUPLICATE@(originals);
        if(copies.size()!=originals.size())return 9;
        for(std::size_t i=0;i<copies.size();++i) {
            if(!copies[i]||copies[i].get()==identities[i]||
               originals[i].get()!=identities[i]||
               copies[i]->@NUMBER@()!=originals[i]->@NUMBER@())
                return 10;
            if(i%2==0) {
                if(!dynamic_cast<@SIMPLE@*>(copies[i].get())||
                   dynamic_cast<@IDENTIFIED@*>(copies[i].get()))
                    return 11;
            }else{
                auto* a=dynamic_cast<@IDENTIFIED@*>(originals[i].get());
                auto* b=dynamic_cast<@IDENTIFIED@*>(copies[i].get());
                if(!a||!b||a->@CODE@()!=b->@CODE@())return 12;
            }
            originals[i]->@CHANGE@(static_cast<int>(i)*37+1);
            if(copies[i]->@NUMBER@()==originals[i]->@NUMBER@())
                return 13;
            copies[i]->@CHANGE@(static_cast<int>(i)*37+2);
            if(originals[i]->@NUMBER@()!=static_cast<int>(i)*37+1)return 14;
        }
        originals.clear();
        for(std::size_t i=0;i<copies.size();++i)
            if(!copies[i]||copies[i]->@NUMBER@()!=int(i)*37+2)
                return 15;
    }
    {
        // The extra class is unknown to the student's provided source.
        LgaedCloneCounts counts;
        {
            @VECTOR@ originals;
            originals.push_back(std::make_unique<LgaedNewKind>(
                -5,93,&counts));
            originals.push_back(std::make_unique<LgaedNewKind>(
                0,INT_MAX,&counts));
            const auto* first=originals[0].get();
            const auto copies=@DUPLICATE@(originals);
            if(counts.cloned!=2||copies.size()!=2||
               originals[0].get()!=first||counts.destroyed!=0)
                return 16;
            for(std::size_t i=0;i<copies.size();++i) {
                auto* other=dynamic_cast<LgaedNewKind*>(copies[i].get());
                if(!other||copies[i].get()==originals[i].get()||
                   other->extra()!=(i==0?93:INT_MAX))
                    return 17;
            }
            originals[0]->@CHANGE@(100);
            if(copies[0]->@NUMBER@()!=-5)return 18;
            originals.clear();
            if(counts.destroyed!=2||copies[1]->@NUMBER@()!=0)
                return 19;
        }
        if(counts.destroyed!=4)return 20;
    }
    {
        // A controlled failure after two successful clones must discard
        // local copies and keep every original object untouched.
        LgaedCloneCounts counts;
        @VECTOR@ originals;
        originals.push_back(std::make_unique<LgaedNewKind>(1,10,&counts));
        originals.push_back(std::make_unique<LgaedNewKind>(2,20,&counts));
        originals.push_back(std::make_unique<LgaedNewKind>(3,30,&counts,true));
        const auto* address=originals[0].get();
        @VECTOR@ destination;
        destination.push_back(std::make_unique<LgaedNewKind>(-4,40,&counts));
        const auto* saved=destination[0].get();
        bool caught=false;
        try{ destination=@DUPLICATE@(originals); }
        catch(const std::bad_alloc&){ caught=true; }
        if(!caught||counts.cloned!=3||counts.destroyed!=2||
           originals[0].get()!=address||destination.size()!=1||
           destination[0].get()!=saved||
           originals[0]->@NUMBER@()!=1||originals[1]->@NUMBER@()!=2||
           originals[2]->@NUMBER@()!=3)return 21;
        originals.clear();
        destination.clear();
        if(counts.destroyed!=6)return 22;
    }
    {
        @VECTOR@ empty;
        const auto copied=@DUPLICATE@(empty);
        if(!copied.empty()||!empty.empty())return 23;
    }
    std::fputs("LGAED_POLYMORPHIC_CLONE_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    std::string witness(probe);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,10>{{
        {"@BASE@",policy.base_class},
        {"@SIMPLE@",policy.simple_class},
        {"@IDENTIFIED@",policy.identified_class},
        {"@VECTOR@",policy.vector_alias},
        {"@CLONE@",policy.clone_method},
        {"@DUPLICATE@",policy.duplicate_function},
        {"@NUMBER@",policy.number_method},
        {"@CHANGE@",policy.change_method},
        {"@WRITE@",policy.write_method},
        {"@CODE@",policy.code_method}
    }})cpp_polymorphic_clone_detail::substitute(witness,key,value);
    return std::string(prelude)+std::string(candidate)+witness;
}
} // namespace lgaed::curriculum
