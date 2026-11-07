#include "core/text_semantics.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace lgaed::curriculum;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& run){bool rejected=false;try{run();}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Invalid semantic descriptor was accepted");}
}
int main() try {
    using namespace lgaed::curriculum;
    const std::string source="\xf0\x9f\x98\x80 Usa `int p;`. (Autor, 2024)\r\n";
    TextSemantics value{source,{{8,14,"int p;",SemanticRole::cpp,SemanticReading::technical},
        {17,30,"(Autor, 2024)",SemanticRole::citation,SemanticReading::natural}}};
    validate_text_semantics(value);validate_text_semantics({source,{}});
    require(value.source==source&&value.spans[0].text=="int p;","Validation changed an authored source");
    for(const auto role:{SemanticRole::prose,SemanticRole::cpp,SemanticRole::string,SemanticRole::data,SemanticRole::math,SemanticRole::output,SemanticRole::citation})
        require(parse_semantic_role(semantic_role_name(role))==role,"Semantic role round trip changed");
    for(const auto reading:{SemanticReading::automatic,SemanticReading::natural,SemanticReading::technical,SemanticReading::characters})
        require(parse_semantic_reading(semantic_reading_name(reading))==reading,"Semantic reading round trip changed");
    rejects([]{(void)parse_semantic_role("red");});rejects([]{(void)parse_semantic_reading("phonetic");});
    for(const auto [first,last]:{std::pair<std::size_t,std::size_t>{0,1},{1,2},{2,2},{10,8},{0,1000}})
        rejects([&]{auto bad=value;bad.spans={{first,last,"x",SemanticRole::data}};validate_text_semantics(bad);});
    rejects([&]{auto bad=value;bad.spans[0].text="int q;";validate_text_semantics(bad);});
    rejects([&]{auto bad=value;bad.spans.push_back(bad.spans[0]);validate_text_semantics(bad);});
    rejects([&]{auto bad=value;bad.spans[0].role=static_cast<SemanticRole>(999);validate_text_semantics(bad);});
    rejects([&]{auto bad=value;bad.spans[0].reading=static_cast<SemanticReading>(999);validate_text_semantics(bad);});
    for(const auto invalid:{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2)})
        rejects([&]{validate_text_semantics({invalid,{}});});
    std::cout<<"PASS exact semantic UTF-8/UTF-16 ranges, source, role/reading and explicit empty-field contract; no UI/audio.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
