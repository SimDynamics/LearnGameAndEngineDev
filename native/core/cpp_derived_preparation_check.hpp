#pragma once

#include "core/cpp_multi_body_check.hpp"
#include "core/cpp_class_structure_check.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lgaed::curriculum {

// A source-bound, author-declared one-body property: prepare a complete
// derived owner before replacing a base owner. A compiled private program
// verifies lifetime, identity and exception safety, never stdout alone.
struct CppDerivedPreparationPolicy {
    CppMultiBodyPolicy editable;
    std::string base_class, derived_class, balance_class, resource_class;
    std::string replacement_function;
    std::string destination_parameter, number_parameter, code_parameter;
    std::string mode_parameter, balance_parameter, print_method;
    std::string resource_alive, base_complete, derived_complete, resource_complete;
    std::string base_destroyed, derived_destroyed, resource_destroyed;
    bool operator==(const CppDerivedPreparationPolicy&) const = default;
};

inline constexpr std::string_view cpp_derived_preparation_completion_marker =
    "LGAED_DERIVED_PREPARATION_COMPLETE_V1";

namespace cpp_derived_preparation_detail {
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
inline bool valid(const CppDerivedPreparationPolicy& p) {
    for(std::string_view name:{
            std::string_view(p.base_class),std::string_view(p.derived_class),
            std::string_view(p.balance_class),std::string_view(p.resource_class),
            std::string_view(p.replacement_function),
            std::string_view(p.destination_parameter),
            std::string_view(p.number_parameter),std::string_view(p.code_parameter),
            std::string_view(p.mode_parameter),std::string_view(p.balance_parameter),
            std::string_view(p.print_method),std::string_view(p.resource_alive),
            std::string_view(p.base_complete),std::string_view(p.derived_complete),
            std::string_view(p.resource_complete),std::string_view(p.base_destroyed),
            std::string_view(p.derived_destroyed),
            std::string_view(p.resource_destroyed)})
        if(!identifier(name))return false;
    if(p.editable.bodies.size()!=1||p.editable.bodies[0].qualified||
       !cpp_multi_body_matches(p.editable,p.editable.original_source))
        return false;
    const auto signature="void "+p.replacement_function+
        "(std::unique_ptr<"+p.base_class+">& "+p.destination_parameter+
        ", int "+p.number_parameter+", int "+p.code_parameter+
        ", int "+p.mode_parameter+", "+p.balance_class+"& "+
        p.balance_parameter+")";
    if(p.editable.bodies[0].signature!=signature)return false;
    const auto code=cpp_class_structure::lexical(p.editable.original_source);
    if(!code)return false;
    const auto base=cpp_class_structure::defined_class(*code,p.base_class);
    const auto derived=cpp_class_structure::defined_class(
        *code,p.derived_class,p.base_class);
    const auto resource=cpp_class_structure::defined_class(*code,p.resource_class);
    const auto balance=cpp_class_structure::defined_class(*code,p.balance_class);
    if(!base||!derived||!resource||!balance||
       !cpp_class_structure::direct_int_fields(*code,*balance,{
           p.resource_alive,p.base_complete,p.derived_complete,p.resource_complete,
           p.base_destroyed,p.derived_destroyed,p.resource_destroyed}))
        return false;
    // Bind declarations to their real protected classes. The separately
    // compiled witness still proves survival and exceptional cleanup.
    return cpp_class_structure::direct_contains(*code,*base,{
            "id:virtual","op:~","id:"+p.base_class,
            "op:(","op:)","id:noexcept"})&&
        cpp_class_structure::direct_contains(*code,*derived,{
            "op:~","id:"+p.derived_class,"op:(","op:)",
            "id:noexcept","id:override"})&&
        cpp_class_structure::direct_contains(*code,*base,{
            "id:std","op:::","id:unique_ptr","op:<",
            "id:"+p.resource_class,"op:>"})&&
        cpp_class_structure::direct_contains(*code,*base,{
            "id:virtual","id:void","id:"+p.print_method,
            "op:(","id:std","op:::","id:ostream","op:&"});
}
inline void substitute(std::string& code,std::string_view key,
                       std::string_view value) {
    for(std::size_t at=0;(at=code.find(key,at))!=code.npos;) {
        code.replace(at,key.size(),value);
        at+=value.size();
    }
}
} // namespace cpp_derived_preparation_detail

inline bool cpp_derived_preparation_matches(
        const CppDerivedPreparationPolicy& p,std::string_view candidate) {
    return cpp_derived_preparation_detail::valid(p)&&
           cpp_multi_body_matches(p.editable,candidate);
}

inline std::optional<std::string> cpp_derived_preparation_private_driver(
        const CppDerivedPreparationPolicy& p,std::string_view candidate) {
    if(!cpp_derived_preparation_matches(p,candidate))return {};
    constexpr std::string_view prelude=R"lgaed(#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#define main lgaed_published_original_main
)lgaed";
    constexpr std::string_view driver=R"lgaed(
