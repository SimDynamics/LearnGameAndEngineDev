#pragma once

#include "core/cpp_multi_body_check.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

// Inline methods have identical signatures in the two concrete classes.
// Discover them by their *original owning class*, not by the first matching
// signature in the file or by a string such as "Pendiente".
inline std::optional<CppMemberBodySpan> inline_body(
        std::string_view code,std::string_view class_name,
        std::string_view base_name,std::string_view signature) {
    const auto declaration="class "+std::string(class_name)+
        " : public "+std::string(base_name)+" {";
    const auto class_at=code.find(declaration);
    if(class_at==code.npos||code.find(declaration,class_at+1)!=code.npos)
        return {};
    const auto opening=class_at+declaration.size()-1;
    const auto closing=cpp_member_body_detail::closing_brace(code,opening);
    if(!closing)return {};
    const auto at=code.find(signature,opening+1);
    if(at==code.npos||at>=*closing)return {};
    const auto repeated=code.find(signature,at+1);
    if(repeated!=code.npos&&repeated<*closing)return {};
    const auto line_break=code.rfind('\n',at);
    const auto begin_line=line_break==code.npos?0:line_break+1;
    for(auto i=begin_line;i<at;++i)
        if(!cpp_member_body_detail::horizontal_space(code[i]))return {};
    auto method_open=at+signature.size();
    while(method_open<code.size()&&
            (cpp_member_body_detail::horizontal_space(code[method_open])||
             code[method_open]=='\n'||code[method_open]=='\r'))
        ++method_open;
    if(method_open>=*closing||code[method_open]!='{')return {};
    const auto method_end=
        cpp_member_body_detail::closing_brace(code,method_open);
    if(!method_end||*method_end>=*closing)return {};
    return CppMemberBodySpan{method_open+1,*method_end};
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
    const auto original=std::string_view(source);
    // A real public abstract interface is a prerequisite, not a new student
    // editing freedom. The matched bodies cannot change these declarations.
    const auto base="class "+p.base_class+" {";
    return original.find(base)!=original.npos&&
        original.find("virtual ~"+p.base_class+"() = default;")!=original.npos&&
        original.find("virtual int "+p.number_method+
                      "() const noexcept = 0;")!=original.npos&&
        original.find("virtual void "+p.write_method+
                      "(std::ostream& "+p.stream_parameter+
                      ") const = 0;")!=original.npos&&
        original.find("int "+p.number_method+"() const noexcept override")!=
            original.npos&&
        original.find("int "+p.code_method+"() const noexcept")!=
            original.npos;
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
    }})d::substitute(witness,key,value);
    return std::string(prelude)+std::string(candidate)+witness;
}

} // namespace lgaed::curriculum
