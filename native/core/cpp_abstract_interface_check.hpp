#pragma once

#include "core/cpp_multi_body_check.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Four author-declared implementations of an existing abstract interface:
// two inline virtual functions, one type-selecting unique-owner factory,
// and one virtual consumer. The provided abstract interface, signatures,
// class members, constructors, getter implementations and main are immutable.
struct CppAbstractInterfacePolicy {
    std::string original_source;
    std::string base_class, simple_class, identified_class;
    std::string create_function, present_function;
    std::string number_method, code_method, write_method;
    std::string stream_parameter, type_parameter, number_parameter;
    std::string code_parameter, object_parameter;
    bool operator==(const CppAbstractInterfacePolicy&) const = default;
};

inline constexpr std::string_view cpp_abstract_interface_completion_marker =
    "LGAED_ABSTRACT_INTERFACE_COMPLETE_V1";

namespace cpp_abstract_interface_detail {

inline bool identifier(std::string_view name) {
    if(name.empty()||name.size()>96||name.starts_with("__")||
       (name.size()>1&&name[0]=='_'&&name[1]>='A'&&name[1]<='Z'))
        return false;
    const auto alpha=[](unsigned char c) {
        return c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z');
    };
    if(!alpha(static_cast<unsigned char>(name.front())))return false;
    return std::all_of(name.begin()+1,name.end(),[&](unsigned char c) {
        return alpha(c)||(c>='0'&&c<='9');
    });
}

// Require a genuine direct base-class declaration. Searching the whole
// source as text can treat a comment, a raw string, or an inner class as the
// virtual contract that the provided program must actually contain.
inline bool direct_class_declaration(
        std::string_view source,std::string_view class_name,
        std::string_view declaration) {
    namespace scanner=cpp_member_body_detail;
    const auto signature="class "+std::string(class_name);
    const auto class_at=scanner::definition_start(source,signature);
    if(!class_at)return false;
    const auto open=scanner::definition_opening(
        source,*class_at+signature.size());
    if(!open)return false;
    const auto end=scanner::closing_brace(source,open->position);
    if(!end)return false;
    const auto begin=open->position+open->length;
    const auto tokens=code_format::cpp_tokens(source.substr(begin,*end-begin));
    const auto expected=code_format::cpp_tokens(declaration);
    if(!tokens||!expected||expected->empty())return false;
    std::vector<std::string_view> actual;
    actual.reserve(tokens->size());
    for(const auto& token:*tokens) {
        if(token.starts_with("comment:"))continue;
        // A preprocessor branch can change which declaration exists.
        // Do not authenticate an ambiguous base-class source without it
        // being resolved by a trusted complete publication.
        if(token.starts_with("directive:"))return false;
        actual.push_back(token);
    }
    std::size_t depth=0;
    bool found=false;
    for(std::size_t i=0;i<actual.size();++i) {
        if(depth==0 && i+expected->size()<=actual.size() &&
           std::equal(expected->begin(),expected->end(),actual.begin()+i)) {
            if(found)return false;
            found=true;
        }
        if(actual[i]=="op:{"||actual[i]=="op:<%")++depth;
        else if(actual[i]=="op:}"||actual[i]=="op:%>") {
            if(depth==0)return false;
            --depth;
        }
    }
    return depth==0&&found;
}

// Only methods directly declared in the selected class are its editable
// bodies. A nested helper class can have the same virtual signature without
// defining or replacing the outer owner's method. Track actual C++ brace
// nesting while ignoring comments, quoted strings and raw string literals.
inline std::optional<std::size_t> direct_inline_definition(
        std::string_view scope,std::string_view signature) {
    namespace scanner=cpp_member_body_detail;
    std::optional<std::size_t> found;
    std::size_t depth=0;
    bool line_leading=true;
    for(std::size_t at=0;at<scope.size();) {
        const auto c=scope[at];
        if(c=='\n'||c=='\r'){line_leading=true;++at;continue;}
        if(scanner::horizontal_space(c)){++at;continue;}
        if(c=='/'&&at+1<scope.size()&&scope[at+1]=='/') {
            const auto end=scope.find_first_of("\r\n",at+2);
            at=end==scope.npos?scope.size():end;
            continue;
        }
        if(c=='/'&&at+1<scope.size()&&scope[at+1]=='*') {
            const auto end=scope.find("*/",at+2);
            if(end==scope.npos)return {};
            if(scope.substr(at,end+2-at).find_first_of("\r\n")!=scope.npos)
                line_leading=true;
            at=end+2;continue;
        }
        if((c=='R'&&at+1<scope.size()&&scope[at+1]=='"')||
           c=='"'||c=='\'') {
            const auto end=scanner::quoted_end(scope,at);
            if(!end)return {};
            at=*end;line_leading=false;continue;
        }
        if(depth==0&&line_leading&&scope.substr(at).starts_with(signature)&&
           scanner::definition_opening(scope,at+signature.size())) {
            if(found)return {};
            found=at;
        }
        if(c=='{'||(c=='<'&&at+1<scope.size()&&scope[at+1]=='%')) {
            ++depth;
            at+=(c=='{'?1:2);
        }else if(c=='}'||(c=='%'&&at+1<scope.size()&&scope[at+1]=='>')) {
            if(depth==0)return {};
            --depth;
            at+=(c=='}'?1:2);
        }else ++at;
        line_leading=false;
    }
    return depth==0?found:std::nullopt;
}

// Inline methods have identical signatures in the two concrete classes.
// Discover them by their *direct owning class*, not by a nested helper,
// a sibling class, or a string such as "Pendiente".
inline std::optional<CppMemberBodySpan> inline_body(
        std::string_view code,std::string_view class_name,
        std::string_view base_name,std::string_view signature) {
    namespace scanner=cpp_member_body_detail;
    // Match genuine line-leading definitions, not example classes copied
    // into comments, string literals or raw documentation. The same lexical
    // scanner already protects qualified member definitions and free bodies.
    const auto class_signature="class "+std::string(class_name)+
        " : public "+std::string(base_name);
    const auto class_at=scanner::definition_start(code,class_signature);
    if(!class_at)return {};
    const auto class_open=scanner::definition_opening(
        code,*class_at+class_signature.size());
    if(!class_open)return {};
    const auto class_end=scanner::closing_brace(code,class_open->position);
    if(!class_end)return {};

    // Search only inside the authenticated class. An identical virtual
    // method in a sibling class or a quoted example is not this owner.
    const auto start=class_open->position+class_open->length;
    const auto scope=code.substr(start,*class_end-start);
    const auto local_at=direct_inline_definition(scope,signature);
    if(!local_at)return {};
    const auto method_open=scanner::definition_opening(
        code,start+*local_at+signature.size());
    if(!method_open||method_open->position>=*class_end)return {};
    const auto method_end=scanner::closing_brace(code,method_open->position);
    if(!method_end||*method_end>=*class_end)return {};
    return CppMemberBodySpan{
        method_open->position+method_open->length,*method_end};
}

inline std::optional<std::array<CppMemberBodySpan,4>> spans(
        const CppAbstractInterfacePolicy& p,std::string_view source,
        bool verify_bodies) {
    if(!cpp_multi_body_detail::valid_source(source))return {};
    const auto method="void "+p.write_method+
        "(std::ostream& "+p.stream_parameter+") const override";
    const auto simple=inline_body(source,p.simple_class,p.base_class,method);
    const auto identified=
        inline_body(source,p.identified_class,p.base_class,method);
    const auto factory=cpp_multi_body_detail::find_body(source,{
        "std::unique_ptr<"+p.base_class+"> "+p.create_function+
        "(int "+p.type_parameter+", int "+p.number_parameter+
        ", int "+p.code_parameter+")",false
    });
    const auto presenter=cpp_multi_body_detail::find_body(source,{
        "void "+p.present_function+"(const "+p.base_class+"& "+
        p.object_parameter+", std::ostream& "+p.stream_parameter+")",false
    });
    if(!simple||!identified||!factory||!presenter||
       simple->end>=identified->begin||
       identified->end>=factory->begin||factory->end>=presenter->begin)
        return {};
    const std::array<CppMemberBodySpan,4> parts{*simple,*identified,
                                                *factory,*presenter};
    if(verify_bodies)
        for(const auto& part:parts)
            if(!cpp_multi_body_detail::safe_body(source,part))return {};
    return parts;
}

inline bool valid(const CppAbstractInterfacePolicy& p) {
    for(std::string_view value:{std::string_view(p.base_class),
            std::string_view(p.simple_class),std::string_view(p.identified_class),
            std::string_view(p.create_function),std::string_view(p.present_function),
            std::string_view(p.number_method),std::string_view(p.code_method),
            std::string_view(p.write_method),std::string_view(p.stream_parameter),
            std::string_view(p.type_parameter),
            std::string_view(p.number_parameter),std::string_view(p.code_parameter),
            std::string_view(p.object_parameter)})
        if(!identifier(value))return false;
    const auto& source=p.original_source;
    if(!spans(p,source,false))return false;
    // The provided interface and getters are an executable prerequisite,
    // not a phrase that may be quoted elsewhere in its original source.
    // Restrict each declaration or body to the real owning class.
    const auto getter="int "+p.number_method+"() const noexcept override";
    return direct_class_declaration(source,p.base_class,
               "virtual ~"+p.base_class+"() = default;")&&
        direct_class_declaration(source,p.base_class,
               "virtual int "+p.number_method+"() const noexcept = 0;")&&
        direct_class_declaration(source,p.base_class,
               "virtual void "+p.write_method+"(std::ostream& "+
                   p.stream_parameter+") const = 0;")&&
        inline_body(source,p.simple_class,p.base_class,getter).has_value()&&
        inline_body(source,p.identified_class,p.base_class,getter).has_value()&&
        inline_body(source,p.identified_class,p.base_class,
               "int "+p.code_method+"() const noexcept").has_value();
}

inline void substitute(std::string& code,std::string_view key,
                       std::string_view replacement) {
    for(std::size_t at=0;(at=code.find(key,at))!=code.npos;) {
        code.replace(at,key.size(),replacement);
        at+=replacement.size();
    }
}

} // namespace cpp_abstract_interface_detail

