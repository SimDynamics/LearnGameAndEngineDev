#pragma once

#include "core/cpp_multi_body_check.hpp"
#include "core/cpp_class_structure_check.hpp"

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
    // Protect *global* curricular owners and direct member declarations.
    // A same-named type inside a function or namespace is unrelated: it
    // cannot supply Ciclo's counters, Ficha, a destructor or ficha_.
    const auto code=cpp_class_structure::lexical(source);
    if(!code)return false;
    const auto cycle=cpp_class_structure::defined_class(*code,p.cycle_class);
    const auto owned=cpp_class_structure::defined_class(*code,p.owned_class);
    const auto base_class=cpp_class_structure::defined_class(*code,p.base_class);
    const auto derived_class=cpp_class_structure::defined_class(
        *code,p.derived_class,p.base_class);
    if(!cycle||!owned||!base_class||!derived_class)return false;
    const auto has=cpp_class_structure::direct_contains;
    return cpp_class_structure::direct_int_fields(*code,*cycle,{
            p.base_alive,p.derived_alive,p.owned_alive,
            p.base_destroyed,p.derived_destroyed,p.owned_destroyed})&&
        has(*code,*base_class,{
            "id:virtual","op:~","id:"+p.base_class,
            "op:(","op:)","id:noexcept","op:;"})&&
        has(*code,*derived_class,{
            "op:~","id:"+p.derived_class,"op:(","op:)",
            "id:noexcept","id:override","op:;"})&&
        has(*code,*derived_class,{
            "id:std","op:::","id:unique_ptr","op:<",
            "id:"+p.owned_class,"op:>","id:"+p.owned_member,"op:;"})&&
        has(*code,*base_class,{
            "id:virtual","id:void","id:"+p.write_method,"op:(",
            "id:std","op:::","id:ostream","op:&"});
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
