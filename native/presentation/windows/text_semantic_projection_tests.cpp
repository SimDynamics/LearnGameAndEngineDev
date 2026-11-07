#include "presentation/windows/text_semantic_projection.hpp"
#include <iostream>
#include <numeric>

namespace {
using namespace lgaed::presentation;
using namespace lgaed::curriculum;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class Action>void rejects(Action action,const char* message){bool rejected=false;try{action();}catch(const std::invalid_argument&){rejected=true;}check(rejected,message);}
std::vector<std::size_t> identity(std::size_t size){std::vector<std::size_t> result(size+1);std::iota(result.begin(),result.end(),0);return result;}
}
int main()try{
    unsigned groups=0;
    {
        const TextSemantics text{"Dato: LaGo!.",{{6,11,"LaGo!",SemanticRole::data,SemanticReading::characters}}};
        const auto original=text;
        const auto runs=semantic_display_runs(text,identity(12),12);
        check(runs==std::vector<SemanticDisplayRun>{{0,6},{6,11,SemanticRole::data,SemanticReading::characters},{11,12}}&&
            semantic_reference(runs[1].role)&&text==original,
            "Literal-character speech changed its data role, exact reference background or visible punctuation");
        ++groups;
    }
    {
        const TextSemantics text{"A `int x;`. B",{{3,9,"int x;",SemanticRole::cpp}}};
        const std::vector<std::size_t> map{0,1,2,2,3,4,5,6,7,8,8,9,10,11};
        const auto runs=semantic_display_runs(text,map,11);
        check(runs==std::vector<SemanticDisplayRun>{{0,2},{2,8,SemanticRole::cpp},{8,11}},
            "Code background escaped its exact source range into prose punctuation or lost internal spaces");++groups;
    }
    {
        const TextSemantics text{"A `int x;`. B",{{3,6,"int",SemanticRole::cpp},{6,9," x;",SemanticRole::prose}}};
        const auto untouched=text;
        const std::vector<std::size_t> map{0,1,2,2,3,4,5,6,7,8,8,9,10,11};
        const auto runs=semantic_display_runs(text,map,11);
        const std::vector<std::pair<std::size_t,std::size_t>> trusted{{2,8}};
        check(semantic_inline_font_ranges(runs,trusted)==std::vector<std::pair<std::size_t,std::size_t>>{{2,5}},
            "An authored code fragment lost its prior inline face or expanded it into declared prose");
        check(semantic_inline_font_ranges(runs,{}).empty(),"Authoring activated an inline face without an existing trusted fragment");
        for(const auto role:{SemanticRole::cpp,SemanticRole::string,SemanticRole::data,SemanticRole::output})
            check(semantic_inline_font_ranges(std::vector<SemanticDisplayRun>{{2,8,role}},trusted)==trusted,
                "A compatible authored inline reference lost its existing typography");
        for(const auto role:{SemanticRole::prose,SemanticRole::citation,SemanticRole::math})
            check(semantic_inline_font_ranges(std::vector<SemanticDisplayRun>{{2,8,role}},trusted).empty(),
                "The code face displaced prose, bibliography or the native mathematical face");
        check(text==untouched&&semantic_display_runs(text,map,11)==runs,
            "The typography plan mutated original semantics, source owners or reference ranges");
        ++groups;
    }
    {
        const TextSemantics text{"7\r\n8",{{0,1,"7",SemanticRole::data},{3,4,"8",SemanticRole::output}}};
        const std::vector<std::size_t> map{0,1,1,2,3};
        check(semantic_display_runs(text,map,3)==std::vector<SemanticDisplayRun>{{0,1,SemanticRole::data},{1,2},{2,3,SemanticRole::output}},
            "CRLF collapse changed authored ownership or highlighted the intervening paragraph");++groups;
    }
    {
        const TextSemantics text{"𐐀 int.",{{3,6,"int",SemanticRole::cpp,SemanticReading::technical}}};
        const auto runs=semantic_display_runs(text,identity(7),7);
        check(runs==std::vector<SemanticDisplayRun>{{0,3},{3,6,SemanticRole::cpp,SemanticReading::technical},{6,7}},
            "Supplementary Unicode shifted a semantic range or absorbed its final prose period");
        auto invalid=text;invalid.spans.front().start=1;
        rejects([&]{(void)semantic_display_runs(invalid,identity(7),7);},"A semantic projection accepted half a surrogate pair");++groups;
    }
    {
        const TextSemantics text{"std::cout 2024 int = 7",{}};
        const auto runs=semantic_display_runs(text,identity(text.source.size()),text.source.size());
        check(runs.size()==1&&runs.front().role==SemanticRole::prose&&!semantic_reference(runs.front().role),
            "An explicit empty field retained legacy code/number/citation guesses");
        const TextSemantics repeated{"int int",{{0,3,"int",SemanticRole::cpp},{4,7,"int",SemanticRole::prose}}};
        const auto distinct=semantic_display_runs(repeated,identity(7),7);
        check(distinct.size()==3&&semantic_reference(distinct.front().role)&&!semantic_reference(distinct.back().role),
            "Identical strings in one field borrowed each other's explicit role");++groups;
    }
    {
        for(const auto role:{SemanticRole::cpp,SemanticRole::string,SemanticRole::data,SemanticRole::math,SemanticRole::output})
            check(semantic_reference(role),"An authored technical/data role lost its existing reference background");
        check(!semantic_reference(SemanticRole::prose)&&!semantic_reference(SemanticRole::citation),
            "A bibliography or prose role acquired a technical background");
        const TextSemantics text{"int",{{0,3,"int",SemanticRole::prose,SemanticReading::natural}}};
        rejects([&]{(void)semantic_display_runs(text,std::vector<std::size_t>{0,2,1,3},3);},"A nonmonotone source/display map was accepted");
        rejects([&]{(void)semantic_display_runs(text,std::vector<std::size_t>{0,1,2},2);},"An incomplete UTF-16 map was accepted");++groups;
    }
    {
        std::map<std::string,TextSemantics> fields{
            {"/feedback/correct",{"Usa int.",{{4,7,"int",SemanticRole::cpp}}}},
            {"/responses/0/options/1/reviewFeedback",{"  Resultado: 7  ",{{2,14,"Resultado: 7",SemanticRole::output,SemanticReading::natural}}}}};
        const std::string full="✓ Correcto\nUsa int.\nResultado: 7";
        const std::vector<SemanticFieldOrigin> origins{{"/feedback/correct",11,19},{"/responses/0/options/1/reviewFeedback",20,32}};
        const auto composed=compose_semantic_fields(full,fields,origins);
        check(composed&&composed->source==full&&composed->spans==std::vector<SemanticSpan>{{15,18,"int",SemanticRole::cpp},{20,32,"Resultado: 7",SemanticRole::output,SemanticReading::natural}},
            "Typed feedback sources lost their exact code/output ranges, trim or generated-prefix ownership");
        auto other=fields;other.erase("/feedback/correct");
        rejects([&]{(void)compose_semantic_fields(full,other,origins);},"A partial feedback composition silently converted an unmarked guide to prose");
        check(!compose_semantic_fields(full,{},origins),"Legacy feedback manufactured explicit semantics");
        auto forged=origins;forged.back().start=19;
        rejects([&]{(void)compose_semantic_fields(full,fields,forged);},"Stale feedback provenance matched an unrelated phrase");
        ++groups;
    }
    {
        const std::string full="✗ Error\n𐐀 int";
        const std::map<std::string,TextSemantics> fields{{"/feedback/incorrect",{"\xc2\xa0𐐀 int\xc2\xa0",{{4,7,"int",SemanticRole::cpp}}}}};
        const auto composed=compose_semantic_fields(full,fields,std::vector<SemanticFieldOrigin>{{"/feedback/incorrect",8,14}});
        check(composed&&composed->spans==std::vector<SemanticSpan>{{11,14,"int",SemanticRole::cpp}},
            "Unicode whitespace trim or supplementary text changed feedback source coordinates");++groups;
    }
    std::cout<<"PASS "<<groups<<" pure semantic projection/composition groups; exact UTF-16 ranges, authoritative prose, backgrounds and typed feedback origins. No HWND, input or audio.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL semantic projection: "<<error.what()<<'\n';return 1;}
