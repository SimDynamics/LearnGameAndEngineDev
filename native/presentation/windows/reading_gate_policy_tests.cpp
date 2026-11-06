#include "presentation/windows/reading_gate_policy.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void require(bool value,std::string_view reason) { if(!value)throw std::runtime_error(std::string(reason)); }
struct Part { LessonReadingRegion region; };
constexpr auto projection=[](auto& part)->auto& { return part.region; };
std::vector<LessonReadingRegion> current_page() {
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_conversiones_y_deduccion.json";
    auto lesson=windows::load_lesson(path);
    // Keep the original regression meaningful after editorial activation of
    // the flag: the mandatory policy must also hold for the old missing flag.
    lesson.compiler_puzzles_required=false;
    const auto& page=lesson.pages.at(6);
    require(page.blocks.at(2).stable_id=="block_0006_0002"&&page.blocks.at(6).stable_id=="block_0006_0006",
        "The two current compiler objective owners changed");
    require(page.blocks.at(3).text.starts_with("Con quince y dos,"),"The originally over-read passage changed");
    require(reading_gate::compiler_objective(page.blocks.at(2))&&reading_gate::compiler_objective(page.blocks.at(6)),
        "An authored output objective incorrectly depends on the lesson navigation flag");
    std::vector<LessonReadingRegion> regions;
    for(std::size_t n=0;n<page.blocks.size();++n) {
        const auto& block=page.blocks[n];
        LessonReadingRegion region;
        region.key=lesson.id+'/'+page.stable_id+'/'+block.stable_id;
        region.anchor=region.key+"/part/0";region.block_index=n;
        region.required_compiler=reading_gate::compiler_objective(block);
        region.is_exercise=region.required_compiler;
        region.solved=!region.required_compiler;
        region.source_text=region.required_compiler?"Editor de código C más más.":block.text;
        regions.push_back(std::move(region));
    }
    return regions;
}
void current_objectives() {
    auto regions=current_page();
    require(!reading_gate::compiler_rewind_target(regions,1,false),"An earlier paragraph was rewound unnecessarily");
    require(!reading_gate::compiler_rewind_target(regions,2,false),"A pending compiler instruction cannot play");
    require(reading_gate::compiler_rewind_target(regions,3,false)==2,"A late bookmark crossed the first unverified compiler");
    require(reading_gate::compiler_rewind_target(regions,2,true)==2,"A finished compiler bookmark bypassed its objective");
    require(reading_gate::compiler_rewind_target(regions,6,true)==2,"A finished page skipped the earliest compiler");
    require(!reading_gate::prefix_admitted(regions,3,0),"The reader admitted 'Con quince y dos' before checking the first program");
    require(!reading_gate::prefix_admitted(regions,3,0,{},true),"Manual advance bypassed an unresolved compiler");
    regions[2].solved=true;
    require(reading_gate::prefix_admitted(regions,3,0),"Verified first objective did not release its following explanation");
    require(reading_gate::compiler_rewind_target(regions,6,true)==6,"The second objective inherited completion of the first");
    require(!reading_gate::prefix_admitted(regions,regions.size(),0),"End-of-page admission bypassed the second compiler");
    regions[6].solved=true;
    require(reading_gate::prefix_admitted(regions,regions.size(),0),"Both actual verified objectives failed to release the page");
    require(!reading_gate::compiler_rewind_target(regions,6,true),"A fully verified page was rewound");

    curriculum::Block optional;optional.type=curriculum::BlockType::cpp_compiler;
    require(reading_gate::compiler_objective(optional),"An editor without its evaluator became an optional curricular activity");
    optional.type=curriculum::BlockType::paragraph;
    require(!reading_gate::compiler_objective(optional),"Ordinary prose became a compiler objective");
}
void grouped_feedback() {
    auto regions=current_page();
    // Several spoken segments from one editor remain a single block gate.
    regions.insert(regions.begin()+3,regions[2]);
    require(reading_gate::prefix_admitted(regions,3,0),"The reader stopped inside the compiler's own spoken group");
    require(!reading_gate::prefix_admitted(regions,4,0),"The last compiler segment released the next paragraph unsolved");
    require(reading_gate::compiler_rewind_target(regions,4,false)==2,"A split compiler group rewound to its end instead of its beginning");
    auto feedback=regions.back();feedback.key+="/feedback";feedback.source_text="Respuesta comprobada.";
    feedback.block_index=20;feedback.required_compiler=false;feedback.is_exercise=true;
    feedback.feedback=true;feedback.correct=true;feedback.solved=true;
    const auto feedback_at=regions.size();regions.push_back(feedback);
    const reading_gate::FeedbackPriority priority{20,feedback_at};
    require(reading_gate::prefix_admitted(regions,feedback_at,0,priority),"A fresh answer cannot enter its own verified feedback");
    require(!reading_gate::prefix_admitted(regions,regions.size(),0,priority),"Fresh feedback granted credit to another pending compiler");
    require(!reading_gate::prefix_admitted(regions,regions.size(),0,priority,true),"Manual feedback advance granted compiler completion");
    for(auto& region:regions)if(region.required_compiler)region.solved=true;
    require(!reading_gate::prefix_admitted(regions,regions.size(),0,priority),"Unheard correct feedback was skipped");
    require(reading_gate::prefix_admitted(regions,regions.size(),0,priority,true),"Explicit manual advance lost the existing active-feedback behavior");
    require(reading_gate::prefix_admitted(regions,regions.size(),static_cast<std::int64_t>(regions.size())),
        "Heard correct feedback failed to release the completed prefix");
}
void asynchronous_metadata() {
    auto live=current_page();
    for(auto& region:live)if(region.required_compiler)region.solved=true;
    std::vector<Part> planned;for(const auto& region:live)planned.push_back({region});
    // Planning captured success. Editing invalidates its proof while spoken
    // text/key/context remain identical, so no new audio plan is necessary.
    live[2].solved=false;live[2].correct=false;live[2].anchor+="/live";
    require(reading_gate::same_script_region(planned[2].region,live[2]),"Metadata invalidation changed the spoken identity");
    require(reading_gate::reconcile(planned,live,projection),"A same-script completion update unnecessarily required replanning");
    require(!planned[2].region.solved&&planned[2].region.anchor==live[2].anchor,"A late plan reinstated stale compiler metadata");
    require(reading_gate::compiler_rewind_target(planned,3,false,projection)==2,"Reconciled pending metadata did not constrain the late bookmark");
    require(!reading_gate::prefix_admitted(planned,3,0,{},false,projection),"A stale asynchronous success crossed the live compiler gate");
    live[2].solved=true;require(reading_gate::reconcile(planned,live,projection),"Live completion could not refresh an unchanged plan");
    require(reading_gate::prefix_admitted(planned,3,0,{},false,projection),"Same-script verified completion failed to release the gate");

    auto changed=live;changed[0].source_text+=" Texto nuevo.";changed[2].solved=false;
    require(!reading_gate::reconcile(planned,changed,projection),"Changed speech was accepted as metadata-only reconciliation");
    require(planned[2].region.solved,"A rejected plan mapping partially copied live metadata");
    changed=live;changed[3].reading_context="cpp";
    require(!reading_gate::reconcile(planned,changed,projection),"Changed speech context retained a stale plan");
    changed=live;changed[3].math_mode="text";
    require(!reading_gate::reconcile(planned,changed,projection),"Changed mathematical speech mode retained a stale plan");
    changed=live;changed.erase(changed.begin()+2);
    require(!reading_gate::reconcile(planned,changed,projection),"A disappeared region retained its old completion metadata");
    changed=live;changed.push_back(live[2]);
    require(!reading_gate::reconcile(planned,changed,projection),"An ambiguous region identity was accepted");
}
void preview_from_later_feedback() {
    auto live=current_page();
    auto feedback=live[3];feedback.key+="/feedback";feedback.block_index=20;
    feedback.source_text="Respuesta comprobada.";feedback.feedback=true;feedback.correct=true;
    feedback.is_exercise=true;feedback.solved=true;feedback.required_compiler=false;
    const auto feedback_at=live.size();live.push_back(feedback);
    const reading_gate::FeedbackPriority priority{20,feedback_at};
    require(reading_gate::prefix_admitted(live,feedback_at,0,priority),"A later answer lost its explicit feedback admission");
    // Returning from that feedback is a preview of another region, not proof
    // that the intervening explanation was heard or its compiler checked.
    require(live[3].source_text.starts_with("Con quince y dos,"),"The intermediate over-read explanation changed");
    require(reading_gate::compiler_rewind_target(live,3,false)==2,
        "A backward preview from later feedback entered unverified intermediate prose");
    live[2].solved=true;
    require(!reading_gate::compiler_rewind_target(live,3,false),
        "A verified preceding objective or an unrelated future objective blocked earlier prose");
    std::vector<Part> planned;for(const auto& region:live)planned.push_back({region});
    live[2].solved=false;
    require(reading_gate::reconcile(planned,live,projection),"The preview could not refresh invalidated live completion metadata");
    require(reading_gate::compiler_rewind_target(planned,3,false,projection)==2,
        "A preview kept a previously valid receipt after current metadata invalidated it");
}
void ordinary_and_mixed_requirements() {
    const auto passage=[](std::size_t block,bool exercise,bool solved,bool compiler=false){
        LessonReadingRegion region;region.key="mixed/"+std::to_string(block);
        region.source_text="Pasaje conservado.";region.block_index=block;
        region.is_exercise=exercise;region.solved=solved;region.required_compiler=compiler;return region;
    };
    std::vector<LessonReadingRegion> regions{
        passage(0,false,true),passage(1,true,false),passage(1,false,true),
        passage(2,false,true),passage(3,true,false,true),
        passage(4,true,true),passage(4,true,true),passage(5,false,true)};
    regions[2].key+="/continuation";
    regions[5].feedback=regions[6].feedback=true;
    regions[5].correct=regions[6].correct=true;regions[6].key+="/feedback-continuation";
    const reading_gate::FeedbackPriority priority{4,5};

    require(!reading_gate::exercise_rewind_target(regions,0,false),"An earlier introduction was rewound by a future ordinary puzzle");
    require(!reading_gate::exercise_rewind_target(regions,2,false),"An ordinary puzzle could not narrate its own split group");
    require(reading_gate::exercise_rewind_target(regions,3,false)==1,"A late ordinary bookmark entered following prose");
    require(reading_gate::exercise_rewind_target(regions,2,true)==1,"A finished bookmark inside an ordinary group was accepted");
    require(reading_gate::exercise_rewind_target(regions,7,true)==1,"A finished mixed page skipped its earliest ordinary puzzle");
    require(!reading_gate::prefix_admitted(regions,3,0)&&!reading_gate::prefix_admitted(regions,3,0,{},true),
        "The group's non-exercise tail or manual advance hid its pending ordinary requirement");
    for(const auto target:{5u,6u})require(reading_gate::prefix_admitted(regions,target,0,priority),
        "A fresh later answer lost admission inside its own multi-segment feedback");
    for(const auto target:{7u,8u})for(const bool manual:{false,true})
        require(!reading_gate::prefix_admitted(regions,target,0,priority,manual),
            "Leaving fresh feedback bypassed a preceding pending ordinary puzzle");

    regions[1].solved=true;
    require(reading_gate::exercise_rewind_target(regions,7,false)==4,"A completed ordinary puzzle credited a later compiler");
    require(!reading_gate::exercise_rewind_target(regions,3,false),"An unrelated later compiler blocked earlier prose");
    require(!reading_gate::prefix_admitted(regions,7,0,priority,true),"A fresh feedback priority bypassed the remaining compiler");
    regions[4].solved=true;
    require(!reading_gate::exercise_rewind_target(regions,7,true),"A fully solved mixed page was rewound");
    require(!reading_gate::prefix_admitted(regions,7,0,priority),"Unheard current correct feedback was skipped");
    require(reading_gate::prefix_admitted(regions,7,0,priority,true),"Manual advance inside the active feedback regressed");
    require(reading_gate::prefix_admitted(regions,8,7),"A solved and heard mixed prefix did not release later prose");

    // The current answer's permission does not manufacture hearing of another
    // earlier correct feedback, even though both puzzles are already solved.
    regions[1].feedback=true;regions[1].correct=true;
    require(reading_gate::prefix_admitted(regions,5,0,priority),"A later verdict could not interrupt earlier unheard feedback");
    require(!reading_gate::prefix_admitted(regions,7,0,priority,true),"Manual current-feedback advance credited older unheard feedback");
    require(reading_gate::prefix_admitted(regions,7,2,priority,true),"Actually heard earlier feedback remained blocked");

    std::vector<Part> planned;for(const auto& region:regions)planned.push_back({region});
    regions[1].solved=false;
    require(reading_gate::reconcile(planned,regions,projection),"Ordinary completion metadata could not reconcile without changing speech");
    require(reading_gate::exercise_rewind_target(planned,7,false,projection)==1&&
        !reading_gate::prefix_admitted(planned,7,8,priority,true,projection),
        "A late asynchronous plan reinstated solved metadata for an ordinary puzzle");
}
}
int main() {
    try {
        current_objectives();grouped_feedback();asynchronous_metadata();preview_from_later_feedback();ordinary_and_mixed_requirements();
        std::cout<<"PASS current U2L6 objectives, ordinary/mixed groups, late/finished bookmarks, local feedback permission and asynchronous same-script invalidation; pure gates only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