inline bool cpp_abstract_interface_matches(
        const CppAbstractInterfacePolicy& p,std::string_view candidate) {
    namespace d=cpp_abstract_interface_detail;
    if(!d::valid(p))return false;
    const auto old=d::spans(p,p.original_source,false);
    const auto edited=d::spans(p,candidate,true);
    if(!old||!edited)return false;
    // Compare every immutable interval byte-for-byte. No token-only
    // substitution can smuggle edits to interface purity, data or main.
    const std::string_view original=p.original_source;
    std::size_t prev_old=0,prev_new=0;
    for(std::size_t i=0;i<old->size();++i) {
        const auto& before=old->at(i);
        const auto& after=edited->at(i);
        if(original.substr(prev_old,before.begin-prev_old)!=
           candidate.substr(prev_new,after.begin-prev_new))return false;
        prev_old=before.end;
        prev_new=after.end;
    }
    return original.substr(prev_old)==candidate.substr(prev_new);
}

inline std::optional<std::string> cpp_abstract_interface_private_driver(
        const CppAbstractInterfacePolicy& p,std::string_view candidate) {
    if(!cpp_abstract_interface_matches(p,candidate))return {};
    constexpr std::string_view prelude=R"lgaed(#include <array>
#include <climits>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#define main lgaed_published_original_main
)lgaed";
    constexpr std::string_view driver=R"lgaed(
