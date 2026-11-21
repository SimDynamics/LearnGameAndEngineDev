#pragma once

#include "core/cpp_expression_repair.hpp"
#include "core/cpp_member_body_check.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lgaed::curriculum::cpp_class_definition_instrumentation {

// Locate the actual class token, not a matching phrase inside a comment or
// literal before the declaration. Keep strict source offsets and require a
// complete class definition with its own terminator; never insert a private
// friend into unrelated source or a forward declaration.
inline std::optional<std::size_t> body_insertion(
        std::string_view source,std::string_view class_name) {
    auto scan=cpp_expression_repair_detail::tokens(source);
    if(!scan)return {};
    std::erase_if(*scan,[](const auto& token) {
        return token.value.starts_with("comment:");
    });
    const std::string wanted="id:"+std::string(class_name);
    std::optional<std::size_t> insertion;
    for(std::size_t i=0;i+2<scan->size();++i) {
        if(((*scan)[i].value!="id:class" && (*scan)[i].value!="id:struct") ||
           (*scan)[i+1].value!=wanted)continue;
        auto brace=i+2;
        if(brace<scan->size() && (*scan)[brace].value=="id:final")++brace;
        if(brace>=scan->size() ||
           ((*scan)[brace].value!="op:{" && (*scan)[brace].value!="op:<%"))
            continue; // Forward declaration or a different C++ construct.
        const auto closing=cpp_member_body_detail::closing_brace(
            source,(*scan)[brace].begin);
        if(!closing)return {};
        const auto after_closing=*closing+
            (source[*closing]=='%'?2u:1u);
        const auto next=std::ranges::find_if(*scan,[&](const auto& token) {
            return token.begin>=after_closing;
        });
        if(next==scan->end()||next->value!="op:;"||insertion)return {};
        insertion=(*scan)[brace].end;
    }
    return insertion;
}

} // namespace lgaed::curriculum::cpp_class_definition_instrumentation
