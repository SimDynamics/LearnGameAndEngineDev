#pragma once

#include "core/cpp_expression_repair.hpp"
#include "core/cpp_class_structure_check.hpp"

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
    // First identify the unique real *global* definition with the same
    // token scanner used by the authoring checks. A namespace-local class
    // with the same spelling must not receive this privileged friend.
    const auto lexical=cpp_class_structure::lexical(source);
    if(!lexical)return {};
    const auto owner=cpp_class_structure::defined_class(*lexical,class_name);
    if(!owner)return {};

    // The position-aware tokenizer gives a byte-accurate insertion point.
    // Remove inert comment and literal include tokens, then verify the
    // token streams align before trusting any character offset.
    auto positioned=cpp_expression_repair_detail::tokens(source);
    if(!positioned||
       !cpp_expression_repair_detail::source_tokens_valid(*positioned))
        return {};
    std::erase_if(*positioned,[](const auto& token) {
        return token.value.starts_with("comment:")||
               token.value.starts_with("directive:");
    });
    if(positioned->size()!=lexical->size())return {};
    for(std::size_t i=0;i<lexical->size();++i)
        if((*positioned)[i].value!=(*lexical)[i])return {};
    return (*positioned)[owner->opening].end;
}

} // namespace lgaed::curriculum::cpp_class_definition_instrumentation
