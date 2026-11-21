#pragma once

#include "core/cpp_multi_body_check.hpp"
#include "core/cpp_class_definition_instrumentation.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// A semantic, source-bound proof for a virtual base destructor, the derived
// destructor and a factory that returns a uniquely owned base pointer.
// All names are declared by the author, not guessed from a student's stdout.
struct CppPolymorphicDestructionPolicy {
    CppMultiBodyPolicy editable;
    std::string base_class, derived_class, cycle_class, owned_class, owned_member;
    std::string factory_function, write_method;
    std::string base_alive, derived_alive, owned_alive;
    std::string base_destroyed, derived_destroyed, owned_destroyed;
    bool operator==(const CppPolymorphicDestructionPolicy&) const = default;
};

inline constexpr std::string_view cpp_polymorphic_destruction_completion_marker =
    "LGAED_POLYMORPHIC_DESTRUCTION_V1";

namespace cpp_polymorphic_destruction_detail {

inline bool identifier(std::string_view value) {
    if(value.empty()||value.size()>96||value.starts_with("__")||
       (value.size()>1&&value[0]=='_'&&value[1]>='A'&&value[1]<='Z'))
        return false;
    const auto alpha=[](unsigned char c) {
        return c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z');
    };
    if(!alpha(static_cast<unsigned char>(value.front())))return false;
    return std::all_of(value.begin()+1,value.end(),[&](unsigned char c) {
        return alpha(c)||(c>='0'&&c<='9');
    });
}
inline bool valid(const CppPolymorphicDestructionPolicy& p) {
    for(std::string_view name:{
            std::string_view(p.base_class),std::string_view(p.derived_class),
            std::string_view(p.cycle_class),std::string_view(p.owned_class),
            std::string_view(p.owned_member),std::string_view(p.factory_function),
            std::string_view(p.write_method),std::string_view(p.base_alive),
            std::string_view(p.derived_alive),std::string_view(p.owned_alive),
            std::string_view(p.base_destroyed),std::string_view(p.derived_destroyed),
            std::string_view(p.owned_destroyed)})
        if(!identifier(name))return false;
    if(p.editable.bodies.size()!=3||!p.editable.bodies[0].qualified||
       !p.editable.bodies[1].qualified||p.editable.bodies[2].qualified||
       !cpp_multi_body_matches(p.editable,p.editable.original_source))
        return false;
    const auto& source=p.editable.original_source;
    const auto& bodies=p.editable.bodies;
    const auto base=p.base_class+"::~"+p.base_class+"() noexcept";
    const auto derived=p.derived_class+"::~"+p.derived_class+"() noexcept";
    if(bodies[0].signature!=base||bodies[1].signature!=derived||
       bodies[2].signature.find("std::unique_ptr<"+p.base_class+"> "+
          p.factory_function+"(")!=0)
        return false;
    // Require actual C++ declarations: text in comments or raw literals
    // must not invent a Ficha owner, close Ciclo early, or supply its fields.
    if(!cpp_class_definition_instrumentation::body_insertion(source,p.owned_class))
        return false;
    const auto scanned=code_format::cpp_tokens(source);
    if(!scanned)return false;
    std::vector<std::string> code;
    code.reserve(scanned->size());
    for(const auto& token:*scanned)
        if(!token.starts_with("comment:"))code.push_back(token);
    const auto contains=[&](std::initializer_list<std::string> pattern) {
        return std::search(code.begin(),code.end(),
            pattern.begin(),pattern.end())!=code.end();
    };
    std::optional<std::pair<std::size_t,std::size_t>> cycle;
    for(std::size_t i=0;i+2<code.size();++i) {
        if(code[i]!="id:struct"||code[i+1]!="id:"+p.cycle_class||
           code[i+2]!="op:{")continue;
        if(cycle)return false; // The author cannot pick among two owners.
        std::size_t depth=1;
        for(std::size_t j=i+3;j<code.size();++j) {
            if(code[j]=="op:{")++depth;
            else if(code[j]=="op:}"&&--depth==0) {
                if(j+1>=code.size()||code[j+1]!="op:;")return false;
                cycle=std::pair{i+2,j};
                break;
            }
        }
        if(!cycle)return false;
    }
    if(!cycle)return false;
    for(const std::string_view field:{
            std::string_view(p.base_alive),std::string_view(p.derived_alive),
            std::string_view(p.owned_alive),std::string_view(p.base_destroyed),
            std::string_view(p.derived_destroyed),
            std::string_view(p.owned_destroyed)}) {
        bool found=false;
        std::size_t depth=1;
        for(std::size_t j=cycle->first+1;j<cycle->second;++j) {
            if(depth==1&&j+2<cycle->second&&code[j]=="id:int"&&
               code[j+1]=="id:"+std::string(field)&&
               (code[j+2]=="op:="||code[j+2]=="op:;"||
                code[j+2]=="op:{")) {
                if(found)return false;
                found=true;
            }
            if(code[j]=="op:{")++depth;
            else if(code[j]=="op:}")--depth;
        }
        if(!found)return false;
    }
    // These are protected original source declarations, not an inferred
    // runtime result. The private native probe still checks real semantics.
    return contains({"id:void","id:"+p.write_method,"op:("})&&
        contains({"id:virtual","op:~","id:"+p.base_class,
                  "op:(","op:)","id:noexcept","op:;"})&&
        contains({"op:~","id:"+p.derived_class,"op:(","op:)",
                  "id:noexcept","id:override","op:;"})&&
        contains({"id:std","op:::","id:unique_ptr","op:<",
                  "id:"+p.owned_class,"op:>","id:"+p.owned_member,"op:;"});
}
inline void substitute(std::string& source,std::string_view key,
                       std::string_view replacement) {
    for(std::size_t at=0;(at=source.find(key,at))!=source.npos;) {
        source.replace(at,key.size(),replacement);
        at+=replacement.size();
    }
}

} // namespace cpp_polymorphic_destruction_detail

