#pragma once
#include "core/text_semantics.hpp"
#include "presentation/windows/text_semantic_projection.hpp"
#include <algorithm>
#include <optional>

namespace lgaed::presentation {
// Compose a known source field with generated UI text. This never searches a
// label for a matching word or consults an answer to choose its semantic role.
inline curriculum::TextSemantics prefix_text_semantics(std::string_view prefix,
        const curriculum::TextSemantics& field,std::size_t trim_leading_utf16=0) {
    curriculum::validate_text_semantics(field);
    const auto boundaries=semantic_utf8_boundaries(field.source);
    if(trim_leading_utf16>=boundaries.size()||boundaries[trim_leading_utf16]==std::string_view::npos||
       std::ranges::any_of(std::string_view(field.source).substr(0,boundaries[trim_leading_utf16]),
            [](char c){return c!='\r'&&c!='\n';}))
        throw std::invalid_argument("Semantic feedback trimming must address only its generated leading line breaks");
    curriculum::TextSemantics result;
    result.source=std::string(prefix)+field.source.substr(boundaries[trim_leading_utf16]);
    const auto offset=semantic_utf8_boundaries(prefix).size()-1;
    for(auto span:field.spans) {
        if(span.end<=trim_leading_utf16)continue;
        const auto first=std::max(span.start,trim_leading_utf16);
        span.text=field.source.substr(boundaries[first],boundaries[span.end]-boundaries[first]);
        span.start=offset+first-trim_leading_utf16;span.end=offset+span.end-trim_leading_utf16;
        result.spans.push_back(std::move(span));
    }
    curriculum::validate_text_semantics(result);return result;
}
// A historical feedback body may differ from today's authored field. In that
// case its unmarked source is retained, rather than borrowing a current role.
inline std::optional<curriculum::TextSemantics> verdict_text_semantics(std::string_view rendered,
        std::string_view verdict_prefix,const curriculum::TextSemantics* field) {
    if(!field)return {};
    const auto& source=field->source;std::size_t trim=0;
    while(trim<source.size()&&(source[trim]=='\r'||source[trim]=='\n'))++trim;
    const auto body=source.substr(trim);
    const auto prefix=std::string(verdict_prefix)+(body.empty()?"":"\n\n");
    auto composed=prefix_text_semantics(prefix,*field,trim);
    if(composed.source!=rendered)return {};
    return composed;
}
} // namespace lgaed::presentation
