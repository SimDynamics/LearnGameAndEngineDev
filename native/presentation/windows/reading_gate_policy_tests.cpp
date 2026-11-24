#include "presentation/windows/reading_gate_policy.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/catalog_loader.hpp"
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
    const auto& first=page.blocks.at(2);
    const auto& second=page.blocks.at(6);
    // The active edition must use the revised second editor. The archived
    // editor is tested below, never accepted as a live replacement.
    require(first.stable_id=="block_0006_0002"&&
            second.stable_id=="p1u2l6_reparar_conversion",
        "The current U2L6 compiler identities reverted to the archive");
    require(first.output_check&&second.output_check&&
        first.output_check->cases.size()==1&&second.output_check->cases.size()==1&&
        std::ranges::find(first.output_check->cases[0].outputs,
            "Entero: 7\nDecimal: 7.5")!=first.output_check->cases[0].outputs.end(),
        "First U2L6 output objective changed");
    const auto& second_outputs=second.output_check->cases[0].outputs;
    require(std::ranges::find(second_outputs,
        "Entero: 4\nDecimal: 4.25")!=second_outputs.end(),
        "The revised U2L6 second editor lost its original output objective");
    require(std::ranges::any_of(lesson.activity_after,[&](const auto& edge){
        return edge.activity==second.stable_id&&edge.prerequisites.size()==1&&
            edge.prerequisites.front()==first.stable_id;
    }),"The second U2L6 editor lost its dependency on the first");
    // Test the archived edition as an actual separate reader gate. Its
    // output and prerequisite remain historical, not current requirements.
    const auto& history=lesson.state_layout.retirement_history;
    const auto old=std::ranges::find_if(history,[](const auto& edition){
        return edition.current.size()>6&&edition.pages.size()>6&&
            edition.current[6].blocks.size()>6&&
            edition.current[6].blocks[6]=="block_0006_0006";
    });
    require(old!=history.end()&&old->pages[6].blocks.size()==7&&
        old->pages[6].blocks[2].stable_id=="block_0006_0002"&&
        old->pages[6].blocks[6].stable_id=="block_0006_0006"&&
        old->pages[6].blocks[6].output_check&&
        !old->pages[6].blocks[6].output_check->cases.empty()&&
        std::ranges::find(old->pages[6].blocks[6].output_check->cases[0].outputs,
            "Entero: 7\nDecimal: 7")!=
            old->pages[6].blocks[6].output_check->cases[0].outputs.end()&&
        std::ranges::any_of(old->activity_after,[&](const auto& edge){
            return edge.activity=="block_0006_0006"&&
                edge.prerequisites.size()==1&&
                edge.prerequisites.front()=="block_0006_0002";
        }),
        "The archived U2L6 editor or its original prerequisite was lost");
    std::vector<LessonReadingRegion> old_regions;
    for(std::size_t n=0;n<old->pages[6].blocks.size();++n) {
        const auto& block=old->pages[6].blocks[n];
        LessonReadingRegion region;region.block_index=n;
        region.key=old->pages[6].stable_id+'/'+block.stable_id;
        region.required_compiler=reading_gate::compiler_objective(block);
        region.is_exercise=region.required_compiler;
        region.solved=!region.required_compiler;
        old_regions.push_back(std::move(region));
    }
    require(old_regions[2].required_compiler&&old_regions[6].required_compiler&&
        reading_gate::compiler_rewind_target(old_regions,7,false)==2&&
        !reading_gate::prefix_admitted(old_regions,3,0),
        "The archived U2L6 first editor no longer gates reading");
    old_regions[2].solved=true;
    require(reading_gate::compiler_rewind_target(old_regions,7,false)==6&&
        !reading_gate::prefix_admitted(old_regions,7,0,{},true),
        "The archived U2L6 second editor inherited the first's answer");
    old_regions[6].solved=true;
    require(reading_gate::prefix_admitted(old_regions,7,0),
        "The two archived U2L6 editors failed to release their old page");
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
// Use the application's selected catalog, independently of the legacy tests.
void selected_edition_reader_gates() {
    const auto root=std::filesystem::path(build::source_root)/"app/src/main/assets/content";
    const curriculum::CatalogLocation course{"es","programming","cpp_fundamentals",{},{}};
    const auto units=windows::load_catalog(root,course);
    const auto unit=std::ranges::find_if(units.entries,[](const auto& entry){
        return entry.id.ends_with("__objetos_y_estado");
    });
    require(unit!=units.entries.end()&&unit->available,"Selected U2 is unavailable");
    const auto lessons=windows::load_catalog(root,curriculum::catalog_child(course,unit->id));
    const auto verify=[&](std::string_view suffix,std::string_view second_id,
                          std::string_view first_output,std::string_view second_output) {
        const auto entry=std::ranges::find_if(lessons.entries,[&](const auto& item){
            return item.id.ends_with(suffix);
        });
        require(entry!=lessons.entries.end()&&entry->available,"Selected lesson is unavailable");
        const auto lesson=windows::load_lesson(entry->asset);
        require(lesson.id==entry->id&&lesson.course_edition.has_value(),
            "Selected lesson is not the active course edition");
        const auto first=curriculum::stable_block_position(lesson,"block_0006_0002");
        const auto second=curriculum::stable_block_position(lesson,second_id);
        require(first&&second,"Selected edition lost a compiler owner");
        const auto& a=lesson.pages.at(first->page).blocks.at(first->block);
        const auto& b=lesson.pages.at(second->page).blocks.at(second->block);
        const auto has_output=[](const auto& block,std::string_view expected){
            return block.output_check&&!block.output_check->cases.empty()&&
                std::ranges::any_of(block.output_check->cases.front().outputs,
                    [&](const auto& output){return output==expected;});
        };
        require(has_output(a,first_output)&&has_output(b,second_output),
            "Selected edition changed an independently audited output");
        require(std::ranges::any_of(lesson.activity_after,[&](const auto& edge){
            return edge.activity==b.stable_id&&edge.prerequisites.size()==1&&
                edge.prerequisites.front()==a.stable_id;
        }),"Selected second editor lost its own prerequisite");
        std::vector<LessonReadingRegion> regions;
        for(const auto* owner:{&a,&b}) {
            LessonReadingRegion region;
            region.block_index=regions.size();
            region.key=lesson.id+'/'+owner->stable_id;
            region.required_compiler=reading_gate::compiler_objective(*owner);
            region.is_exercise=true;
            region.solved=false;
            regions.push_back(std::move(region));
        }
        require(regions[0].required_compiler&&regions[1].required_compiler,
            "Selected compiler objectives are not mandatory");
        require(reading_gate::compiler_rewind_target(regions,1,true)==0&&
            !reading_gate::prefix_admitted(regions,2,0,{},true),
            "Manual navigation bypassed the selected first editor");
        regions[0].solved=true;
        require(reading_gate::compiler_rewind_target(regions,1,true)==1&&
            !reading_gate::prefix_admitted(regions,2,0,{},true),
            "Selected second editor inherited completion of the first");
        regions[1].solved=true;
        require(reading_gate::prefix_admitted(regions,2,0),
            "Independent completion did not release both selected gates");
    };
    verify("__enteros_y_aproximaciones","block_0007_0009","Parte: 4.5 m","Parte: 2.75 m");
    verify("__conversiones_y_deduccion","block_0006_0006","Entero: 7\nDecimal: 7.5","Entero: 4\nDecimal: 4.25");
    std::cout<<"PASS selected U2L5/U2L6 catalog owners and independent policy gates; no execution claimed\n";
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
    require(!reading_gate::prefix_admitted(regions,regions.size(),0),
        "End-of-page admission bypassed the second compiler");
    require(!reading_gate::prefix_admitted(regions,regions.size(),0,{},true),
        "Manual navigation bypassed an unresolved second compiler");
    regions[6].solved=true;
    require(reading_gate::prefix_admitted(regions,regions.size(),0),"Both actual verified objectives failed to release the page");
    require(!reading_gate::compiler_rewind_target(regions,6,true),"A fully verified page was rewound");

    curriculum::Block optional;optional.type=curriculum::BlockType::cpp_compiler;
    require(reading_gate::compiler_objective(optional),"An editor without its evaluator became an optional curricular activity");
    optional.type=curriculum::BlockType::paragraph;
    require(!reading_gate::compiler_objective(optional),"Ordinary prose became a compiler objective");
}
void current_u2l6_reopened_pause() {
    auto regions=current_page();
    require(regions.size()==7&&regions[2].required_compiler&&regions[6].required_compiler&&
        !regions[2].solved&&!regions[6].solved,
        "The active U2L6 lesson lost an independent compiler gate");
    regions[2].solved=true;
    require(reading_gate::compiler_rewind_target(regions,6,true)==6,
        "Completing U2L6's first editor credited its second");
    const auto late=narration::Position{static_cast<std::int64_t>(regions.size()),650,0,false,
        narration::HeardPrefix{"qa-u2l6-late",650000,1000000}};
    const auto rewind=narration::seek(late,6,0);
    require(rewind.segment==6&&rewind.milliseconds==0&&
        rewind.heard_through==0&&!rewind.heard_prefix,
        "Rewinding retained a later speech certificate");
    const auto& identity=regions[6].key;
    const auto owner=identity.substr(identity.find_last_of('/')+1);
    const std::string key="qa/u2l6/read",reader="qa-u2l6";
    auto history=narration::begin({},key,reader,rewind,owner,"page_0006");
    history=narration::checkpoint(std::move(history),key,reader,rewind);
    state::StateData progress;
    narration::write_history(progress,history);
    const auto persisted=state::decode(state::encode(progress));
    const auto restored=narration::restore(narration::read_history(persisted),key,regions.size());
    require(restored&&*restored==rewind&&
        !reading_gate::prefix_admitted(regions,regions.size(),restored->heard_through,{},true)&&
        reading_gate::compiler_rewind_target(regions,6,true)==6,
        "A restored U2L6 bookmark credited an unfinished second editor");
    regions[6].solved=true;
    require(reading_gate::prefix_admitted(regions,regions.size(),restored->heard_through),
        "Two separately completed editors did not release the page");
    regions[2].solved=false;
    require(reading_gate::compiler_rewind_target(regions,6,true)==2&&
        !reading_gate::prefix_admitted(regions,regions.size(),restored->heard_through,{},true),
        "A completed later editor credited the unfinished first");
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

void current_u2l5_reader_pauses_and_gates(){
    using namespace curriculum;
    // Authored owners are real. These are policy-level spoken regions,
    // not generated speech or claims of Win32 playback.
    const auto lesson=windows::load_lesson(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/"
        "objetos_y_estado/lesson_enteros_y_aproximaciones.json");
    const auto first=stable_block_position(lesson,"block_0006_0002");
    const auto second=stable_block_position(lesson,"p1u2l5_reparar_agrupacion");
    const auto original=stable_block_position(lesson,"block_0007_0001");
    const auto repaired=stable_block_position(lesson,"p1u2l5_referencia_agrupacion");
    require(first&&second&&original&&repaired&&first->page==6&&first->block==2&&
        second->page==7&&second->block==9&&original->page==7&&repaired->page==8,
        "Current U2L5 editors or their protected reference owners moved");
    require(reading_gate::compiler_objective(lesson.pages[6].blocks[2])&&
        reading_gate::compiler_objective(lesson.pages[7].blocks[9]),
        "The two active compiler objectives lost mandatory narration gates");
    const auto region=[&](std::size_t page,std::size_t block,std::size_t part,bool solved) {
        const auto& owner=lesson.pages[page].blocks[block];
        LessonReadingRegion result;
        result.key=lesson.id+"/"+lesson.pages[page].stable_id+"/"+owner.stable_id+
            "/policy-"+std::to_string(part);
        result.block_index=block;
        result.source_text=!owner.text.empty()?owner.text:
            !owner.question.empty()?owner.question:owner.stable_id;
        result.required_compiler=reading_gate::compiler_objective(owner);
        result.is_exercise=result.required_compiler||
            owner.type==BlockType::multiple_choice||
            owner.type==BlockType::output_prediction||
            owner.type==BlockType::inline_fill_code;
        result.solved=!result.is_exercise||solved;
        return result;
    };
    std::vector<LessonReadingRegion> first_page{
        region(6,0,0,true),region(6,1,0,true),region(6,2,0,false),
        region(6,2,1,false),region(6,3,0,true),region(6,4,0,false)};
    require(reading_gate::prefix_admitted(first_page,3,0)&&
        reading_gate::compiler_rewind_target(first_page,4,false)==2&&
        !reading_gate::prefix_admitted(first_page,4,0,{},true),
        "A split first-editor prompt allowed manual seeking into protected prose");
    const auto blocked=reading_gate::prefix_blocker(first_page,4,0);
    require(blocked&&blocked->index==2&&blocked->block==first->block&&
        blocked->reason==reading_gate::BlockerReason::answer,
        "An unverified first editor was not identified as the reader barrier");

    first_page[2].solved=first_page[3].solved=true;
    auto feedback=region(6,2,2,true);
    feedback.feedback=feedback.correct=true;feedback.required_compiler=false;
    feedback.source_text="Comprobación correcta.";
    first_page.insert(first_page.begin()+4,feedback);
    const auto pending_feedback=reading_gate::prefix_blocker(first_page,5,0);
    require(pending_feedback&&pending_feedback->index==4&&
        pending_feedback->reason==reading_gate::BlockerReason::correct_feedback,
        "Correct first-editor feedback was treated as already heard");
    const auto late=narration::Position{5,460,0,false,
        narration::HeardPrefix{std::string(64,'a'),460000,1000000}};
    const auto rewound=narration::seek(late,4,0);
    require(rewound.segment==4&&rewound.milliseconds==0&&
        rewound.heard_through==0&&!rewound.heard_prefix&&
        !reading_gate::prefix_admitted(first_page,5,rewound.heard_through),
        "Seeking backwards forged an audio-hearing certificate");
    const std::string key="qa/u2l5/reader-six",reader="qa-u2l5";
    auto history=narration::begin({},key,reader,rewound,
        "block_0006_0002",lesson.pages[6].stable_id);
    history=narration::checkpoint(std::move(history),key,reader,rewound);
    state::StateData progress;
    narration::write_history(progress,history);
    const auto saved=state::decode(state::encode(progress));
    const auto restored=narration::restore(narration::read_history(saved),key,first_page.size());
    require(restored&&*restored==rewound&&
        !reading_gate::prefix_admitted(first_page,5,restored->heard_through),
        "Reopening a pause skipped the first editor's unheard explanation");
    const auto heard=narration::Position{4,0,5,false};
    history=narration::checkpoint(std::move(history),key,reader,heard);
    narration::write_history(progress,history);
    require(reading_gate::prefix_admitted(first_page,5,heard.heard_through)&&
        !reading_gate::prefix_admitted(first_page,first_page.size(),heard.heard_through),
        "Hearing one explanation bypassed the still-unanswered following choice");
    first_page.back().solved=true;
    require(reading_gate::prefix_admitted(first_page,first_page.size(),heard.heard_through),
        "The completed first editor and choice failed to release the page");

    std::vector<LessonReadingRegion> next_page;
    for(std::size_t index=0;index<=second->block;++index)
        next_page.push_back(region(7,index,0,index!=second->block));
    require(next_page.back().required_compiler&&
        reading_gate::compiler_rewind_target(next_page,next_page.size()-1,true)==second->block&&
        !reading_gate::prefix_admitted(next_page,next_page.size(),0,{},true),
        "First-editor success or a finished bookmark skipped the second editor");
    std::vector<Part> planned;for(const auto& item:next_page)planned.push_back({item});
    planned.back().region.solved=true;
    require(reading_gate::reconcile(planned,next_page,projection)&&
        !planned.back().region.solved&&
        reading_gate::compiler_rewind_target(planned,next_page.size()-1,true,projection)==second->block,
        "A late narration plan restored stale second-editor completion");
    auto altered=next_page;altered[1].source_text+=" Nueva lectura.";
    require(!reading_gate::reconcile(planned,altered,projection)&&
        !planned.back().region.solved,
        "Changed speech entered a metadata-only plan reconciliation");

    next_page.back().solved=true;
    auto next_feedback=region(7,second->block,1,true);
    next_feedback.feedback=next_feedback.correct=true;
    next_feedback.required_compiler=false;
    next_feedback.source_text="Segunda comprobación correcta.";
    next_page.push_back(next_feedback);
    require(!reading_gate::prefix_admitted(next_page,next_page.size(),0)&&
        reading_gate::prefix_blocker(next_page,next_page.size(),0)->reason==
            reading_gate::BlockerReason::correct_feedback,
        "Second-editor success skipped its separately unheard feedback");
    history=narration::begin(std::move(history),"qa/u2l5/reader-seven",reader,
        {static_cast<std::int64_t>(next_page.size()-1),0,0,false},
        "p1u2l5_reparar_agrupacion",lesson.pages[7].stable_id);
    narration::write_history(progress,history);
    const auto reopened=state::decode(state::encode(progress));
    const auto bookmarks=narration::read_history(reopened);
    const auto six=narration::restore(bookmarks,key,first_page.size());
    const auto seven=narration::restore(bookmarks,"qa/u2l5/reader-seven",next_page.size());
    require(six&&seven&&six->heard_through==5&&seven->heard_through==0&&
        !reading_gate::prefix_admitted(next_page,next_page.size(),seven->heard_through)&&
        reading_gate::prefix_admitted(next_page,next_page.size(),
            static_cast<std::int64_t>(next_page.size())),
        "Reopening one page overwrote another bookmark or pretended to hear feedback");
    std::cout<<"PASS current U2L5 reader policy: split prompts, feedback waits, "
        "state-coded pauses, second-editor gate and late-plan reconciliation; "
        "no audio synthesis or UI execution claimed\n";
}

void cadenas_three_puzzle_flow(){
    using namespace curriculum;
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_cadenas_y_texto.json";
    const auto lesson=windows::load_lesson(path);const auto& page=lesson.pages.at(2);
    require(page.stable_id=="page_0002"&&page.blocks.at(2).stable_id=="block_0002_0002"&&
        page.blocks.at(3).stable_id=="block_0002_0003"&&page.blocks.at(4).stable_id=="block_0002_0004"&&
        page.blocks[2].type==BlockType::output_prediction&&page.blocks[3].type==BlockType::fill_code&&
        page.blocks[4].type==BlockType::multiple_choice&&page.blocks[4].options.size()==3,
        "The reported U2L7 page or its three actual puzzle contracts changed");
    std::vector<LessonReadingRegion> regions;
    const auto add=[&](std::size_t block,std::string source,bool feedback=false){
        LessonReadingRegion region;region.key=page.blocks[block].stable_id+'/'+std::to_string(regions.size());
        region.block_index=block;region.source_text=std::move(source);region.is_exercise=block>=2;
        region.solved=block!=4;region.feedback=feedback;region.correct=feedback;regions.push_back(std::move(region));
    };
    add(0,page.blocks[0].text);add(1,page.blocks[1].text);
    add(2,page.blocks[2].question);const auto prediction_prompt=regions.size()-1;
    add(2,"Ejercicio de código C más más.");regions.back().passive_label=true;
    const auto prediction_feedback=regions.size();add(2,"Correcto",true);add(2,page.blocks[2].explanation,true);
    const auto prediction_heard=regions.size();
    add(3,page.blocks[3].question);add(3,"Ejercicio de código C más más.");regions.back().passive_label=true;
    const auto fill_feedback=regions.size();add(3,"Correcto",true);add(3,page.blocks[3].explanation,true);
    const auto sur_prompt=regions.size();add(4,page.blocks[4].question);
    for(const auto& option:page.blocks[4].options)add(4,option);
    const reading_gate::FeedbackPriority fill_priority{3,sur_prompt};
    // This is the reported adjacent transition, with the earlier prediction
    // actually solved and heard. The pending next puzzle must narrate its
    // prompt and every option before its own answer gate closes.
    for(auto at=sur_prompt;at<regions.size();++at)
        require(!reading_gate::prefix_blocker(regions,at,static_cast<std::int64_t>(prediction_heard),fill_priority)&&
            reading_gate::prefix_admitted(regions,at,static_cast<std::int64_t>(prediction_heard),fill_priority),
            "A solved/heard prefix stopped before the Sur prompt or one of its three options");
    const auto pending=reading_gate::prefix_blocker(regions,regions.size(),static_cast<std::int64_t>(prediction_heard),fill_priority);
    require(pending&&pending->reason==reading_gate::BlockerReason::answer&&pending->index==sur_prompt&&pending->block==4,
        "The next activity did not wait only after its complete spoken prompt/options");
    // A fresh later answer may interrupt to read only its own feedback. If an
    // earlier correct guide was never heard, identify that exact guide rather
    // than mislabelling the already-correct later exercise as unanswered.
    require(reading_gate::prefix_admitted(regions,fill_feedback,0,fill_priority),"The fill's fresh feedback permission was lost");
    const auto unheard=reading_gate::prefix_blocker(regions,sur_prompt,0,fill_priority);
    require(unheard&&unheard->reason==reading_gate::BlockerReason::correct_feedback&&unheard->index==prediction_feedback&&unheard->block==2,
        "A previous unheard guide was reported as the later pending answer");
    narration::Position position{static_cast<std::int64_t>(sur_prompt-1),900,0,false,
        narration::HeardPrefix{"qa-later-guide",900000,900000}};
    const auto redirected=narration::seek(position,static_cast<std::int64_t>(unheard->index),0);
    require(redirected.segment==static_cast<std::int64_t>(prediction_feedback)&&redirected.milliseconds==0&&
        redirected.heard_through==position.heard_through&&!redirected.heard_prefix&&!redirected.finished,
        "Returning to an unheard guide fabricated offset, certificate or prior listening credit");
    require(reading_gate::prefix_admitted(regions,sur_prompt,static_cast<std::int64_t>(prediction_heard),fill_priority),
        "Hearing the actual earlier guide failed to release the already-heard fill and next prompt");
    // A genuinely unanswered earlier activity still wins; no new permission
    // skips it merely because a later answer has been checked correctly.
    for(auto& region:regions)if(region.block_index==2){region.solved=false;region.feedback=false;region.correct=false;}
    const auto earlier=reading_gate::prefix_blocker(regions,sur_prompt,0,fill_priority);
    require(earlier&&earlier->reason==reading_gate::BlockerReason::answer&&earlier->index==prediction_prompt&&earlier->block==2,
        "A fresh later answer bypassed the actual pending prediction");
    for(auto at=prediction_prompt;at<prediction_feedback;++at)
        require(reading_gate::prefix_admitted(regions,at,0,fill_priority),"The pending prediction could not narrate its own prompt/caption");
    // The recorded model still contains its old feedback slots, now belonging
    // to the same pending group. Waiting at that group's end cannot rewind
    // and loop its prompt; the reader distinguishes the block owner.
    const auto same=reading_gate::prefix_blocker(regions,prediction_heard,0,fill_priority);
    require(same&&same->reason==reading_gate::BlockerReason::answer&&same->block==2,
        "An ordinary pending group's end lost its same-owner answer gate");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--u2l6-current-archived") {
            current_objectives();
            current_u2l6_reopened_pause();
            std::cout<<"PASS active U2L6 two independent compiler gates, archived historical gate, manual advance and restored bookmark\n";
            return 0;
        }
        if(argc==2&&std::string_view(argv[1])=="--selected-edition") {
            selected_edition_reader_gates();return 0;
        }
        require(argc==1,"Unknown reading gate QA arguments");
        selected_edition_reader_gates();
        current_objectives();current_u2l6_reopened_pause();grouped_feedback();asynchronous_metadata();preview_from_later_feedback();ordinary_and_mixed_requirements();cadenas_three_puzzle_flow();current_u2l5_reader_pauses_and_gates();
        std::cout<<"PASS current U2L6 objectives and restored pauses, actual U2L7 three-puzzle prompt/feedback gates, ordinary/mixed groups, late/finished bookmarks, local feedback permission and asynchronous same-script invalidation; pure gates only\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