inline bool cpp_polymorphic_destruction_matches(
        const CppPolymorphicDestructionPolicy& p,std::string_view candidate) {
    return cpp_polymorphic_destruction_detail::valid(p)&&
           cpp_multi_body_matches(p.editable,candidate);
}

inline std::optional<std::string> cpp_polymorphic_destruction_private_driver(
        const CppPolymorphicDestructionPolicy& p,std::string_view candidate) {
    if(!cpp_polymorphic_destruction_matches(p,candidate))return {};
    constexpr std::string_view prelude=R"lgaed(#include <cstdio>
#include <memory>
#include <sstream>
#include <utility>
#define main lgaed_published_original_main
)lgaed";
    constexpr std::string_view driver=R"lgaed(
#undef main
int main() {
    const auto state=[](const @CYCLE@& value,int base,int derived,int owned,
            int dead_base,int dead_derived,int dead_owned)->bool {
        return value.@BASE_ALIVE@==base&&
            value.@DERIVED_ALIVE@==derived&&
            value.@OWNED_ALIVE@==owned&&
            value.@BASE_DEAD@==dead_base&&
            value.@DERIVED_DEAD@==dead_derived&&
            value.@OWNED_DEAD@==dead_owned;
    };
    {
        @CYCLE@ cycle;
        auto source=@FACTORY@(0,-3,cycle);
        if(!source||!dynamic_cast<@DERIVED@*>(source.get())||
           !state(cycle,1,1,1,0,0,0))return 1;
        std::ostringstream report;
        source->@WRITE@(report);
        if(report.str()!="Numero:0;Codigo:-3")return 2;
        const @BASE@* identity=source.get();
        std::unique_ptr<@BASE@> destination=std::move(source);
        if(source||destination.get()!=identity||
           !state(cycle,1,1,1,0,0,0))return 3;
        destination.reset();
        if(!state(cycle,0,0,0,1,1,1))return 4;
        destination.reset();
        if(!state(cycle,0,0,0,1,1,1))return 5;
    }
    {
        @CYCLE@ cycle;
        // Preexisting counts distinguish increments from forced constants.
        cycle.@BASE_ALIVE@=5;cycle.@DERIVED_ALIVE@=2;
        cycle.@OWNED_ALIVE@=7;cycle.@BASE_DEAD@=11;
        cycle.@DERIVED_DEAD@=13;cycle.@OWNED_DEAD@=17;
        auto first=@FACTORY@(7,0,cycle);
        auto second=@FACTORY@(-7,19,cycle);
        if(!first||!second||first.get()==second.get()||
           !dynamic_cast<@DERIVED@*>(first.get())||
           !dynamic_cast<@DERIVED@*>(second.get())||
           !state(cycle,7,4,9,11,13,17))return 6;
        std::ostringstream a,b;
        first->@WRITE@(a);
        second->@WRITE@(b);
        if(a.str()!="Numero:7;Codigo:0"||
           b.str()!="Numero:-7;Codigo:19")return 7;
        first.reset();
        if(!state(cycle,6,3,8,12,14,18))return 8;
        second.reset();
        if(!state(cycle,5,2,7,13,15,19))return 9;
    }
    {
        @CYCLE@ first,second;
        auto a=@FACTORY@(-5,33,first);
        auto b=@FACTORY@(14,0,second);
        if(!state(first,1,1,1,0,0,0)||
           !state(second,1,1,1,0,0,0))return 10;
        b.reset();
        if(!state(first,1,1,1,0,0,0)||
           !state(second,0,0,0,1,1,1))return 11;
        a.reset();
        if(!state(first,0,0,0,1,1,1))return 12;
    }
    {
        // RAII also applies when the caller exits via a thrown exception.
        @CYCLE@ cycle;
        bool caught=false;
        try {
            auto value=@FACTORY@(-12,28,cycle);
            if(!value||!dynamic_cast<@DERIVED@*>(value.get())||
               !state(cycle,1,1,1,0,0,0))return 13;
            throw 31;
        }catch(int value){caught=value==31;}
        if(!caught||!state(cycle,0,0,0,1,1,1))return 14;
    }
    std::fputs("LGAED_POLYMORPHIC_DESTRUCTION_V1",stderr);
    return 0;
}
)lgaed";
    std::string witness(driver);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,11>{{
        {"@BASE@",p.base_class},{"@DERIVED@",p.derived_class},
        {"@CYCLE@",p.cycle_class},{"@FACTORY@",p.factory_function},
        {"@WRITE@",p.write_method},
        {"@BASE_ALIVE@",p.base_alive},{"@DERIVED_ALIVE@",p.derived_alive},
        {"@OWNED_ALIVE@",p.owned_alive},
        {"@BASE_DEAD@",p.base_destroyed},
        {"@DERIVED_DEAD@",p.derived_destroyed},
        {"@OWNED_DEAD@",p.owned_destroyed}
    }})cpp_polymorphic_destruction_detail::substitute(witness,key,value);
    return std::string(prelude)+std::string(candidate)+witness;
}

} // namespace lgaed::curriculum
