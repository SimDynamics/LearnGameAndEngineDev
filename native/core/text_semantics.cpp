#include "core/text_semantics.hpp"
#include <limits>
#include <initializer_list>
#include <stdexcept>

namespace lgaed::curriculum {
std::string_view semantic_role_name(SemanticRole role) {
    switch(role){
    case SemanticRole::prose:return "prose";case SemanticRole::cpp:return "cpp";
    case SemanticRole::string:return "string";case SemanticRole::data:return "data";
    case SemanticRole::math:return "math";case SemanticRole::output:return "output";
    case SemanticRole::citation:return "citation";
    }
    throw std::invalid_argument("Unknown semantic role");
}
std::string_view semantic_reading_name(SemanticReading reading) {
    switch(reading){case SemanticReading::automatic:return "auto";case SemanticReading::natural:return "natural";case SemanticReading::technical:return "technical";case SemanticReading::characters:return "characters";}
    throw std::invalid_argument("Unknown semantic reading intent");
}
SemanticRole parse_semantic_role(std::string_view role) {
    for(const auto value:{SemanticRole::prose,SemanticRole::cpp,SemanticRole::string,SemanticRole::data,SemanticRole::math,SemanticRole::output,SemanticRole::citation})
        if(semantic_role_name(value)==role)return value;
    throw std::invalid_argument("Unknown semantic role");
}
SemanticReading parse_semantic_reading(std::string_view reading) {
    for(const auto value:{SemanticReading::automatic,SemanticReading::natural,SemanticReading::technical,SemanticReading::characters})if(semantic_reading_name(value)==reading)return value;
    throw std::invalid_argument("Unknown semantic reading intent");
}
namespace {
constexpr auto invalid_boundary=std::numeric_limits<std::size_t>::max();
std::vector<std::size_t> boundaries(std::string_view source) {
    std::vector<std::size_t> result{0};
    for(std::size_t offset=0;offset<source.size();){
        const auto first=static_cast<unsigned char>(source[offset]);std::size_t count=0;unsigned scalar=0;
        if(first<0x80){count=1;scalar=first;}
        else if(first>=0xc2&&first<=0xdf){count=2;scalar=first&0x1f;}
        else if(first>=0xe0&&first<=0xef){count=3;scalar=first&0x0f;}
        else if(first>=0xf0&&first<=0xf4){count=4;scalar=first&7;}
        else throw std::invalid_argument("Semantic source is not valid UTF-8");
        if(offset+count>source.size())throw std::invalid_argument("Truncated semantic UTF-8 source");
        for(std::size_t n=1;n<count;++n){const auto byte=static_cast<unsigned char>(source[offset+n]);
            if((byte&0xc0)!=0x80)throw std::invalid_argument("Invalid semantic UTF-8 continuation");scalar=(scalar<<6)|(byte&0x3f);}
        if((count==2&&scalar<0x80)||(count==3&&scalar<0x800)||(count==4&&scalar<0x10000)||
                scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff))throw std::invalid_argument("Invalid semantic Unicode scalar");
        offset+=count;if(scalar>0xffff)result.push_back(invalid_boundary);result.push_back(offset);
    }
    return result;
}
}
void validate_text_semantics(const TextSemantics& text) {
    if(text.source.size()>8*1024*1024||text.spans.size()>100000)throw std::invalid_argument("Semantic field exceeds its size limit");
    const auto offsets=boundaries(text.source);std::size_t previous=0;
    for(const auto& span:text.spans){
        (void)semantic_role_name(span.role);(void)semantic_reading_name(span.reading);
        if(span.start>=span.end||span.end>=offsets.size()||span.start<previous||
                offsets[span.start]==invalid_boundary||offsets[span.end]==invalid_boundary)
            throw std::invalid_argument("Semantic spans must be ordered disjoint UTF-16 scalar ranges");
        if(std::string_view(text.source).substr(offsets[span.start],offsets[span.end]-offsets[span.start])!=span.text)
            throw std::invalid_argument("Semantic selection differs from its exact source text");
        previous=span.end;
    }
}
} // namespace lgaed::curriculum
