#pragma once

#include "core/cpp_multi_body_check.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace lgaed::curriculum {

// Protects the complete published program while permitting only the two
// original bodies. Semantic dispatch is proved by a separately compiled
// C++ probe, not by the transcript or a syntactic body matcher.
struct CppVirtualLifetimeDispatchPolicy {
    CppMultiBodyPolicy editable;
    std::string base_class, derived_class;
    std::string register_method, write_method;
    std::string phase_parameter, register_stream, write_stream;
    bool operator==(const CppVirtualLifetimeDispatchPolicy&) const = default;
};

inline constexpr std::string_view cpp_virtual_lifetime_dispatch_marker =
    "LGAED_VIRTUAL_LIFETIME_DISPATCH_V1";

namespace cpp_virtual_lifetime_detail {
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
inline bool valid(const CppVirtualLifetimeDispatchPolicy& p) {
    for(std::string_view name:{std::string_view(p.base_class),
            std::string_view(p.derived_class),
            std::string_view(p.register_method),std::string_view(p.write_method),
            std::string_view(p.phase_parameter),
            std::string_view(p.register_stream),std::string_view(p.write_stream)})
        if(!identifier(name))return false;
    if(p.editable.bodies.size()!=2||
       !p.editable.bodies[0].qualified||!p.editable.bodies[1].qualified||
       !cpp_multi_body_matches(p.editable,p.editable.original_source))
        return false;
    const auto& source=p.editable.original_source;
    const auto signature_a="void "+p.base_class+"::"+p.register_method+
        "(const char* "+p.phase_parameter+", std::ostream& "+
        p.register_stream+") const";
    const auto signature_b="void "+p.derived_class+"::"+p.write_method+
        "(std::ostream& "+p.write_stream+") const";
    return p.editable.bodies[0].signature==signature_a&&
        p.editable.bodies[1].signature==signature_b&&
        source.find("class "+p.base_class+" {")!=std::string::npos&&
        source.find("class "+p.derived_class+" : public "+p.base_class+" {")!=
            std::string::npos&&
        source.find("virtual ~"+p.base_class+"()")!=std::string::npos&&
        source.find("virtual void "+p.write_method+"(std::ostream&")!=
            std::string::npos&&
        source.find("void "+p.write_method+"(std::ostream& "+
                    p.write_stream+") const override;")!=std::string::npos;
}
inline void substitute(std::string& source,std::string_view key,
                       std::string_view replacement) {
    for(std::size_t at=0;(at=source.find(key,at))!=source.npos;) {
        source.replace(at,key.size(),replacement);
        at+=replacement.size();
    }
}
} // namespace cpp_virtual_lifetime_detail

inline bool cpp_virtual_lifetime_dispatch_matches(
        const CppVirtualLifetimeDispatchPolicy& p,
        std::string_view candidate) {
    return cpp_virtual_lifetime_detail::valid(p)&&
           cpp_multi_body_matches(p.editable,candidate);
}

inline std::optional<std::string> cpp_virtual_lifetime_dispatch_private_driver(
        const CppVirtualLifetimeDispatchPolicy& p,
        std::string_view candidate) {
    if(!cpp_virtual_lifetime_dispatch_matches(p,candidate))return {};
    static constexpr std::string_view prelude=R"lgaed(#include <cstdio>
#include <string_view>
#define main lgaed_published_original_main
)lgaed";
    static constexpr std::string_view driver=R"lgaed(
