#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
state::StateData without_sidecars(state::StateData data){for(auto& [domain,values]:data.preferences)if(domain=="native_windows")
    std::erase_if(values,[](const auto& item){return item.first.ends_with("feedback-origin/v1");});return data;}
}
int main() try {
    using namespace lgaed::curriculum;
    // In-memory adapter fixtures only: this test exercises provenance storage,
    // not native compilation, execution or any real learner achievement.
    Lesson lesson;lesson.id="qa_feedback_origins";lesson.locale="es";
    Block legacy;legacy.type=BlockType::cpp_compiler;legacy.stable_id="block_0000_0000";legacy.initial_code="int main() {}";
    OutputCase item;item.standard_input="";item.outputs={"good"};item.explanation="N=1";
    item.review_feedback={{{"bad"},"N=1"}};legacy.output_check=OutputCheck{TextComparison::exact,{item}};
    CppRunRequest request;request.revision=7;request.source_utf8=legacy.initial_code;
    FreshExecution correct;correct.source_revision=7;correct.compilation_succeeded=true;correct.execution_succeeded=true;
    correct.execution_completed=true;correct.exit_code=0;correct.standard_output="good";
    auto wrong=correct;wrong.standard_output="bad";
    state::StateData data;require(record_compiler_puzzle_case(data,lesson,legacy,request,correct,"Correcto\nN=1"),"Fixture did not retain its original completion");
    const auto original_receipts=data;
    Block block=legacy;
    const std::string explanation="/outputCheck/cases/0/explanation",review="/outputCheck/cases/0/reviewFeedback/0/text";
    block.text_semantics[explanation]=TextSemantics{"N=1",{{0,3,"N=1",SemanticRole::output}}};
    block.text_semantics[review]=TextSemantics{"N=1",{{0,3,"N=1",SemanticRole::data}}};
    auto feedback=compiler_puzzle_current_feedback(data,lesson,block,request);
    require(feedback&&feedback->correct&&feedback->source_path.empty(),"Legacy feedback inferred a source by equal wording");
    require(record_compiler_puzzle_case(data,lesson,block,request,correct,"Correcto\nN=1"),"An earned completion failed a fresh correct recheck");
    feedback=compiler_puzzle_current_feedback(data,lesson,block,request);
    require(feedback&&feedback->correct&&feedback->source_path==explanation,"Typed correct guidance was not preserved for reopen");
    require(without_sidecars(data)==original_receipts,"Presentation provenance changed a grade/proof/receipt");
    require(record_compiler_puzzle_review(data,lesson,block,request,wrong,"Incorrecto\nN=1"),"Typed rejection was not stored");
    feedback=compiler_puzzle_current_feedback(data,lesson,block,request);
    require(feedback&&!feedback->correct&&feedback->source_path==review,"Identical guide wording borrowed the success field's origin");
    require(compiler_puzzle_completed(data,lesson,block),"A feedback origin revoked a historical achievement");
    const auto reviewed=data;
    for(auto& [key,value]:data.preferences["native_windows"])if(key.ends_with("review/feedback-origin/v1")){
        auto origin=std::get<std::vector<std::string>>(value);origin[0]=explanation;value=std::move(origin);}
    feedback=compiler_puzzle_current_feedback(data,lesson,block,request);
    require(feedback&&!feedback->correct&&feedback->source_path.empty(),"A tampered source sidecar was trusted");
    data=reviewed;auto changed=block;changed.text_semantics[review].source="stale";
    feedback=compiler_puzzle_current_feedback(data,lesson,changed,request);
    require(feedback&&!feedback->correct&&feedback->source_path.empty(),"A stale descriptor was restored");
    auto unavailable=wrong;unavailable.standard_output="other";
    require(record_compiler_puzzle_review(data,lesson,block,request,unavailable,"Incorrecto\nGenerado"),"Generated rejection fixture failed");
    feedback=compiler_puzzle_current_feedback(data,lesson,block,request);
    require(feedback&&!feedback->correct&&feedback->source_path.empty(),"Generated feedback acquired an authored origin");
    require(record_compiler_puzzle_case(data,lesson,block,request,correct,"Correcto\nN=1"),"A subsequent correct run failed");
    require(without_sidecars(data)==original_receipts,"Recheck rewrote immutable historical receipts");
    // An editorial guide can change independently of achievement identity.
    // A current run must not relabel the old stored body as that new guide.
    auto revised=block;revised.output_check->cases[0].explanation="N=3";
    revised.text_semantics[explanation]=TextSemantics{"N=3",{{0,3,"N=3",SemanticRole::output}}};
    require(record_compiler_puzzle_case(data,lesson,revised,request,correct,"Correcto\nN=3"),"An editorial guide invalidated an existing correct result");
    feedback=compiler_puzzle_current_feedback(data,lesson,revised,request);
    require(feedback&&feedback->correct&&feedback->text=="Correcto\nN=1"&&feedback->source_path.empty(),"A recheck misattributed the stored historical guide");
    require(without_sidecars(data)==original_receipts,"Metadata changed receipts after an editorial guide revision");
    reset_compiler_puzzle(data,lesson,block);require(!compiler_puzzle_completed(data,lesson,block)&&
        !compiler_puzzle_current_feedback(data,lesson,block,request),"Explicit reset left provenance visible");
    std::cout<<"PASS typed compiler feedback sidecars, legacy/recheck isolation, equal-text disambiguation and stale/tampered rejection; in-memory fixtures only.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
