#pragma once
#include "core/text_semantics.hpp"
#include <algorithm>
#include <span>
#include <stdexcept>
#include <map>
#include <optional>

namespace lgaed::presentation {
struct SemanticDisplayRun {
    std::size_t start=0,end=0;
    curriculum::SemanticRole role=curriculum::SemanticRole::prose;
    curriculum::SemanticReading reading=curriculum::SemanticReading::automatic;
    bool operator==(const SemanticDisplayRun&) const=default;
};
struct SemanticFieldOrigin {std::string path;std::size_t start=0,end=0;};
inline std::vector<std::size_t> semantic_utf8_boundaries(std::string_view text){
    curriculum::validate_text_semantics({std::string(text),{}});
    std::vector<std::size_t> result{0};
    for(std::size_t at=0;at<text.size();){const auto lead=static_cast<unsigned char>(text[at]);const auto count=lead<0x80?1:lead<0xe0?2:lead<0xf0?3:4;
        if(count==4)result.push_back(std::string_view::npos);at+=count;result.push_back(at);}
    return result;
}
// These are Kotlin Char.isWhitespace's Unicode whitespace scalars, as used by
// the existing feedback composer. Only a known field's surrounding whitespace
// may be trimmed; no phrase search or spelling-based provenance is permitted.
inline bool semantic_trim_space(std::string_view scalar){
    unsigned value=static_cast<unsigned char>(scalar.front());
    if(value>=0x80){const auto count=scalar.size();value&=count==2?0x1f:count==3?0x0f:0x07;
        for(std::size_t i=1;i<count;++i)value=(value<<6)|(static_cast<unsigned char>(scalar[i])&0x3f);}
    return (value>=9&&value<=13)||(value>=0x1c&&value<=0x20)||value==0xa0||value==0x1680||
        (value>=0x2000&&value<=0x200a)||value==0x2028||value==0x2029||value==0x202f||value==0x205f||value==0x3000;
}
inline std::optional<curriculum::TextSemantics> compose_semantic_fields(std::string_view full_text,
        const std::map<std::string,curriculum::TextSemantics>& fields,std::span<const SemanticFieldOrigin> origins){
    const auto full=semantic_utf8_boundaries(full_text);curriculum::TextSemantics result{std::string(full_text),{}};
    const bool any_authored=std::ranges::any_of(origins,[&](const auto& origin){return fields.contains(origin.path);});
    if(!any_authored)return {};
    bool authored=false;std::size_t previous=0;
    for(const auto& origin:origins){
        if(origin.start>=origin.end||origin.start<previous||origin.end>=full.size()||
            full[origin.start]==std::string_view::npos||full[origin.end]==std::string_view::npos)
            throw std::invalid_argument("Invalid feedback semantic origin range");
        previous=origin.end;const auto found=fields.find(origin.path);
        if(found==fields.end())throw std::invalid_argument("A partially annotated feedback composition lacks an authored source descriptor");
        authored=true;const auto& field=found->second;curriculum::validate_text_semantics(field);
        const auto source=semantic_utf8_boundaries(field.source);
        std::size_t first=0,last=source.size()-1;
        const auto visible=full_text.substr(full[origin.start],full[origin.end]-full[origin.start]);
        if(visible!=field.source){
            const auto scalar_end=[&](std::size_t at){return at+1<source.size()&&source[at+1]==std::string_view::npos?at+2:at+1;};
            while(first<last){const auto end=scalar_end(first);if(!semantic_trim_space(std::string_view(field.source).substr(source[first],source[end]-source[first])))break;first=end;}
            while(last>first){auto begin=last-1;if(source[begin]==std::string_view::npos)--begin;
                if(!semantic_trim_space(std::string_view(field.source).substr(source[begin],source[last]-source[begin])))break;last=begin;}
            if(visible!=std::string_view(field.source).substr(source[first],source[last]-source[first]))
                throw std::invalid_argument("Feedback semantic provenance differs from its exact authored field");
        }
        if(origin.end-origin.start!=last-first)throw std::invalid_argument("Feedback semantic origin has a mismatched UTF-16 extent");
        for(const auto& span:field.spans){const auto begin=std::max(span.start,first),end=std::min(span.end,last);if(begin>=end)continue;
            result.spans.push_back({origin.start+begin-first,origin.start+end-first,
                field.source.substr(source[begin],source[end]-source[begin]),span.role,span.reading});}
    }
    if(!authored)return {};curriculum::validate_text_semantics(result);return result;
}
inline bool semantic_reference(curriculum::SemanticRole role) {
    using Role=curriculum::SemanticRole;
    return role==Role::cpp||role==Role::string||role==Role::data||role==Role::math||role==Role::output;
}
// The complete monotone UTF-16 map can hide delimiters, collapse CRLF or
// compose a mathematical native projection. Unmarked gaps remain prose.
inline std::vector<SemanticDisplayRun> semantic_display_runs(const curriculum::TextSemantics& semantics,
        std::span<const std::size_t> source_to_display,std::size_t display_size) {
    curriculum::validate_text_semantics(semantics);
    std::size_t units=0;
    for(const auto byte:semantics.source)if((static_cast<unsigned char>(byte)&0xc0)!=0x80)
        units+=static_cast<unsigned char>(byte)>=0xf0?2:1;
    if(source_to_display.size()!=units+1||source_to_display.front()!=0||source_to_display.back()!=display_size||
        !std::ranges::is_sorted(source_to_display))throw std::invalid_argument("Invalid semantic source/display projection");
    std::vector<SemanticDisplayRun> result;std::size_t cursor=0;
    for(const auto& span:semantics.spans){
        const auto first=source_to_display[span.start],last=source_to_display[span.end];
        if(first<cursor||last<first||last>display_size)throw std::invalid_argument("Overlapping semantic display projection");
        if(cursor<first)result.push_back({cursor,first});
        if(first<last)result.push_back({first,last,span.role,span.reading});
        cursor=last;
    }
    if(cursor<display_size)result.push_back({cursor,display_size});
    return result;
}
} // namespace lgaed::presentation