#undef main
int main() {
    const auto same=[](const std::ostringstream& output,
                       std::string_view expected)->bool {
        return output.str() == expected;
    };
    {
        std::ostringstream history;
        {
            @DERIVED@ value{0,-3,history};
            if(!same(history,
                "ConstruirBase:Numero:0\n"
                "ConstruirDerivada:Numero:0;Codigo:-3\n"))return 1;
            const @BASE@& view=value;

            // These are *ordinary use* calls on a live derived object.
            // Their text resembles the base constructor/destructor phases.
            // A label-dependent dispatch hack must not force base here.
            std::ostringstream during_base_label;
            view.@REGISTER@("ConstruirBase",during_base_label);
            if(!same(during_base_label,
                "ConstruirBase:Numero:0;Codigo:-3\n"))return 2;
            std::ostringstream during_death_label;
            view.@REGISTER@("DestruirBase",during_death_label);
            if(!same(during_death_label,
                "DestruirBase:Numero:0;Codigo:-3\n"))return 3;
            std::ostringstream unfamiliar;
            view.@REGISTER@("Otra fase",unfamiliar);
            if(!same(unfamiliar,"Otra fase:Numero:0;Codigo:-3\n"))return 4;
            std::ostringstream empty_label;
            view.@REGISTER@("",empty_label);
            if(!same(empty_label,":Numero:0;Codigo:-3\n"))return 5;

            // A requested stream is not the constructor's history stream.
            std::ostringstream report;
            view.@WRITE@(report);
            if(!same(report,"Numero:0;Codigo:-3"))return 6;
            std::ostringstream explicit_base;
            view.@BASE@::@WRITE@(explicit_base);
            if(!same(explicit_base,"Numero:0"))return 7;
            if(!same(history,
                "ConstruirBase:Numero:0\n"
                "ConstruirDerivada:Numero:0;Codigo:-3\n"))return 8;
        }
        if(!same(history,
            "ConstruirBase:Numero:0\n"
            "ConstruirDerivada:Numero:0;Codigo:-3\n"
            "DestruirDerivada:Numero:0;Codigo:-3\n"
            "DestruirBase:Numero:0\n"))return 9;
    }
    {
        // A plain base object must *remain* base even if the supplied
        // string resembles a derived phase.
        std::ostringstream history;
        {
            @BASE@ base{-7,history};
            if(!same(history,"ConstruirBase:Numero:-7\n"))return 10;
            std::ostringstream external;
            base.@REGISTER@("ConstruirDerivada",external);
            if(!same(external,"ConstruirDerivada:Numero:-7\n"))return 11;
            std::ostringstream other;
            base.@REGISTER@("DestruirDerivada",other);
            if(!same(other,"DestruirDerivada:Numero:-7\n"))return 12;
        }
        if(!same(history,
            "ConstruirBase:Numero:-7\n"
            "DestruirBase:Numero:-7\n"))return 13;
    }
    {
        // The original lifetime phases are still genuine for two other
        // codes and for reset through a virtual base owner.
        std::ostringstream history;
        {
            std::unique_ptr<@BASE@> instance =
                std::make_unique<@DERIVED@>(19,0,history);
            std::ostringstream report;
            instance->@REGISTER@("Uso",report);
            if(!same(report,"Uso:Numero:19;Codigo:0\n"))return 14;
            instance.reset();
        }
        if(!same(history,
            "ConstruirBase:Numero:19\n"
            "ConstruirDerivada:Numero:19;Codigo:0\n"
            "DestruirDerivada:Numero:19;Codigo:0\n"
            "DestruirBase:Numero:19\n"))return 15;
    }
    {
        std::ostringstream history;
        {
            @DERIVED@ value{-11,23,history};
            const @BASE@& view=value;
            std::ostringstream result;
            view.@REGISTER@("DestruirDerivada",result);
            if(!same(result,"DestruirDerivada:Numero:-11;Codigo:23\n"))
                return 16;
        }
        if(!same(history,
            "ConstruirBase:Numero:-11\n"
            "ConstruirDerivada:Numero:-11;Codigo:23\n"
            "DestruirDerivada:Numero:-11;Codigo:23\n"
            "DestruirBase:Numero:-11\n"))return 17;
    }
    std::fputs("LGAED_VIRTUAL_LIFETIME_DISPATCH_V1",stderr);
    return 0;
}
)lgaed";
    auto witness=std::string(driver);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,4>{{
        {"@BASE@",p.base_class},{"@DERIVED@",p.derived_class},
        {"@REGISTER@",p.register_method},{"@WRITE@",p.write_method}
    }}) cpp_virtual_lifetime_detail::substitute(witness,key,value);
    return std::string(prelude)+std::string(candidate)+witness;
}

} // namespace lgaed::curriculum
