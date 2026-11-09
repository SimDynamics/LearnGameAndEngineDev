#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lgaed::presentation::narration_recording {
// Current semantic plans now retain punctuation and adjacent hidden delimiters
// in their source envelope. Heard-prefix v1 used the lexical displayed bounds
// before that presentation fix. Recover only that exact old source for the
// fingerprint; the live segment and its word mappings keep the full envelope.
// Unmarked/legacy plans do not call this function. With no mapped words there
// are no lexical bounds to recover; preserve the supplied source unchanged.
template<class Unit,class Ranges>
std::basic_string_view<Unit> semantic_source_v1(std::basic_string_view<Unit> source,const Ranges& displayed) {
    static_assert(sizeof(Unit)==2,"Narration source offsets require UTF-16 code units");
    if(displayed.empty())return source;
    const auto boundary=[&](std::size_t at){
        return at==0||at==source.size()||
            !(source[at-1]>=0xd800&&source[at-1]<=0xdbff&&source[at]>=0xdc00&&source[at]<=0xdfff);
    };
    std::size_t first=source.size(),last=0;
    for(const auto& range:displayed){
        if(range.start>=range.end||range.end>source.size()||!boundary(range.start)||!boundary(range.end))
            throw std::invalid_argument("Invalid UTF-16 recording source range");
        // Several spoken words may share or overlap one displayed token.
        first=std::min(first,range.start);last=std::max(last,range.end);
    }
    return source.substr(first,last-first);
}

// Preserve the existing byte-length framing and field order exactly. The
// caller still hashes these bytes with SHA-256 and verifies the actual WAV.
inline std::string fingerprint_material_v1(std::string_view page,std::string_view part,
        std::string_view source,std::string_view audio_key,std::string_view wav_hash) {
    std::string material="lgaed-heard-prefix-v1";
    const auto append=[&](std::string_view value){material+=std::to_string(value.size())+':';material.append(value);};
    append(page);append(part);append(source);append(audio_key);append(wav_hash);
    return material;
}
} // namespace lgaed::presentation::narration_recording
