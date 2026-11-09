#include "presentation/windows/narration_recording_identity.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::presentation::narration_recording;
struct Range {std::size_t start,end;};
unsigned checks=0;
void check(bool condition,const char* message) {
    ++checks;if(!condition)throw std::runtime_error(message);
}
// Portable test encoding only. Production uses its existing strict Win32
// UTF-8/UTF-16 conversion; char16_t keeps these test offsets UTF-16 on Linux.
std::string encoded(std::u16string_view source) {
    std::string result;
    for(std::size_t i=0;i<source.size();++i){
        std::uint32_t scalar=source[i];
        if(scalar>=0xd800&&scalar<=0xdbff){
            if(++i==source.size()||source[i]<0xdc00||source[i]>0xdfff)throw std::invalid_argument("Invalid test UTF-16");
            scalar=0x10000+((scalar-0xd800)<<10)+(source[i]-0xdc00);
        }else if(scalar>=0xdc00&&scalar<=0xdfff)throw std::invalid_argument("Invalid test UTF-16");
        if(scalar<0x80)result+=static_cast<char>(scalar);
        else if(scalar<0x800){result+=static_cast<char>(0xc0|(scalar>>6));result+=static_cast<char>(0x80|(scalar&0x3f));}
        else if(scalar<0x10000){result+=static_cast<char>(0xe0|(scalar>>12));result+=static_cast<char>(0x80|((scalar>>6)&0x3f));result+=static_cast<char>(0x80|(scalar&0x3f));}
        else{result+=static_cast<char>(0xf0|(scalar>>18));result+=static_cast<char>(0x80|((scalar>>12)&0x3f));result+=static_cast<char>(0x80|((scalar>>6)&0x3f));result+=static_cast<char>(0x80|(scalar&0x3f));}
    }
    return result;
}
std::string part(std::string_view owner,std::string_view prepared,unsigned occurrence=0) {
    return std::string(owner)+'\0'+std::string(prepared)+'\0'+std::to_string(occurrence);
}
std::string material(std::string_view source,std::string_view owner="block/text",
        std::string_view prepared="same prepared speech",unsigned occurrence=0,
        std::string_view page="page-key",std::string_view audio="audio-key",std::string_view wav="wav-hash") {
    return fingerprint_material_v1(page,part(owner,prepared,occurrence),source,audio,wav);
}
void equivalent(std::u16string_view before,std::u16string_view after,const std::vector<Range>& ranges,
        std::string_view owner,std::string_view prepared,std::string_view audio) {
    const auto canonical=semantic_source_v1(after,ranges);
    check(before!=after,"The source-envelope regression does not actually expand the source");
    check(canonical==before,"The expanded semantic envelope did not recover its exact previous source");
    const auto old_material=material(encoded(before),owner,prepared,0,"same-page-key",audio);
    check(material(encoded(canonical),owner,prepared,0,"same-page-key",audio)==old_material,
        "The presentation-only envelope change altered heard-prefix v1 bytes");
    check(material(encoded(after),owner,prepared,0,"same-page-key",audio)!=old_material,
        "The regression would not detect hashing the expanded source directly");
    std::size_t first=after.size();for(const auto range:ranges)first=std::min(first,range.start);
    auto rebased=ranges;for(auto& range:rebased){range.start-=first;range.end-=first;}
    check(semantic_source_v1(before,rebased)==before,"Canonicalizing an unchanged old semantic source was not idempotent");
}
void rejects(std::u16string_view source,std::vector<Range> ranges) {
    bool rejected=false;
    try{(void)semantic_source_v1(source,ranges);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Invalid or split-surrogate displayed ranges entered the recording identity");
}
}

