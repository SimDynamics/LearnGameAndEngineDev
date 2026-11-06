#include "core/catalog.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/choice_revision_test_fixture.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Action> void rejects(Action action,const char* message) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; } check(failed,message);
}
void page_progress_matches_android_without_crediting_unanswered_puzzles() {
    using namespace lgaed;
    using namespace curriculum;
    Lesson lesson; lesson.id = "legacy";
    Block theory; theory.type = BlockType::paragraph; theory.stable_id = "theory";
    Block quiz; quiz.type = BlockType::multiple_choice; quiz.stable_id = "quiz";
    lesson.pages = {Page{"p0",{theory}},Page{"p1",{quiz}},Page{"p2",{theory}}};
    state::StateData data;
    data.preferences["learning_progress"]["lesson_legacy_page"] = std::int64_t{2};
    const auto unanswered = [](const Block&) { return false; };
    check(catalog_lesson_page_progress(data,lesson,unanswered).completed == 0,
        "A saved navigation position alone manufactured lesson progress");
    data.preferences["learning_progress"]["lesson_legacy_started"] = true;
    const auto theory_read = catalog_lesson_page_progress(data,lesson,unanswered);
    check(theory_read.completed == 2 && theory_read.total == 3 && theory_read.percentage() == 66,
        "Legacy theory pages no longer follow Android's explicit started/current-page rule");
    data.preferences["learning_progress"]["lesson_legacy_completed"] = true;
    check(catalog_lesson_page_progress(data,lesson,unanswered).completed == 2,
        "A lesson flag credited an unanswered exercise in page progress");
    check(catalog_lesson_page_progress(data,lesson,[](const Block&) { return true; }).completed == 3,
        "A verified exercise did not complete its original page");
}
void dependent_puzzles_keep_historical_progress_without_granting_execution_proofs() {
    using namespace lgaed; using namespace curriculum;
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValue;
    const auto path = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json";
    const auto original = test::load_pre_retirement_lesson(path.filename().string());
    auto root = JsonObject::Parse(winrt::to_hstring(original.raw_json));
    root.Insert(L"activityAfter",JsonValue::Parse(LR"([{"activity":"block_0004_0004","requires":["ejecucion_original_editor","block_0004_0002"]}])"));
    const auto projected = windows::parse_lesson_progress_metadata(winrt::to_string(root.Stringify()));
    state::StateData data;
    data.preferences["learning_progress"]["lesson_"+original.id+"_started"] = true;
    for (const auto& page : original.pages) for (const auto& block : page.blocks) {
        if (block.type == BlockType::multiple_choice || block.type == BlockType::output_prediction ||
            block.type == BlockType::fill_code || block.type == BlockType::order_code)
            state::put_response(data,exercise_key(original,block),state::Response{"Historical exact answer",true});
    }
    const auto preserved = data;
    const auto correct = [&](const Block& block) {
        const auto response = state::get_response(data,exercise_key(projected,block));
        return response && response->correct;
    };
    const auto before = catalog_lesson_page_progress(data,original,correct);
    const auto after = catalog_lesson_page_progress(data,projected,correct);
    check(before.completed > 0 && before.completed == after.completed && before.total == after.total && data == preserved,
        "Adding a dependency changed historical puzzle progress or wrote student data during projection");
    check(!projected.compiler_puzzles_required && projected.activity_after.size() == 1 &&
        projected.reference_after.size() == original.reference_after.size() &&
        projected.pages[4].blocks[3].output_check && projected.pages[4].blocks[6].output_check,
        "The catalog discarded compiler source goals or existing references outside mandatory mode");
    check(!windows::compiler_puzzle_completed(data,projected,projected.pages[4].blocks[3]) &&
        !windows::compiler_puzzle_completed(data,projected,projected.pages[4].blocks[6]),
        "A historical correct puzzle answer granted execution authorization to its prerequisites");
    check(state::get_response(data,exercise_key(projected,projected.pages[4].blocks[7]))->answer == "Historical exact answer",
        "A protected puzzle lost its exact historical response");
}
void compiler_puzzles_keep_captured_success_until_explicit_reset() {
    using namespace lgaed;using namespace curriculum;
    auto lesson=windows::parse_lesson_json(R"({"id":"qa_compiler_completion","title":"QA","compilerPuzzlesRequired":true,"pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"original","initialStandardInput":"1","outputCheck":{"cases":[{"standardInput":"1","outputs":["2"]},{"standardInput":"2","outputs":["4"]}]}}]}]})");
    const auto& block=lesson.pages[0].blocks[0];state::StateData data;
    state::put_response(data,exercise_key(lesson,block),state::Response{"",true});
    data.preferences["learning_progress"]["lesson_"+lesson.id+"_started"]=true;
    data.preferences["learning_progress"]["lesson_"+lesson.id+"_completed"]=true;
    const auto progress=[&]{return catalog_lesson_page_progress(data,lesson,[&](const Block& item){return windows::compiler_puzzle_completed(data,lesson,item);});};
    check(!windows::compiler_puzzle_completed(data,lesson,block)&&progress().completed==0,
        "An old correct flag or completed page manufactured a compiler execution proof");
    windows::CppRunRequest captured;captured.revision=7;captured.source_utf8="same captured program";captured.stdin_utf8="1";
    FreshExecution execution{7,true,true,0,"1","2"};
    auto stale=execution;stale.source_revision=6;
    check(!windows::record_compiler_puzzle_case(data,lesson,block,captured,stale,"Correcto"),"A stale result accredited a compiler objective");
    check(!windows::record_compiler_puzzle_case(data,lesson,block,captured,execution,"Correcto")&&windows::compiler_puzzle_case_count(data,lesson,block)==1,
        "One input completed a two-case compiler puzzle");
    auto edited=captured;edited.source_utf8+=" changed";
    windows::invalidate_compiler_puzzle_attempt(data,lesson,block,edited);
    windows::invalidate_compiler_puzzle_attempt(data,lesson,block,captured);
    check(windows::compiler_puzzle_case_count(data,lesson,block)==0,"Undo resurrected an invalidated partial execution proof");
    check(!windows::record_compiler_puzzle_case(data,lesson,block,captured,execution,"Correcto"),"The first case completed both inputs");
    captured.stdin_utf8="2";windows::invalidate_compiler_puzzle_attempt(data,lesson,block,captured);
    check(windows::compiler_puzzle_case_count(data,lesson,block)==1,"Changing only input discarded an unchanged program's independent case");
    execution.standard_input="2";execution.standard_output="4";
    check(windows::record_compiler_puzzle_case(data,lesson,block,captured,execution,"Correcto\nExplicación comprobada.")&&progress().completed==1,
        "Both actual matching cases did not complete the opted-in compiler page");
    const auto sealed=data;
    windows::invalidate_compiler_puzzle_attempt(data,lesson,block,edited);
    auto failed=execution;failed.standard_input=edited.stdin_utf8;failed.standard_output="wrong";failed.compilation_succeeded=false;
    check(!windows::record_compiler_puzzle_case(data,lesson,block,edited,failed,"Incorrecto")
        &&windows::compiler_puzzle_completed(data,lesson,block)&&windows::compiler_puzzle_case_count(data,lesson,block)==2
        &&windows::compiler_puzzle_feedback(data,lesson,block)=="Correcto\nExplicación comprobada."&&data==sealed,
        "A failed new compilation reused historical success as its verdict or altered the independent captured achievement");
    const auto restored=state::decode(state::encode(data));
    check(windows::compiler_puzzle_completed(restored,lesson,block),"Reopening discarded a real completed compiler objective");
    auto changed=lesson;changed.pages[0].blocks[0].output_check->cases[0].outputs={"another goal"};
    check(!windows::compiler_puzzle_completed(data,changed,changed.pages[0].blocks[0]),"A changed objective reused another contract's receipt");
    const auto before=data.preferences.at("learning_progress");
    windows::reset_compiler_puzzle(data,lesson,block);
    check(!windows::compiler_puzzle_completed(data,lesson,block)&&progress().completed==0&&data.preferences.at("learning_progress")==before,
        "Reset retained the compiler achievement or altered independent answers");
}
void required_compile_diagnostics_remain_pending_in_catalog_progress() {
    using namespace lgaed;using namespace curriculum;
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido";
    for(const auto& plan:test::compile_check_cases) {
        const auto original=test::load_pre_compile_check_lesson(plan.filename);
        const auto integrated=windows::load_lesson(folder/plan.filename);
        const auto live=stable_block_position(integrated,plan.identity);
        check(live.has_value(),"The integrated catalog diagnostic lost its stable owner");
        const auto& authored=integrated.pages[live->page].blocks[live->block];
        check(authored.compile_check&&authored.compile_check->diagnostic==plan.diagnostic&&!authored.compile_check->explanation.empty(),
            "The catalog diagnostic no longer has its current authored contract and explanation");
        const auto lesson=test::with_compile_check(original,plan.identity,plan.diagnostic,authored.compile_check->explanation);
        const auto position=stable_block_position(lesson,plan.identity);
        check(position.has_value()&&lesson.compiler_puzzles_required,"Catalog diagnostic QA lost its required original editor");
        const auto& diagnostic=lesson.pages[position->page].blocks[position->block];
        state::StateData data;
        auto& values=data.preferences["learning_progress"];const auto prefix="lesson_"+lesson.id;
        values[prefix+"_started"]=true;values[prefix+"_completed"]=true;
        std::vector<std::string> pages,blocks;
        for(const auto& page:lesson.state_layout.origin)pages.push_back(page.id);
        for(const auto& page:lesson.pages)for(const auto& block:page.blocks)blocks.push_back(block.stable_id);
        values[prefix+"_read_pages_v2_es"]=pages;values[prefix+"_read_blocks_v2_es"]=blocks;
        state::put_response(data,lesson,diagnostic,state::Response{"historical opaque success",true});
        const auto retained=data;bool asked=false;
        // This unit isolates catalog grouping with its completion callback. It
        // writes no compiler receipts and does not claim a native compilation.
        const auto pending=[&](const Block& block){if(block.stable_id==plan.identity){asked=true;return false;}return true;};
        const auto before=catalog_lesson_page_progress(data,lesson,pending);
        check(asked&&before.completed+1==before.total,
            "A visited/completed page or opaque old answer bypassed a required compile diagnostic in the catalog");
        const auto verified=catalog_lesson_page_progress(data,lesson,[](const Block&){return true;});
        check(verified.completed==verified.total&&verified.total==before.total,
            "A supplied completed diagnostic failed to complete its historical catalog group");
        const auto metadata=windows::parse_lesson_progress_metadata(lesson.raw_json);
        check(catalog_lesson_page_progress(data,metadata,pending).completed==before.completed,
            "Catalog metadata dropped the pending compile diagnostic requirement");
        auto optional=lesson;optional.compiler_puzzles_required=false;
        check(catalog_lesson_page_progress(data,optional,pending).completed==verified.completed,
            "An optional diagnostic changed historical catalog counting without mandatory mode");
        check(data==retained&&!windows::compiler_puzzle_completed(data,lesson,diagnostic),
            "Inspecting catalog progress changed student data or manufactured diagnostic proof");
    }
    std::cout<<"PASS five required compile diagnostics in catalog: opaque flags/read coverage stay pending, completion callback and metadata preserve groups, no native proof invented\n";
}
void stable_progress_groups_survive_splits_reordering_and_new_activities() {
    using namespace lgaed;
    using namespace curriculum;
    Lesson lesson; lesson.id = "stable"; lesson.locale = "es";
    const auto make = [](BlockType type,const char* id) { Block block; block.type=type;block.stable_id=id;return block; };
    lesson.pages = {Page{"page_b",{make(BlockType::multiple_choice,"a_quiz"),make(BlockType::paragraph,"b_theory")}},
        Page{"page_a",{make(BlockType::paragraph,"a_theory")}}};
    lesson.state_layout.version=2;lesson.state_layout.locale="es";
    lesson.state_layout.origin={{"page_a",{"a_theory","a_quiz"}},{"page_b",{"b_theory"}}};
    lesson.state_layout.current={{"page_b",{"a_quiz","b_theory"}},{"page_a",{"a_theory"}}};
    state::StateData data;
    auto& values=data.preferences["learning_progress"];
    values["lesson_stable_started"]=true;
    values["lesson_stable_page"]=std::int64_t{1};
    values["lesson_stable_read_pages_v2_es"]=std::vector<std::string>{"page_a"};
    values["lesson_stable_read_blocks_v2_es"]=std::vector<std::string>{"a_theory"};
    const auto before=data;
    check(catalog_lesson_page_progress(data,lesson,[](const Block&) { return false; }).completed==0,
        "Reordered theory or saved display page completed an unread historical group");
    const auto correct_original=[](const Block& block) { return block.stable_id=="a_quiz"; };
    check(catalog_lesson_page_progress(data,lesson,correct_original).completed==1,
        "A moved exercise was counted against its display page instead of its historical group");
    values["lesson_stable_read_blocks_v2_es"]=std::vector<std::string>{"a_theory","b_theory"};
    check(catalog_lesson_page_progress(data,lesson,correct_original).completed==2,
        "Stable read coverage did not complete the theory-only group");
    auto addition=make(BlockType::math_puzzle,"new_quiz");
    lesson.pages[1].blocks.push_back(addition);
    lesson.state_layout.current[1].blocks.push_back("new_quiz");
    lesson.state_layout.additions.push_back({"new_quiz","",{2,0},"page_b"});
    check(catalog_lesson_page_progress(data,lesson,correct_original).completed==1,
        "A new activity inherited completed theory progress without being answered");
    check(catalog_lesson_page_progress(data,lesson,[](const Block&) { return true; }).completed==2,
        "A verified added activity did not complete its assigned historical group");
    values["lesson_stable_read_pages_v2_es"]=std::vector<std::string>{};
    check(catalog_lesson_page_progress(data,lesson,[](const Block&) { return true; }).completed==2,
        "Exercise-bearing groups incorrectly require a theory-page visitation marker");
    auto unstarted=before;unstarted.preferences["learning_progress"]["lesson_stable_started"]=false;
    check(catalog_lesson_page_progress(unstarted,lesson,[](const Block&) { return false; }).completed==0,
        "Unstarted stable theory was credited from a stored page index");
    check(state::integer_value(before,"learning_progress","lesson_stable_page")==1,
        "Progress inspection rewrote navigation");
}
void unit_percentage_weights_lessons_like_android_and_keeps_completion_counts() {
    using namespace lgaed;
    using namespace curriculum;
    const CatalogScope scope{{},{},{"one","two","three"}};
    state::StateData data;
    data.preferences["learning_progress"]["lesson_one_completed"]=true;
    const auto before=data;
    const std::vector<CatalogProgress> lessons{{3,3},{17,20},{0,5}};
    const auto progress=catalog_unit_learning_progress(data,scope,lessons);
    check(progress.completed==1 && progress.total==3 && progress.learning_completed==185 &&
        progress.learning_total==300 && progress.percentage()==61,
        "Unit percentage lost Android's equal lesson weighting or truncated per-lesson percentages");
    check(catalog_progress(data,scope).percentage()==33,"Course bars stopped counting fully completed lessons");
    const auto unequal=catalog_unit_learning_progress(data,{{},{},{"one","two"}},std::vector<CatalogProgress>{{1,1},{0,100}});
    check(unequal.percentage()==50,"A long lesson incorrectly outweighed a short lesson in unit progress");
    auto pending=lessons;pending[1].pending_validation=true;
    check(catalog_unit_learning_progress(data,scope,pending).pending_validation,
        "A pending authoritative math validation was hidden by aggregation");
    check(data==before,"Calculating catalog percentages mutated student data");
    rejects([&] { (void)catalog_unit_learning_progress(data,scope,std::vector<CatalogProgress>{{1,1}}); },
        "A partial lesson list manufactured a unit percentage");
}
void catalog_metadata_does_not_claim_cmake_execution_or_round_math_payloads() {
    using namespace lgaed;
    const std::string project=R"({"id":"project","title":"Project","pages":[{"blocks":[{"type":"cpp_compiler","cmake":{"targets":["example"]},"projectFiles":[]},{"type":"multiple_choice","question":"Question","options":["A","B"],"correctIndex":1}]}]})";
    rejects([&] { (void)windows::parse_lesson_json(project); },"Runtime loader accidentally enabled unsupported CMake execution");
    const auto metadata=windows::parse_lesson_progress_metadata(project);
    check(metadata.id=="project" && metadata.pages.size()==1 && metadata.pages[0].blocks.size()==2 &&
        metadata.pages[0].blocks[0].type==curriculum::BlockType::cpp_compiler &&
        metadata.pages[0].blocks[0].raw_json.find("projectFiles")!=std::string::npos,
        "Metadata-only catalog projection lost a future compiler activity");
    const std::string math=R"({"id":"math","title":"Math","pages":[{"blocks":[{"type":"math_puzzle","prompt":"Exact value","responses":[],"value":9007199254740993,"decimal":0.1234567890123456789}]}]})";
    const auto exact=windows::parse_lesson_progress_metadata(math);
    check(exact.pages[0].blocks[0].raw_json.find("9007199254740993")!=std::string::npos &&
        exact.pages[0].blocks[0].raw_json.find("0.1234567890123456789")!=std::string::npos,
        "Catalog metadata rounded the mathematical validator's original source tokens");
    const auto reviewed_source=R"({"id":"reviewed","title":"Reviewed","pages":[{"blocks":[{"type":"multiple_choice","question":"Q","options":["A","B"],"correctIndex":1,"reviewFeedback":["Try again","Correct choice"]}]}]})";
    const auto reviewed=windows::parse_lesson_progress_metadata(reviewed_source);
    check(reviewed.pages[0].blocks[0].correct_index==1&&reviewed.pages[0].blocks[0].raw_json.find("reviewFeedback")!=std::string::npos,
        "Catalog metadata interpreted valid editorial feedback arrays as a runtime string or dropped original JSON");
    const auto runtime_reviewed=windows::parse_lesson_json(reviewed_source);
    check(runtime_reviewed.pages[0].blocks[0].review_feedback_options==std::vector<std::string>{"Try again","Correct choice"},
        "The runtime loader discarded original per-option review feedback");
    rejects([&]{(void)windows::parse_lesson_json(R"({"id":"reviewed","title":"Reviewed","pages":[{"blocks":[{"type":"multiple_choice","question":"Q","options":["A","B"],"correctIndex":1,"reviewFeedback":["Only one"]}]}]})");},
        "An incomplete per-option feedback array was silently attached to the wrong answer");
    rejects([&] { (void)windows::parse_lesson_progress_metadata(R"({"id":"x","title":"X","pages":[{"blocks":[{"type":"unknown"}]}]})"); },
        "Unknown activity metadata was silently credited as theory");
}
void choice_revision_reset_keeps_exact_scope_and_provenance(){
    using namespace lgaed;
    const auto& plan=test::choice_revision_cases().front();
    const auto integrated=windows::load_lesson(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos"/plan.filename);
    const auto original=test::choice_revision_before(integrated,plan.identity);
    const auto revised=test::revise_choice_lesson(original,plan.identity);const auto at=curriculum::stable_block_position(revised,plan.identity);
    check(at.has_value(),"Choice reset fixture lost its activity");const auto& block=revised.pages[at->page].blocks[at->block];
    const auto legacy=curriculum::exercise_key(revised,block),current=state::response_storage_key(revised,block);
    state::StateData data;state::prepare_lesson_state(data,revised);
    state::Response response;response.correct=true;response.selected=static_cast<std::int64_t>(*block.correct_index);
    state::put_response(data,legacy,response);state::put_response(data,revised,block,response);
    std::vector<std::string> removed{legacy,current};
    for(const auto locale:{"en","fr"})removed.push_back("choice-options/v1/"+std::string(locale)+'/'+std::string(64,'a')+'/'+legacy);
    removed.push_back("order-lines/v1/es/"+std::string(64,'b')+'/'+legacy);
    for(const auto& key:removed)if(!state::find(data,"learning_progress",key))data.preferences["learning_progress"][key]=std::string("explicit reset target");
    const auto foreign="exercise_"+original.id+"_extra_page_0_block_0";
    const std::vector<std::string> preserved{
        "choice-options/v1/es/"+std::string(64,'a')+'/'+foreign,
        "choice-options/v1/de/"+std::string(64,'a')+'/'+legacy,
        "choice-options/v1/es/"+std::string(64,'g')+'/'+legacy,
        "choice-options/v1/es/"+std::string(64,'A')+'/'+legacy,
        "choice-options/v1/es/short/"+legacy};
    for(const auto& key:preserved)data.preferences["learning_progress"][key]=std::string("preserve unrecognized or foreign state");
    data.preferences["cpp_lesson_drafts"]["programming/"+original.id+"/0/0/source"]=std::string("student source intact\r\n");
    data.preferences["narration_settings"]["voice"]=std::string("ef_dora");
    const auto before=data;curriculum::CatalogScope scope;scope.lessons={original.id};
    check(curriculum::reset_catalog_progress(data,scope)==removed.size(),"Catalog reset did not remove exactly the scoped answer editions");
    for(const auto& key:removed)check(!state::find(data,"learning_progress",key),"Catalog reset retained a scoped choice/order answer");
    for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values)
        if(std::ranges::find(removed,key)==removed.end())check(state::find(data,domain,key)&&*state::find(data,domain,key)==value,
            "Catalog reset changed a choice revision pin, unrelated answer, draft or preference");
    check(!state::get_response(data,revised,block),"Catalog reset restored a historical choice achievement");
    std::cout<<"PASS choice revision catalog reset: current/original/locales removed, exact scope, archive pins, drafts and unknown keys retained\n";
}
}
int main() {
    using namespace lgaed;
    using namespace curriculum;
    try {
        page_progress_matches_android_without_crediting_unanswered_puzzles();
        dependent_puzzles_keep_historical_progress_without_granting_execution_proofs();
        compiler_puzzles_keep_captured_success_until_explicit_reset();
        required_compile_diagnostics_remain_pending_in_catalog_progress();
        stable_progress_groups_survive_splits_reordering_and_new_activities();
        unit_percentage_weights_lessons_like_android_and_keeps_completion_counts();
        catalog_metadata_does_not_claim_cmake_execution_or_round_math_payloads();
        choice_revision_reset_keeps_exact_scope_and_provenance();
        const auto root = std::filesystem::path(build::source_root) / "app/src/main/assets/content";
        CatalogLocation location;
        const auto routes = windows::load_catalog(root,location);
        check(routes.entries.size() == 2 && routes.entries[0].id == "programming" && routes.entries[1].id == "math_physics","Real route order changed");
        location = catalog_child(location,"programming"); const auto courses = windows::load_catalog(root,location);
        check(courses.entries.size() == 44 && courses.entries[0].id == "cpp_fundamentals" && courses.entries.back().number == 44,"Future indexed courses were hidden or reordered");
        check(!courses.entries.back().available,"A future course was invented as available");
        location = catalog_child(location,"cpp_fundamentals"); const auto units = windows::load_catalog(root,location);
        check(units.entries.size() == 12 && units.entries[0].id == "cpp_fundamentals__programas_y_ejecucion","Native course lost its indexed units");
        location = catalog_child(location,units.entries[0].id); const auto lessons = windows::load_catalog(root,location);
        check(lessons.entries.size() == 5 && lessons.entries[1].id == "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida","Lesson order differs from unit index");
        check(lessons.entries[1].available && lessons.entries[1].asset.filename() == "lesson_primer_programa_y_salida.json","An indexed lesson points to another source");
        state::StateData pristine;
        const auto pristine_before=pristine;
        const auto lesson_metadata=windows::load_lesson_progress_metadata(lessons.entries[1].asset,lessons.entries[1].asset_locale);
        const auto lesson_card=windows::catalog_entry_progress(root,location,lessons.entries[1],pristine);
        const auto lesson_card_cached=windows::catalog_entry_progress(root,location,lessons.entries[1],pristine);
        check(lesson_card.total==lesson_metadata.pages.size() && lesson_card.completed==0 &&
            lesson_card_cached.total==lesson_card.total && lesson_card_cached.completed==0 && pristine==pristine_before,
            "Loading/caching catalog metadata invented progress or modified the student's profile");
        const auto cold_started=std::chrono::steady_clock::now();
        const auto empty_unit=windows::catalog_entry_progress(root,catalog_parent(location),units.entries.front(),pristine);
        const auto cold_finished=std::chrono::steady_clock::now();
        const auto warm_unit=windows::catalog_entry_progress(root,catalog_parent(location),units.entries.front(),pristine);
        const auto warm_finished=std::chrono::steady_clock::now();
        check(empty_unit.total==lessons.entries.size() && empty_unit.completed==0 && empty_unit.percentage()==0 &&
            warm_unit.total==empty_unit.total && warm_unit.percentage()==empty_unit.percentage() && pristine==pristine_before,
            "A real unit's cached metadata changed its lesson weighting, counts or empty student state");
        std::cout<<"Catalog empty-unit metadata cold/warm: "
            <<std::chrono::duration_cast<std::chrono::milliseconds>(cold_finished-cold_started).count()<<"/"
            <<std::chrono::duration_cast<std::chrono::milliseconds>(warm_finished-cold_finished).count()<<" ms\n";
        auto false_owner=lessons.entries[1];false_owner.id=lessons.entries[0].id;
        rejects([&] { (void)windows::catalog_entry_progress(root,location,false_owner,pristine); },
            "A cached metadata asset was attached to another published lesson");
        auto first_lesson = location; first_lesson.lesson = lessons.entries.front().id;
        const auto same_unit = windows::next_catalog_lesson(root,first_lesson,false,false);
        check(same_unit && same_unit->lesson == lessons.entries[1].id && same_unit->unit == location.unit && same_unit->locale == location.locale,
            "Next lesson did not follow the actual indexed order and content language");
        auto unit_end = location; unit_end.lesson = lessons.entries.back().id;
        check(!windows::next_catalog_lesson(root,unit_end,false,false) && !windows::next_catalog_lesson(root,unit_end,false,true),
            "Automatic continuation crossed an unapproved unit or skipped it to another course");
        const auto next_unit = windows::next_catalog_lesson(root,unit_end,true,false);
        check(next_unit && next_unit->course == location.course && next_unit->unit == units.entries[1].id && next_unit->route == location.route,
            "Unit continuation skipped its indexed parent or changed routes");
        auto course_end = catalog_parent(location); course_end = catalog_child(course_end,units.entries.back().id);
        const auto final_lessons = windows::load_catalog(root,course_end); course_end.lesson = final_lessons.entries.back().id;
        check(!windows::next_catalog_lesson(root,course_end,true,false),"Course continuation ignored its disabled setting");
        const auto next_course = windows::next_catalog_lesson(root,course_end,true,true);
        check(next_course && next_course->route == location.route && next_course->course == courses.entries[1].id && next_course->locale == location.locale,
            "Course continuation did not preserve indexed route/course ownership");
        const auto location_before_restore = location;
        const auto read_index = [&] {
            std::ifstream source(root / location.locale / catalog_index_path(location),std::ios::binary);
            check(source.good(),"Cannot read the original indexed lesson source");
            return std::string(std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>());
        };
        const auto original_index = read_index();
        const auto active = windows::active_catalog_lesson(root,location,lessons.entries[1].id);
        check(active && active->id == lessons.entries[1].id && active->asset == lessons.entries[1].asset &&
            active->asset_locale == lessons.entries[1].asset_locale && active->available,
            "Restoring an active lesson changed its exact published identity/asset");
        check(!windows::active_catalog_lesson(root,location,next_unit->lesson) &&
            !windows::active_catalog_lesson(root,location,next_course->lesson),
            "Restoring navigation searched another unit or course for the saved lesson");
        for (const auto id : {"","missing","../lesson.json","C:\\other\\lesson.json","a/b","a\\b","wrong__unit__lesson"})
            check(!windows::active_catalog_lesson(root,location,id),"An absent or malformed active ID escaped the current index");
        check(!windows::active_catalog_lesson(root,CatalogLocation{},lessons.entries[1].id) &&
            !windows::active_catalog_lesson(root,catalog_parent(location),lessons.entries[1].id),
            "Restoring an active lesson invented missing current course/unit parents");
        check(location == location_before_restore && read_index() == original_index,
            "Reading the active lesson mutated current navigation or the curricular index");
        auto unpublished = first_lesson; unpublished.lesson = unpublished.unit + "__unindexed";
        rejects([&] { (void)windows::next_catalog_lesson(root,unpublished,true,true); },"Automatic continuation activated an unindexed lesson");
        check(catalog_parent(location).course == "cpp_fundamentals" && catalog_parent(catalog_parent(location)).route == "programming","Back navigation lost real parents");
        check(catalog_parent(catalog_parent(catalog_parent(location))) == CatalogLocation{},"Home does not preserve the route stack");
        check(catalog_lesson_path(location,lessons.entries[1].id).generic_string() == "programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json","Historical lesson path was renamed");
        for (const auto locale : {"es","en","fr"}) {
            auto translated = location; translated.locale = locale;
            const auto page = windows::load_catalog(root,translated);
            check(!page.entries.empty() && page.entries[1].id == lessons.entries[1].id,"Translation changed lesson ownership");
            const auto restored = windows::active_catalog_lesson(root,translated,lessons.entries[1].id);
            check(restored && restored->asset == page.entries[1].asset && restored->asset_locale == page.entries[1].asset_locale &&
                translated.locale == locale,"Restoring active navigation substituted another content language");
        }
        auto math = catalog_child(CatalogLocation{},"math_physics");
        const auto math_courses = windows::load_catalog(root,math);
        check(math_courses.entries.front().id == "math_course_01","Mathematics route missing");
        math = catalog_child(math,math_courses.entries.front().id);
        const auto math_units = windows::load_catalog(root,math);
        check(!math_units.entries.empty(),"Mathematics indexed units hidden");
        math = catalog_child(math,math_units.entries.front().id);
        const auto math_lessons=windows::load_catalog(root,math);
        check(!math_lessons.entries.empty(),"Mathematics indexed lessons hidden");
        const auto math_lesson=windows::load_lesson_progress_metadata(math_lessons.entries.front().asset,math_lessons.entries.front().asset_locale);
        state::StateData unverified_math;
        std::size_t saved_math_answers=0;
        for (const auto& page : math_lesson.pages) for (const auto& block : page.blocks)
            if (block.type==BlockType::math_puzzle) {
                state::Response response;response.correct=true;
                response.raw_json=R"({"correct":true,"responses":{},"contentFingerprint":"unverified"})";
                state::put_response(unverified_math,exercise_key(math_lesson,block),response);
                ++saved_math_answers;
            }
        check(saved_math_answers>0,"Math catalog acceptance fixture no longer includes actual puzzles");
        const auto math_before=unverified_math;
        const auto needs_validation=windows::catalog_entry_progress(root,math,math_lessons.entries.front(),unverified_math);
        check(needs_validation.pending_validation && needs_validation.completed==0,
            "A bare saved math correct flag was credited without current-content verification");
        std::size_t validations=0;
        const auto rejected_math=windows::catalog_entry_progress(root,math,math_lessons.entries.front(),unverified_math,
            [&](const Block& block,const state::Response& response,std::string_view locale) {
                check(block.type==BlockType::math_puzzle && response.correct && locale==math_lesson.locale &&
                    !block.raw_json.empty(),"Math catalog validation lost exact current payload/state/language");
                ++validations;return false;
            });
        check(validations>0 && !rejected_math.pending_validation && rejected_math.completed==0 && unverified_math==math_before,
            "Math validation failures were hidden, counted as correct, or changed student data");
        for (const auto id : {"..","a/b","a\\b","C:bad","a.json","","x y"})
            rejects([&] { (void)catalog_child(CatalogLocation{"es","programming",{},{}},id); },"Unsafe course path accepted");
        rejects([&] { (void)catalog_child(CatalogLocation{},"unknown"); },"Unknown route accepted");
        rejects([&] { (void)catalog_child(CatalogLocation{"es","programming","cpp_fundamentals",{}},"other__unit"); },"Cross-course unit accepted");
        rejects([&] { (void)catalog_lesson_path(location,"other__unit__lesson"); },"Cross-unit lesson accepted");
        rejects([&] { (void)windows::load_catalog(root,{"es","programming","unindexed",{}}); },"An unpublished directory can activate a draft course");
        rejects([&] { (void)windows::load_catalog(root,{"es","programming","cpp_fundamentals","cpp_fundamentals__unindexed"}); },"An unpublished unit can activate drafts");
        rejects([&] { (void)windows::load_catalog(root,{"de",{},{},{}}); },"Unsupported locale silently reinterpreted");
        state::StateData progress;
        check(!catalog_lesson_started(progress,lessons.entries[1].id),"A catalog visit marked a lesson started");
        state::set_saved_page(progress,lessons.entries[1].id,0);
        check(catalog_lesson_started(progress,lessons.entries[1].id) && !catalog_lesson_completed(progress,lessons.entries[1].id),"Opening page zero became completion or lost started status");
        progress.preferences["learning_progress"]["lesson_" + lessons.entries[1].id + "_completed"] = true;
        check(catalog_lesson_completed(progress,lessons.entries[1].id),"Original imported completion identity was ignored");
        const auto scope = windows::catalog_scope(root,location);
        check(scope.lessons.size() == 5 && scope.courses == std::vector<std::string>{"cpp_fundamentals"} && scope.units.size() == 1,"Reset scope differs from published index");
        const auto lesson_id = lessons.entries[1].id;
        progress.preferences["learning_progress"]["exercise_" + lesson_id + "_page_4_block_2"] = std::string("saved answer");
        progress.preferences["learning_progress"]["exercise_" + lesson_id + "_state_v2_" + std::string(64,'a') + "_page_6_block_1"] = std::string("added answer");
        progress.preferences["learning_progress"]["lesson_" + lesson_id + "_state_lineage_v2_es"] = std::string("immutable ownership");
        progress.preferences["learning_progress"]["lesson_" + lesson_id + "_read_pages_v2_es"] = std::vector<std::string>{"page_0000"};
        progress.preferences["learning_progress"]["lesson_" + lesson_id + "_extra_completed"] = true;
        progress.preferences["learning_progress"]["exercise_" + lesson_id + "_extra_page_0_block_0"] = std::string("other lesson sharing prefix");
        progress.preferences["learning_progress"]["lesson_math_course_01__numeros_y_operaciones__otra_completed"] = true;
        progress.preferences["cpp_lesson_drafts"]["programming/" + lesson_id + "/4/2/source"] = std::string("user source intact");
        progress.preferences["narration_settings"]["voice"] = std::string("ef_dora");
        progress.preferences["native_windows"]["reference-proof/v1/es/programming/" + lesson_id + "/4/2"] = std::string("old receipt");
        const auto original_progress = progress;
        check(catalog_progress(progress,scope).completed == 1,"Real completion summary does not reflect imported data");
        check(reset_catalog_progress(progress,scope) >= 5 && !catalog_lesson_started(progress,lesson_id),"Reset did not clear its real lesson status");
        check(state::find(progress,"learning_progress","exercise_" + lesson_id + "_page_4_block_2") == nullptr &&
            state::find(progress,"learning_progress","exercise_" + lesson_id + "_state_v2_" + std::string(64,'a') + "_page_6_block_1") == nullptr,"Reset retained scoped responses");
        check(state::string_value(progress,"learning_progress","lesson_" + lesson_id + "_state_lineage_v2_es") == "immutable ownership" &&
            std::get<std::vector<std::string>>(*state::find(progress,"learning_progress","lesson_" + lesson_id + "_read_pages_v2_es")).empty(),"Reset corrupted state ownership or retained read coverage");
        check(state::find(progress,"learning_progress","lesson_" + lesson_id + "_extra_completed") != nullptr &&
            state::find(progress,"learning_progress","exercise_" + lesson_id + "_extra_page_0_block_0") != nullptr &&
            state::find(progress,"learning_progress","lesson_math_course_01__numeros_y_operaciones__otra_completed") != nullptr,"Reset crossed its exact lesson/route boundary");
        check(progress.preferences.at("cpp_lesson_drafts") == original_progress.preferences.at("cpp_lesson_drafts") &&
            progress.preferences.at("narration_settings") == original_progress.preferences.at("narration_settings"),"Reset altered personal drafts or voice preferences");
        std::cout << "Catalog real indices, both routes, three languages, historical paths/order, safe parents and read-only progress: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