#undef main
class MuestraAdicional final : public @BASE@ {
public:
    MuestraAdicional(int n,int marker) : n_{n},marker_{marker} {}
    int @NUMBER@() const noexcept override {
        ++reads;
        return n_;
    }
    void @WRITE@(std::ostream& stream) const override {
        ++writes;
        stream << "Adicional:" << marker_;
    }
    mutable int reads=0,writes=0;
private:
    int n_,marker_;
};
int main() {
    const auto check=[](const @BASE@& object,std::string expected,
                        std::string detail)->bool {
        std::ostringstream report;
        object.@WRITE@(report);
        if(report.str()!=detail)return false;
        std::ostringstream complete;
        @PRESENT@(object,complete);
        return complete.str()==expected;
    };
    {
        constexpr std::array<int,5> numbers{0,-9,5,INT_MIN,INT_MAX};
        for(int n:numbers)for(int extra:{0,-3,17}) {
            auto item=@CREATE@(0,n,extra);
            if(!item||!dynamic_cast<@SIMPLE@*>(item.get())||
               dynamic_cast<@IDENTIFIED@*>(item.get())||
               item->@NUMBER@()!=n)return 1;
            const auto detail="Numero:"+std::to_string(n);
            const auto expected="Numero:"+std::to_string(n)+
                "\nDetalle:"+detail+"\n";
            if(!check(*item,expected,detail))return 2;
            const @BASE@* identity=item.get();
            std::unique_ptr<@BASE@> transferred=std::move(item);
            if(item||transferred.get()!=identity)return 3;
            transferred.reset();
            if(transferred)return 4;
        }
    }
    {
        constexpr std::array<int,5> numbers{0,-4,12,INT_MIN,INT_MAX};
        constexpr std::array<int,5> codes{0,-3,41,INT_MAX,INT_MIN};
        for(size_t i=0;i<numbers.size();++i) {
            const int n=numbers[i],code=codes[i];
            auto item=@CREATE@(1,n,code);
            auto* actual=dynamic_cast<@IDENTIFIED@*>(item.get());
            if(!item||!actual||dynamic_cast<@SIMPLE@*>(item.get())||
               item->@NUMBER@()!=n||actual->@CODE@()!=code)return 5;
            const auto detail="Numero:"+std::to_string(n)+
                ";Codigo:"+std::to_string(code);
            const auto expected="Numero:"+std::to_string(n)+
                "\nDetalle:"+detail+"\n";
            if(!check(*item,expected,detail))return 6;
            std::ostringstream second;
            @PRESENT@(*item,second);
            if(second.str()!=expected)return 7;
        }
    }
    {
        // Neither labels nor known subclasses identify this valid third
        // implementation. Correct code must consume both virtual operations.
        MuestraAdicional custom{-7,93};
        std::ostringstream report;
        @PRESENT@(custom,report);
        if(report.str()!="Numero:-7\nDetalle:Adicional:93\n"||
           custom.reads==0||custom.writes==0)return 8;
        std::ostringstream second;
        @PRESENT@(custom,second);
        if(second.str()!=report.str()||custom.reads<2||
           custom.writes<2)return 9;
    }
    {
        MuestraAdicional extreme{INT_MAX,0};
        std::ostringstream report;
        @PRESENT@(extreme,report);
        if(report.str()!="Numero:"+std::to_string(INT_MAX)+
           "\nDetalle:Adicional:0\n"||
           extreme.reads==0||extreme.writes==0)return 10;
    }
    std::fputs("LGAED_ABSTRACT_INTERFACE_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    std::string witness(driver);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,8>{{
        {"@BASE@",p.base_class},{"@SIMPLE@",p.simple_class},
        {"@IDENTIFIED@",p.identified_class},
        {"@CREATE@",p.create_function},{"@PRESENT@",p.present_function},
        {"@NUMBER@",p.number_method},{"@CODE@",p.code_method},
        {"@WRITE@",p.write_method}
    }})cpp_abstract_interface_detail::substitute(witness,key,value);
    return std::string(prelude)+std::string(candidate)+witness;
}

} // namespace lgaed::curriculum
