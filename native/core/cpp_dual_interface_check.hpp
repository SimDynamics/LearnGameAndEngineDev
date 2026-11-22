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

// Authored contract for one concrete object implementing two independent,
// public abstract bases. The only editable spans are its two existing method
// bodies. Interfaces, class declaration, data, constructor, mutation, the
// supplied consumer and main are compared byte-for-byte with the original.
struct CppDualInterfacePolicy {
    std::string original_source;
    std::string value_interface, report_interface, concrete_class;
    std::string value_method, write_method, change_method;
    std::string stream_parameter, change_parameter;
    std::string code_member, value_member;
    std::string constructor_code_parameter, constructor_value_parameter;
    std::string report_separator;
    bool operator==(const CppDualInterfacePolicy&) const = default;
};

inline constexpr std::string_view cpp_dual_interface_completion_marker =
    "LGAED_DUAL_INTERFACE_COMPLETE_V1";

namespace cpp_dual_interface_detail {
inline bool identifier(std::string_view text) {
    if(text.empty() || text.size()>96 || text.starts_with("__") ||
       (text.size()>1&&text[0]=='_'&&text[1]>='A'&&text[1]<='Z'))
        return false;
    const auto alpha=[](unsigned char c) {
        return c=='_'||(c>='a'&&c<='z')||(c>='A'&&c<='Z');
    };
    if(!alpha(static_cast<unsigned char>(text.front())))return false;
    return std::all_of(text.begin()+1,text.end(),[&](unsigned char c) {
        return alpha(c)||(c>='0'&&c<='9');
    });
}
inline bool separator(std::string_view value) {
    return !value.empty() && value.size()<=32 &&
        std::all_of(value.begin(),value.end(),[](unsigned char c) {
            return c>=32&&c<=126&&c!='"'&&c!='\\';
        });
}
inline std::vector<CppEditableBody> bodies(const CppDualInterfacePolicy& p) {
    return {
        {"int "+p.value_method+"() const override",false},
        {"void "+p.write_method+"(std::ostream& "+p.stream_parameter+
             ") const override",false}
    };
}
inline bool valid(const CppDualInterfacePolicy& p) {
    for(std::string_view name:{
            std::string_view(p.value_interface),
            std::string_view(p.report_interface),
            std::string_view(p.concrete_class),
            std::string_view(p.value_method),
            std::string_view(p.write_method),
            std::string_view(p.change_method),
            std::string_view(p.stream_parameter),
            std::string_view(p.change_parameter),
            std::string_view(p.code_member),
            std::string_view(p.value_member),
            std::string_view(p.constructor_code_parameter),
            std::string_view(p.constructor_value_parameter)})
        if(!identifier(name))return false;
    if(!separator(p.report_separator) ||
       p.value_interface==p.report_interface ||
       p.code_member==p.value_member)return false;
    const auto& source=p.original_source;
    if(!cpp_multi_body_detail::spans(source,bodies(p),false))
        return false;
    const auto code=cpp_class_structure::lexical(source);
    if(!code)return false;
    const auto value=cpp_class_structure::defined_class(*code,p.value_interface);
    const auto report=cpp_class_structure::defined_class(*code,p.report_interface);
    const auto concrete=cpp_class_structure::defined_public_bases(
        *code,p.concrete_class,{p.value_interface,p.report_interface});
    if(!value||!report||!concrete)return false;
    const auto has=cpp_class_structure::direct_contains;
    // Both abstract interfaces and both public bases must be actual C++.
    // A copied phrase in a comment or raw literal cannot supply an owner.
    return has(*code,*value,{
            "id:virtual","op:~","id:"+p.value_interface,
            "op:(","op:)","op:=","id:default","op:;"})&&
        has(*code,*report,{
            "id:virtual","op:~","id:"+p.report_interface,
            "op:(","op:)","op:=","id:default","op:;"})&&
        has(*code,*value,{
            "id:virtual","id:int","id:"+p.value_method,
            "op:(","op:)","id:const","op:=","number:0","op:;"})&&
        has(*code,*report,{
            "id:virtual","id:void","id:"+p.write_method,
            "op:(","id:std","op:::","id:ostream","op:&",
            "id:"+p.stream_parameter,"op:)","id:const",
            "op:=","number:0","op:;"})&&
        cpp_class_structure::direct_int_fields(
            *code,*concrete,{p.code_member,p.value_member})&&
        has(*code,*concrete,{
            "id:"+p.concrete_class,"op:(",
            "id:int","id:"+p.constructor_code_parameter,"op:,",
            "id:int","id:"+p.constructor_value_parameter,"op:)",
            "op::","id:"+p.code_member,"op:{",
            "id:"+p.constructor_code_parameter,"op:}","op:,",
            "id:"+p.value_member,"op:{",
            "id:"+p.constructor_value_parameter,"op:}","op:{"})&&
        has(*code,*concrete,{
            "id:void","id:"+p.change_method,"op:(",
            "id:int","id:"+p.change_parameter,"op:)","op:{",
            "id:"+p.value_member,"op:=",
            "id:"+p.change_parameter,"op:;","op:}"});
}
inline void substitute(std::string& text,std::string_view needle,
                       std::string_view replacement) {
    for(std::size_t pos=0;(pos=text.find(needle,pos))!=text.npos;) {
        text.replace(pos,needle.size(),replacement);
        pos+=replacement.size();
    }
}
} // namespace cpp_dual_interface_detail