#undef main
int main() {
    const auto state=[](const @BALANCE@& b,int live,int base_complete,
            int derived_complete,int resource_complete,int base_destroyed,
            int derived_destroyed,int resource_destroyed)->bool {
        return b.@RESOURCE_ALIVE@==live&&
            b.@BASE_COMPLETE@==base_complete&&
            b.@DERIVED_COMPLETE@==derived_complete&&
            b.@RESOURCE_COMPLETE@==resource_complete&&
            b.@BASE_DESTROYED@==base_destroyed&&
            b.@DERIVED_DESTROYED@==derived_destroyed&&
            b.@RESOURCE_DESTROYED@==resource_destroyed;
    };
    const auto report=[](const std::unique_ptr<@BASE@>& value,
                         int number,int code)->bool {
        if(!value||!dynamic_cast<@DERIVED@*>(value.get()))
            return false;
        std::ostringstream output;
        value->@PRINT@(output);
        return output.str()=="Numero:"+std::to_string(number)+
            ";Codigo:"+std::to_string(code);
    };

    // Each failing constructor stage must leave the same previously owned
    // object alive and let the original Interrupcion propagate unchanged.
    for(bool occupied:{false,true})for(int failure:{1,2,3}) {
        @BALANCE@ balance;
        std::unique_ptr<@BASE@> destination;
        if(occupied)
            destination=std::make_unique<@DERIVED@>(5,-3,0,balance);
        const auto* identity=destination.get();
        int stage=0;
        try {
            @REPLACE@(destination,-7,0,failure,balance);
        }catch(const Interrupcion& thrown) {stage=thrown.etapa;}
        catch(...) {return 1;}
        if(stage!=failure||destination.get()!=identity||
           (occupied&&!report(destination,5,-3))||
           (!occupied&&destination))return 2;
        const int old=occupied?1:0;
        const int new_base=failure>=2?1:0;
        const int new_resource=failure==3?2:1;
        if(!state(balance,occupied?2:0,old+new_base,old,
                  2*old+new_resource,new_base,0,new_resource))
            return 3;
        destination.reset();
        if(!state(balance,0,old+new_base,old,
                  2*old+new_resource,new_base+old,old,
                  new_resource+2*old))return 4;
    }

    // A successful replacement must install a genuinely different derived
    // object. The prior destructor chain completes exactly once.
    for(bool occupied:{false,true}) {
        @BALANCE@ balance;
        std::unique_ptr<@BASE@> destination;
        if(occupied)
            destination=std::make_unique<@DERIVED@>(5,-3,0,balance);
        const auto* former=destination.get();
        @REPLACE@(destination,0,-2,0,balance);
        if(!report(destination,0,-2)||destination.get()==former||
           !state(balance,2,(occupied?2:1),(occupied?2:1),
                  (occupied?4:2),(occupied?1:0),
                  (occupied?1:0),(occupied?2:0)))return 5;
        destination.reset();
        const int completed=occupied?2:1;
        if(!state(balance,0,completed,completed,completed*2,
                  completed,completed,completed*2))return 6;
    }

    // Matching values do not authorize reusing the previous object.
    {
        @BALANCE@ balance;
        std::unique_ptr<@BASE@> destination=
            std::make_unique<@DERIVED@>(-11,23,0,balance);
        const auto* former=destination.get();
        @REPLACE@(destination,-11,23,0,balance);
        if(destination.get()==former||!report(destination,-11,23)||
           !state(balance,2,2,2,4,1,1,2))return 7;
        destination.reset();
        if(!state(balance,0,2,2,4,2,2,4))return 8;
    }

    // The previous and replacement objects may belong to different
    // @BALANCE@ records. A candidate must use only the supplied one.
    {
        @BALANCE@ old_balance,new_balance;
        std::unique_ptr<@BASE@> destination=
            std::make_unique<@DERIVED@>(1,2,0,old_balance);
        const auto* former=destination.get();
        @REPLACE@(destination,-8,9,0,new_balance);
        if(!report(destination,-8,9)||destination.get()==former||
           !state(old_balance,0,1,1,2,1,1,2)||
           !state(new_balance,2,1,1,2,0,0,0))return 9;
        destination.reset();
        if(!state(new_balance,0,1,1,2,1,1,2))return 10;
    }

    // An unrelated caller exception must also clean the accepted owner by
    // normal RAII, without user code explicitly calling the destructor.
    {
        @BALANCE@ balance;
        bool caught=false;
        try {
            std::unique_ptr<@BASE@> destination;
            @REPLACE@(destination,4,0,0,balance);
            if(!report(destination,4,0))return 11;
            throw 31;
        }catch(int value) {caught=value==31;}
        if(!caught||!state(balance,0,1,1,2,1,1,2))return 12;
    }
    std::fputs("LGAED_DERIVED_PREPARATION_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    auto witness=std::string(driver);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,12>{{
        {"@BASE@",p.base_class},{"@DERIVED@",p.derived_class},
        {"@BALANCE@",p.balance_class},{"@REPLACE@",p.replacement_function},
        {"@PRINT@",p.print_method},
        {"@RESOURCE_ALIVE@",p.resource_alive},
        {"@BASE_COMPLETE@",p.base_complete},
        {"@DERIVED_COMPLETE@",p.derived_complete},
        {"@RESOURCE_COMPLETE@",p.resource_complete},
        {"@BASE_DESTROYED@",p.base_destroyed},
        {"@DERIVED_DESTROYED@",p.derived_destroyed},
        {"@RESOURCE_DESTROYED@",p.resource_destroyed}
    }})cpp_derived_preparation_detail::substitute(witness,key,value);
    return std::string(prelude)+std::string(candidate)+witness;
}

} // namespace lgaed::curriculum