int main() try {
    // Fixed wire-format oracle: sizes are UTF-8 bytes and embedded NULs in
    // part_id remain part of the field. These are identity bytes, not a saved
    // listening certificate, profile, waveform or fabricated playback result.
    const auto id=part("b","p");
    const auto expected=std::string("lgaed-heard-prefix-v11:p5:")+id+"2:\xc3\xa1" "1:a1:w";
    check(fingerprint_material_v1("p",id,"\xc3\xa1","a","w")==expected,"The original v1 prefix, order or byte-length framing changed");
    check(encoded(u"\U0001f63a caf\u00e9 e\u0301")=="\xf0\x9f\x98\xba" " caf\xc3\xa9" " e\xcc\x81",
        "The portable fixture encoding differs from its exact UTF-8 bytes");

    // Captured before/after from the actual Windows planner for authenticated
    // U12L20 published source. test_semantic_source_envelopes authenticates
    // the JSON and planner separately; these fixtures exercise native framing.
    // block_0005_0002/text field SHA-256:
    // 493fff3e65e540ec33dbe13ee501d39fafd67dd4d3994c3ae3978439feb0bc6b
    // Sentence 1 moves [56,94) -> [55,96), while prepared speech/key stay exact.
    equivalent(u"letras`, `digitos`, `blancos` y `otros",u"`letras`, `digitos`, `blancos` y `otros`.",
        {{1,7},{11,18},{22,29},{31,32},{34,39}},"block_0005_0002/text",
        "\xee\x80\x82" "letras, digitos, blancos y otros.",
        "72a2fb4d4cbe92076b0b8f437f19d70e912dde681bcd88aeed5a1ef989ab10ce");

    // block_0005_0001/text field SHA-256:
    // 268fb9498fee060a9f3ba14b63f7962c5af5809c080953568caac87f6da0ed85
    // Sentence 0 moves [0,56) -> [0,58); many spoken words share a source token.
    equivalent(u"Construye `bool texto_admitido(const std::string& texto)",
        u"Construye `bool texto_admitido(const std::string& texto)`.",
        {{0,9},{11,15},{16,30},{16,30},{16,30},{16,30},{30,31},{30,31},{31,36},
         {37,40},{37,40},{37,40},{40,42},{40,42},{40,42},{40,42},{40,42},{42,48},{48,49},{50,55},{55,56},{55,56}},
        "block_0005_0001/text",
        encoded(u"\ue002Construye \ue000bool\ue001 texto guion bajo admitido abre par\u00e9ntesis \ue000const\ue001 ese te de operador de resoluci\u00f3n de \u00e1mbito \ue000string\ue001 ampersand texto cierra par\u00e9ntesis."),
        "f0f00dde531b743ec489c061cf3dcf74c3feb293c2c9de11edb11c2c53fd2b83");

    equivalent(u"\U0001f63a caf\u00e9 e\u0301",u"\u00ab`\U0001f63a caf\u00e9 e\u0301`.\u00bb",
        {{10,12},{5,9},{2,4},{5,9},{6,9}},"unicode/text","same unicode speech","unicode-key");
    equivalent(u"\u2713 Correcto\n\n`N=26",u"\u2713 Correcto\n\n`N=26`.",
        {{0,10},{13,17},{13,17}},"feedback","same correct feedback","feedback-key");

    const std::u16string empty_mapping_source=u"`No mapped words`.";
    check(semantic_source_v1(std::u16string_view(empty_mapping_source),std::vector<Range>{})==empty_mapping_source,
        "An empty word map changed the supplied source");
    const std::u16string embedded_nul{u"A\0B",3};
    check(encoded(semantic_source_v1(std::u16string_view(embedded_nul),std::vector<Range>{{0,3}}))==std::string("A\0B",3),
        "The source identity truncated an embedded NUL");
    const auto unmarked="`plain source`.";
    check(fingerprint_material_v1("p","b",unmarked,"a","w")=="lgaed-heard-prefix-v11:p1:b15:`plain source`.1:a1:w",
        "The v1 framing no longer preserves an unmarked source verbatim");

    rejects(u"text",{{0,0}});rejects(u"text",{{3,2}});rejects(u"text",{{0,5}});
    rejects(u"text",{{std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max()}});
    rejects(u"\U0001f63a!",{{1,2}});rejects(u"\U0001f63a!",{{0,1}});
    rejects(u"\U0001f63a!",{{0,3},{1,2}});

    const auto baseline=material("lexical source");
    check(material("different source")!=baseline,"Changed lexical source borrowed the same v1 material");
    check(material("lexical source","other owner")!=baseline,"Another block borrowed the same v1 material");
    check(material("lexical source","block/text","changed speech")!=baseline,"Changed prepared speech borrowed the same v1 material");
    check(material("lexical source","block/text","same prepared speech",1)!=baseline,"Another occurrence borrowed the same v1 material");
    check(material("lexical source","block/text","same prepared speech",0,"other page")!=baseline,"Another page key borrowed the same v1 material");
    check(material("lexical source","block/text","same prepared speech",0,"page-key","other voice/audio key")!=baseline,
        "Another recording key borrowed the same v1 material");
    check(material("lexical source","block/text","same prepared speech",0,"page-key","audio-key","changed WAV hash")!=baseline,
        "Changed PCM bytes borrowed the same v1 material");
    check(fingerprint_material_v1("ab","c","s","a","w")!=fingerprint_material_v1("a","bc","s","a","w"),
        "Field boundaries became ambiguous");
    std::cout<<"PASS "<<checks<<" pure recording-identity checks: exact prior v1 bytes, two captured U12 sources, UTF-16 boundaries and identity negatives. No audio, profile or listening credit.\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL narration recording identity: "<<error.what()<<'\n';return 1;
}