inline bool cpp_dual_interface_matches(
        const CppDualInterfacePolicy& p,std::string_view candidate) {
    return cpp_dual_interface_detail::valid(p) &&
        cpp_multi_body_matches({p.original_source,
                                cpp_dual_interface_detail::bodies(p)},candidate);
}

// Recompile the actual submitted bodies with a private entry point. The
// checks cross-cast between the two bases of the same live object, verify
// current state across mutations, and redirect reports to independent streams.
// We never dereference a destroyed pointer or use user-owned saved state.
inline std::optional<std::string> cpp_dual_interface_private_driver(
        const CppDualInterfacePolicy& p,std::string_view candidate) {
    if(!cpp_dual_interface_matches(p,candidate))return {};
    constexpr std::string_view prelude=R"lgaed(#include <array>
#include <climits>
#include <cstdio>
#include <sstream>
#include <string>
#define main lgaed_published_original_main
)lgaed";
    constexpr std::string_view probe=R"lgaed(
#undef main
int main() {
    constexpr std::array<int,6> codes{0,7,-4,INT_MIN,INT_MAX,13};
    constexpr std::array<int,6> initial_values{0,3,-8,INT_MAX,INT_MIN,13};
    constexpr std::array<int,6> changed_values{-2,0,5,INT_MIN,INT_MAX,-8};
    for(int code:codes)for(int initial:initial_values) {
        @CONCRETE@ object{code,initial};
        const @VALUE_BASE@& value_view=object;
        const @REPORT_BASE@& report_view=object;
        if(dynamic_cast<const @CONCRETE@*>(&value_view)!=&object||
           dynamic_cast<const @CONCRETE@*>(&report_view)!=&object||
           dynamic_cast<const @REPORT_BASE@*>(&value_view)!=&report_view||
           dynamic_cast<const @VALUE_BASE@*>(&report_view)!=&value_view)
            return 1;
        if(value_view.@VALUE_METHOD@()!=initial)return 2;
        const auto verify=[&](int current)->bool {
            if(value_view.@VALUE_METHOD@()!=current)return false;
            const std::string expected=std::to_string(code)+"@SEPARATOR@"+
                                       std::to_string(current);
            std::ostringstream first,second;
            report_view.@WRITE_METHOD@(first);
            report_view.@WRITE_METHOD@(second);
            if(first.str()!=expected||second.str()!=expected)return false;
            report_view.@WRITE_METHOD@(first);
            if(first.str()!=expected+expected)return false;
            // Writing through the reporting interface must not change state.
            return value_view.@VALUE_METHOD@()==current;
        };
        if(!verify(initial))return 3;
        for(int next:changed_values) {
            object.@CHANGE_METHOD@(next);
            if(!verify(next))return 4;
        }
        // Repeated writes after equal values and a final reset are independent.
        object.@CHANGE_METHOD@(initial);
        if(!verify(initial))return 5;
    }
    std::fputs("LGAED_DUAL_INTERFACE_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    std::string witness(probe);
    for(const auto& [key,value]:
            std::array<std::pair<std::string_view,std::string_view>,7>{{
        {"@CONCRETE@",p.concrete_class},
        {"@VALUE_BASE@",p.value_interface},
        {"@REPORT_BASE@",p.report_interface},
        {"@VALUE_METHOD@",p.value_method},
        {"@WRITE_METHOD@",p.write_method},
        {"@CHANGE_METHOD@",p.change_method},
        {"@SEPARATOR@",p.report_separator}
    }})cpp_dual_interface_detail::substitute(witness,key,value);
    return std::string(prelude)+std::string(candidate)+witness;
}
} // namespace lgaed::curriculum
