#include "presentation/windows/text_semantic_composition.hpp"
#include "core/lesson.hpp"
#include <iostream>

namespace {
using namespace lgaed::presentation;
using namespace lgaed::curriculum;
void check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
}
int main()try{
    const TextSemantics guide{"\r\n\xf0\x9f\x98\x80 `int p;`. Etiqueta: 7.",{
        {6,12,"int p;",SemanticRole::cpp}, {15,26,"Etiqueta: 7",SemanticRole::output}}};
    // The verdict is generated UI, the exact body remains the authored field.
    const auto full="✓ Correcto\n\n"+guide.source.substr(2);
    const auto marked=verdict_text_semantics(full,"✓ Correcto",&guide);
    check(marked&&marked->source==full,"Feedback source changed");
    check(marked->spans.size()==2&&marked->spans[0].text=="int p;"&&marked->spans[1].text=="Etiqueta: 7",
        "Feedback composition changed authored limits");
    check(marked->spans[0].start==16&&marked->spans[0].end==22,"Verdict/CRLF/astral projection changed");
    check(!verdict_text_semantics("✓ Correcto\n\nOlder guide","✓ Correcto",&guide),"Historical guide borrowed current metadata");
    check(!verdict_text_semantics(full,"✓ Correcto",nullptr),"Legacy feedback gained metadata");
    const TextSemantics prose{"Una frase: y palabras.",{}};
    check(verdict_text_semantics("✗ Incorrecto\n\nUna frase: y palabras.","✗ Incorrecto",&prose)->spans.empty(),
        "An explicitly natural guide acquired code roles");
    bool rejected=false;try{(void)prefix_text_semantics("",guide,3);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Trimming inside an astral scalar was accepted");
    OutputCheck objective;
    OutputCase first;first.standard_input="a";first.outputs={"7"};first.explanation="same guide";
    first.review_feedback.push_back({{"8"},"same guide"});
    auto second=first;second.standard_input="b";objective.cases={first,second};
    FreshExecution execution;execution.source_revision=9;execution.compilation_succeeded=true;
    execution.execution_succeeded=true;execution.execution_completed=true;execution.exit_code=0;
    execution.standard_input="b";execution.standard_output="7";
    auto result=evaluate_output_check(objective,execution,9);
    check(result.feedback_path=="/outputCheck/cases/1/explanation","Identical guides lost typed case provenance");
    execution.standard_output="8";result=evaluate_output_check(objective,execution,9);
    check(result.feedback_path=="/outputCheck/cases/1/reviewFeedback/0/text","Incorrect guide lost typed review provenance");
    execution.standard_output="unknown";check(evaluate_output_check(objective,execution,9).feedback_path.empty(),
        "Generated feedback acquired authored provenance");
    execution.source_revision=8;check(evaluate_output_check(objective,execution,9).feedback_path.empty(),
        "Stale evaluation acquired provenance");
    std::cout<<"PASS feedback composition, exact Unicode ranges, legacy/history preservation and typed evaluator origins; no UI/audio.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
