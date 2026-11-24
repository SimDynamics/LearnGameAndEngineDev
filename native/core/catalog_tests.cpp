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
#include <set>
#include <string_view>
#include <utility>

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
    // This core test supplies captured execution results to the receipt API;
    // native compiler execution is covered by the separate runner suites.
    windows::CppRunRequest captured;captured.revision=7;captured.source_utf8="same captured program";captured.stdin_utf8="2";
    FreshExecution execution{7,true,true,0,"2","4"};
    auto stale=execution;stale.source_revision=6;
    check(!windows::record_compiler_puzzle_case(data,lesson,block,captured,stale,"Correcto"),"A stale result accredited a compiler objective");
    check(!windows::record_compiler_puzzle_case(data,lesson,block,captured,execution,"Correcto")&&
        windows::compiler_puzzle_case_count(data,lesson,block)==1&&!windows::compiler_puzzle_completed(data,lesson,block)&&progress().completed==0,
        "An optional second case replaced the required first-case achievement");
    auto edited=captured;edited.source_utf8+=" changed";
    windows::invalidate_compiler_puzzle_attempt(data,lesson,block,edited);
    windows::invalidate_compiler_puzzle_attempt(data,lesson,block,captured);
    check(windows::compiler_puzzle_case_count(data,lesson,block)==0,"Undo resurrected an invalidated partial execution proof");
    captured.stdin_utf8="1";execution.standard_input="1";execution.standard_output="2";
    check(windows::record_compiler_puzzle_case(data,lesson,block,captured,execution,"Correcto\nExplicación comprobada.")&&
        windows::compiler_puzzle_case_count(data,lesson,block)==1&&windows::compiler_puzzle_completed(data,lesson,block)&&progress().completed==1,
        "The first declared case did not complete the required compiler activity independently");
    const auto first_achievement=data;
    captured.stdin_utf8="2";windows::invalidate_compiler_puzzle_attempt(data,lesson,block,captured);
    check(data==first_achievement,"Changing only input discarded the earned first-case achievement");
    execution.standard_input="2";execution.standard_output="4";
    check(windows::record_compiler_puzzle_case(data,lesson,block,captured,execution,"Correcto\nEnsayo opcional.")&&
        windows::compiler_puzzle_case_count(data,lesson,block)==2&&progress().completed==1,
        "An optional second case failed to record its own result or changed page completion");
    auto expected=first_achievement;std::size_t extended=0;
    for(auto& [key,value]:expected.preferences["native_windows"])if(key.ends_with("/cases")){
        const auto current=state::find(data,"native_windows",key);
        check(current!=nullptr,"The optional case discarded its existing proof vector");value=*current;++extended;
    }
    check(extended==1&&data==expected,"An optional case rewrote the first-case seal, feedback or unrelated state");
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
        auto legacy_flag=lesson;legacy_flag.compiler_puzzles_required=false;
        check(catalog_lesson_page_progress(data,legacy_flag,pending).completed==before.completed,
            "A legacy missing flag made an unanswered curricular diagnostic optional");
        check(data==retained&&!windows::compiler_puzzle_completed(data,lesson,diagnostic),
            "Inspecting catalog progress changed student data or manufactured diagnostic proof");
    }
    std::cout<<"PASS five required compile diagnostics in catalog: opaque flags/read coverage stay pending, completion callback and metadata preserve groups, no native proof invented\n";
}
void a_missing_compiler_evaluator_never_completes_a_curricular_page() {
    using namespace lgaed;using namespace curriculum;
    Lesson lesson;lesson.id="catalog_missing_evaluator";
    Block editor;editor.type=BlockType::cpp_compiler;editor.stable_id="editor";
    lesson.pages={Page{"page",{editor}}};
    state::StateData data;
    state::set_saved_page(data,lesson.id,0);
    data.preferences["learning_progress"]["lesson_"+lesson.id+"_completed"]=true;
    state::put_response(data,lesson,editor,state::Response{"opaque historical answer",true});
    const auto retained=data;
    bool asked=false;
    const auto progress=catalog_lesson_page_progress(data,lesson,[&](const Block& block){
        asked=true;return windows::compiler_puzzle_completed(data,lesson,block);
    });
    check(asked&&progress.total==1&&progress.completed==0&&data==retained,
        "An editor without a checker borrowed a visited page, old answer or completion flag");
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
    // Reset operates on the currently published lesson. Later authenticated
    // wording improvements do not require reconstructing an obsolete terminal
    // payload or replaying the original option revision over them.
    const auto revised=windows::load_lesson(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos"/plan.filename);
    const auto at=curriculum::stable_block_position(revised,plan.identity);
    check(at.has_value(),"Choice reset fixture lost its activity");
    const auto& block=revised.pages[at->page].blocks[at->block];
    check(block.choice_revision&&block.choice_revision->sha256==plan.before_full&&
        !block.choice_revision->general_option_index&&block.options.at(plan.option)==plan.replacement,
        "Catalog reset lost the authenticated historical choice revision");
    const auto layout=winrt::Windows::Data::Json::JsonObject::Parse(
        winrt::to_hstring(revised.raw_json)).GetNamedObject(L"stateLayout");
    unsigned originals=0,transitions=0;
    for(const auto& value:layout.GetNamedArray(L"choiceRevisions")) {
        const auto record=value.GetObject();
        if(winrt::to_string(record.GetNamedString(L"id"))!=plan.identity)continue;
        ++originals;
        check(test::compile_check_test_fingerprint(record.GetNamedObject(L"block"))==plan.before_full,
            "Catalog reset accepted a different complete historical option");
    }
    for(const auto& value:layout.GetNamedArray(L"editorial")) {
        const auto proof=value.GetObject();
        if(winrt::to_string(proof.GetNamedString(L"id"))==plan.identity&&
            winrt::to_string(proof.GetNamedString(L"beforeSha256"))==plan.before_full&&
            winrt::to_string(proof.GetNamedString(L"afterSha256"))==plan.after_full)++transitions;
    }
    check(originals==1&&transitions==1,
        "Catalog reset requires the original pinned archive and published option transition");
    const auto legacy=curriculum::exercise_key(revised,block),current=state::response_storage_key(revised,block);
    check(legacy!=current,"Catalog reset did not distinguish historical and current choice receipts");
    state::StateData data;state::prepare_lesson_state(data,revised);
    state::Response response;response.correct=true;response.selected=static_cast<std::int64_t>(*block.correct_index);
    state::put_response(data,legacy,response);state::put_response(data,revised,block,response);
    std::vector<std::string> removed{legacy,current};
    for(const auto locale:{"en","fr"})removed.push_back("choice-options/v1/"+std::string(locale)+'/'+std::string(64,'a')+'/'+legacy);
    removed.push_back("order-lines/v1/es/"+std::string(64,'b')+'/'+legacy);
    for(const auto& key:removed)if(!state::find(data,"learning_progress",key))data.preferences["learning_progress"][key]=std::string("explicit reset target");
    const auto foreign="exercise_"+revised.id+"_extra_page_0_block_0";
    const std::vector<std::string> preserved{
        "choice-options/v1/es/"+std::string(64,'a')+'/'+foreign,
        "choice-options/v1/de/"+std::string(64,'a')+'/'+legacy,
        "choice-options/v1/es/"+std::string(64,'g')+'/'+legacy,
        "choice-options/v1/es/"+std::string(64,'A')+'/'+legacy,
        "choice-options/v1/es/short/"+legacy};
    for(const auto& key:preserved)data.preferences["learning_progress"][key]=std::string("preserve unrecognized or foreign state");
    data.preferences["cpp_lesson_drafts"]["programming/"+revised.id+"/0/0/source"]=std::string("student source intact\r\n");
    data.preferences["narration_settings"]["voice"]=std::string("ef_dora");
    const auto before=data;curriculum::CatalogScope scope;scope.lessons={revised.id};
    check(curriculum::reset_catalog_progress(data,scope)==removed.size(),"Catalog reset did not remove exactly the scoped answer editions");
    for(const auto& key:removed)check(!state::find(data,"learning_progress",key),"Catalog reset retained a scoped choice/order answer");
    for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values)
        if(std::ranges::find(removed,key)==removed.end())check(state::find(data,domain,key)&&*state::find(data,domain,key)==value,
            "Catalog reset changed a choice revision pin, unrelated answer, draft or preference");
    check(!state::get_response(data,revised,block),"Catalog reset restored a historical choice achievement");
    std::cout<<"PASS choice revision catalog reset: current/original/locales removed, exact scope, archive pins, drafts and unknown keys retained\n";
}

void invalid_scoped_coverage_cannot_partially_reset_student_progress() {
    using namespace lgaed;
    curriculum::CatalogScope scope;
    scope.courses = {"qa_course"};
    scope.units = {"qa_course__unit"};
    scope.lessons = {"qa_course__unit__first", "qa_course__unit__second"};
    state::StateData data;
    auto& progress = data.preferences["learning_progress"];
    const auto first = scope.lessons.front(), second = scope.lessons.back();
    const auto completion = "lesson_" + first + "_completed";
    const auto answer = "exercise_" + first + "_page_0_block_1";
    const auto valid_read = "lesson_" + first + "_read_pages_v2_es";
    const auto invalid_read = "lesson_" + second + "_read_blocks_v2_fr";
    progress[completion] = true;
    progress["lesson_" + second + "_started"] = true;
    progress[answer] = std::string("answered");
    progress[valid_read] = std::vector<std::string>{"page_0000"};
    progress[invalid_read] = std::int64_t{2};
    progress["unit_" + scope.units.front() + "_completed"] = true;
    progress["course_" + scope.courses.front() + "_completed"] = true;
    progress["lesson_" + first + "_state_lineage_v2_es"] = std::string("historic source");
    progress["lesson_qa_other__unit__first_completed"] = true;
    data.preferences["native_windows"]["scroll/" + first + "/reader"] = std::int64_t{4};
    data.preferences["cpp_lesson_drafts"]["programming/" + first + "/0/1/source"] =
        std::string("unsent source");
    data.preferences["narration_settings"]["voice"] = std::string("ef_dora");
    const auto before = data;
    rejects([&] { (void)curriculum::reset_catalog_progress(data,scope); },
        "A malformed selected coverage was not rejected");
    check(data == before,
        "A rejected course reset removed answers, bookmarks or coverage before discovering corrupted state");

    // An unrelated course's malformed coverage must not block the authorized
    // scope. Fix only the selected receipt and retry the same reset.
    data.preferences["learning_progress"][invalid_read] =
        std::vector<std::string>{"block_0000"};
    data.preferences["learning_progress"]["lesson_qa_other__unit__first_read_pages_v2_es"] =
        std::int64_t{99};
    const auto corrected = data;
    const auto count = curriculum::reset_catalog_progress(data,scope);
    check(count == 8 && !state::find(data,"learning_progress",completion) &&
        !state::find(data,"learning_progress","lesson_" + second + "_started") &&
        !state::find(data,"learning_progress",answer) &&
        !state::find(data,"learning_progress","unit_" + scope.units.front() + "_completed") &&
        !state::find(data,"learning_progress","course_" + scope.courses.front() + "_completed") &&
        !state::find(data,"native_windows","scroll/" + first + "/reader") &&
        std::get<std::vector<std::string>>(*state::find(data,"learning_progress",valid_read)).empty() &&
        std::get<std::vector<std::string>>(*state::find(data,"learning_progress",invalid_read)).empty(),
        "An accepted scoped reset kept stale learning data or discarded its empty coverage slots");
    for (const auto& [domain,records] : corrected.preferences)
        for (const auto& [key,value] : records)
            if (domain != "learning_progress" && domain != "native_windows")
                check(state::find(data,domain,key) &&
                    *state::find(data,domain,key) == value,
                    "Reset changed a pending draft or a general preference");
    check(state::find(data,"learning_progress","lesson_qa_other__unit__first_completed") &&
        state::find(data,"learning_progress","lesson_qa_other__unit__first_read_pages_v2_es") &&
        state::string_value(data,"learning_progress","lesson_" + first + "_state_lineage_v2_es") ==
            "historic source",
        "A course reset crossed its scope or removed original edition provenance");
    const auto accepted = data;
    check(curriculum::reset_catalog_progress(data,scope) == 0 && data == accepted,
        "A repeated successful reset changed its earlier result");
    std::cout << "PASS atomic malformed-coverage preflight: no partial reset, exact course scope, drafts and lineage preserved\n";
}
void reached_page_reset_removes_only_selected_legacy_frontiers(){
    using namespace lgaed;
    state::StateData data;
    curriculum::CatalogScope scope;scope.lessons={"qa_reach","qa_other_selected"};
    std::vector<std::string> removed;
    for(const auto& id:scope.lessons)for(const auto locale:{"es","en","fr"}){
        const auto key="lesson_"+id+"_reached_page_v1_"+locale;
        data.preferences["learning_progress"][key]=std::int64_t{4};removed.push_back(key);
    }
    for(const auto& key:std::vector<std::string>{
        "lesson_qa_reach_extra_reached_page_v1_es",
        "lesson_unselected_reached_page_v1_en",
        "lesson_unselected_reached_page_v1_fr",
        "lesson_qa_reach_reached_page_v1_de",
        "lesson_qa_reach_reached_page_v1_es_extra"})
        data.preferences["learning_progress"][key]=std::int64_t{7};
    data.preferences["learning_progress"]["lesson_qa_reach_state_lineage_v1"]=std::string("historical ownership");
    data.preferences["learning_progress"]["lesson_qa_reach_state_lineage_v2_es"]=std::string("current ownership");
    data.preferences["learning_progress"]["lesson_qa_reach_state_v1_owner_locale"]=std::string("es");
    data.preferences["learning_progress"]["lesson_qa_reach_state_additions_v2_es"]=std::string("[]");
    data.preferences["learning_progress"]["lesson_qa_reach_state_editorial_returns_v1_es"]=std::string("preserved provenance");
    data.preferences["native_windows"][removed.front()]=std::int64_t{8};
    data.preferences["cpp_lesson_drafts"]["programming/qa_reach/0/0/source"]=std::string("keep draft\r\n");
    data.preferences["narration_settings"]["voice"]=std::string("ef_dora");
    const auto before=data;
    check(curriculum::reset_catalog_progress(data,scope)==removed.size(),
        "Catalog reset did not remove exactly the selected lessons' three-language legacy frontiers");
    for(const auto& key:removed)check(!state::find(data,"learning_progress",key),
        "Catalog reset retained a selected legacy reached-page frontier");
    for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values)
        if(domain!="learning_progress"||std::ranges::find(removed,key)==removed.end())
            check(state::find(data,domain,key)&&*state::find(data,domain,key)==value,
                "Legacy frontier reset crossed its exact scope or altered ownership, provenance, draft or settings");
    const auto reset=data;
    check(curriculum::reset_catalog_progress(data,scope)==0&&data==reset,
        "Repeating a legacy frontier reset changed preserved data");
    std::cout<<"PASS reached-page catalog reset: selected es/en/fr frontiers removed, unrelated lessons/locales/domains and provenance retained\n";
}
void programming_catalog_preserves_published_ids(const std::filesystem::path& root,
        const lgaed::curriculum::CatalogPage& courses) {
    using namespace lgaed; using namespace curriculum;
    // Published baseline 18ffc77724e68f1ef3240f146022b3bd3206153a,
    // courses.json blob f936375562f72372d191ce56f40753ee3eb0684a.
    // New courses may be inserted; old identities and relative order remain.
    static constexpr std::string_view original_ids[]{
        "cpp_fundamentals",
        "programming_practice_tools",
        "systems_architecture_toolchain",
        "course_04",
        "course_05",
        "course_06",
        "course_07",
        "course_08",
        "course_09",
        "course_10",
        "course_11",
        "course_12",
        "course_13",
        "course_14",
        "course_15",
        "course_16",
        "course_17",
        "course_18",
        "course_19",
        "course_20",
        "course_21",
        "course_22",
        "course_23",
        "course_24",
        "course_25",
        "course_26",
        "course_27",
        "course_28",
        "course_29",
        "course_30",
        "course_31",
        "course_32",
        "course_33",
        "course_34",
        "course_35",
        "course_36",
        "course_37",
        "course_38",
        "course_39",
        "course_40",
        "course_41",
        "course_42",
        "course_43",
        "course_44"
    };
    auto next = courses.entries.begin();
    for (const auto id : original_ids) {
        next = std::find_if(next,courses.entries.end(),[&](const auto& entry){ return entry.id == id; });
        check(next != courses.entries.end(),"An original course ID was removed, renamed or reordered");
        ++next;
    }
    const auto bytes = state::read_bytes(root / "es/programming/courses.json");
    const std::string source(bytes.begin(),bytes.end());
    std::string_view json = source;
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    const auto published = winrt::Windows::Data::Json::JsonArray::Parse(winrt::to_hstring(json));
    check(courses.entries.size() == published.Size(),"The loader hid an entry from the current course index");
    std::set<std::string> identities;
    for (std::size_t index = 0; index < courses.entries.size(); ++index) {
        const auto& entry = courses.entries[index];
        const auto declared = published.GetObjectAt(static_cast<unsigned>(index));
        check(identities.insert(entry.id).second && entry.number == index + 1,
            "The course index repeated an identity or lost its declared sequential order");
        check(entry.id == winrt::to_string(declared.GetNamedString(L"id")) &&
            entry.title == winrt::to_string(declared.GetNamedString(L"title")) &&
            static_cast<double>(entry.number) == declared.GetNamedNumber(L"number"),
            "The native catalog changed an ID, title or ordinal from the published index");
        const auto expected = root / "es" / catalog_index_path(catalog_child(courses.location,entry.id));
        const auto installed = std::filesystem::is_regular_file(state::native_io_path(expected));
        check(entry.available == installed && (installed
            ? entry.asset == expected && entry.asset_locale == "es"
            : entry.asset.empty() && entry.asset_locale.empty()),
            "A course was redirected, hidden or announced as installed without its real index");
    }
}

// Compare the reader with the authored index and physical resources. A new
// course edition may replace its unit IDs/count without deleting the preserved
// historical assets, and each language can publish a different active edition.
void catalog_matches_its_published_index(
        const std::filesystem::path& root, const lgaed::curriculum::CatalogPage& page) {
    using namespace lgaed; using namespace curriculum;
    const auto level = catalog_level(page.location);
    check(level == CatalogLevel::units || level == CatalogLevel::lessons,
        "The published-index fixture needs a course or unit");
    const auto relative = catalog_index_path(page.location);
    auto index_locale = page.location.locale;
    if (!std::filesystem::is_regular_file(state::native_io_path(root / index_locale / relative)))
        index_locale = "es";
    const auto bytes = state::read_bytes(root / index_locale / relative);
    const std::string source(bytes.begin(),bytes.end());
    std::string_view json = source;
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    const auto document = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(json));
    const auto published = document.GetNamedArray(level == CatalogLevel::units ? L"units" : L"lessons");
    check(page.entries.size() == published.Size() &&
        page.title == winrt::to_string(document.GetNamedString(L"title")),
        "The native catalog hid rows or changed the published index title");
    const bool edition = document.HasKey(L"courseEdition");
    const auto resource_locale = edition
        ? winrt::to_string(document.GetNamedObject(L"courseEdition").GetNamedString(L"locale"))
        : page.location.locale;
    for (std::size_t index = 0; index < page.entries.size(); ++index) {
        const auto& entry = page.entries[index];
        const auto declared = published.GetObjectAt(static_cast<unsigned>(index));
        const auto id = winrt::to_string(declared.GetNamedString(L"id"));
        check(entry.id == id && entry.number == index + 1 &&
            static_cast<double>(entry.number) == declared.GetNamedNumber(L"number") &&
            entry.title == winrt::to_string(declared.GetNamedString(L"title")),
            "The native catalog changed a published ID, title or row order");
        const auto child = level == CatalogLevel::units
            ? catalog_index_path(catalog_child(page.location,id))
            : catalog_lesson_path(page.location,id);
        auto locale = resource_locale;
        auto expected = root / locale / child;
        if (!edition && !std::filesystem::is_regular_file(state::native_io_path(expected))) {
            locale = "es";
            expected = root / locale / child;
        }
        const auto installed = std::filesystem::is_regular_file(state::native_io_path(expected));
        check(entry.available == installed && (installed
            ? entry.asset == expected && entry.asset_locale == locale
            : entry.asset.empty() && entry.asset_locale.empty()),
            "An indexed row was redirected or acquired availability without its real resource");
    }
}

void catalog_rejects_duplicate_and_unsafe_ids_without_inventing_availability() {
    using namespace lgaed; using namespace curriculum;
    using namespace winrt::Windows::Data::Json;
    struct TemporaryCatalog {
        std::filesystem::path root;
        TemporaryCatalog() {
            root = std::filesystem::temp_directory_path() / ("lgaed-catalog-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            check(std::filesystem::create_directory(root),"Cannot create an isolated catalog QA directory");
        }
        ~TemporaryCatalog() { std::error_code ignored; std::filesystem::remove_all(root,ignored); }
    } fixture;
    const auto folder = fixture.root / "es/programming";
    std::filesystem::create_directories(folder);
    const auto write = [&](const JsonArray& rows) {
        std::ofstream output(folder / "courses.json",std::ios::binary | std::ios::trunc);
        output << winrt::to_string(rows.Stringify());
        output.flush();
        check(output.good(),"Cannot write the isolated catalog index");
    };
    const auto row = [](std::string_view id, int number) {
        JsonObject result;
        result.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));
        result.Insert(L"title",JsonValue::CreateStringValue(L"An announced course"));
        result.Insert(L"number",JsonValue::CreateNumberValue(number));
        return result;
    };
    const CatalogLocation route{"es","programming",{},{}};
    JsonArray duplicate; duplicate.Append(row("qa_planned",1)); duplicate.Append(row("qa_planned",2));
    write(duplicate);
    rejects([&]{ (void)windows::load_catalog(fixture.root,route); },
        "A duplicate course ID passed the native index loader");
    for (const auto id : {"../outside","a/b","a\\b","C:bad","x y",""}) {
        JsonArray unsafe; unsafe.Append(row(id,1)); write(unsafe);
        rejects([&]{ (void)windows::load_catalog(fixture.root,route); },
            "An unsafe course ID escaped the native index loader");
    }
    JsonArray planned; planned.Append(row("qa_planned",1)); planned.Append(row("qa_later",2));
    write(planned);
    const auto page = windows::load_catalog(fixture.root,route);
    check(page.entries.size() == planned.Size(),"Unavailable indexed courses were hidden");
    state::StateData data; const auto before = data;
    for (const auto& entry : page.entries) {
        const auto progress = windows::catalog_entry_progress(fixture.root,route,entry,data);
        check(!entry.available && entry.asset.empty() && entry.asset_locale.empty() &&
            progress.completed == 0 && progress.total == 0,
            "An announced course without files acquired availability or progress");
    }
    check(windows::catalog_scope(fixture.root,route).lessons.empty() && data == before,
        "Planned courses invented lesson membership or changed student data");
    rejects([&]{ (void)windows::load_catalog(fixture.root,catalog_child(route,"qa_planned")); },
        "An announced course opened without its real course index");

    // Index numbers are presentation order, not the storage owner. Reject
    // gaps, repeated ordinals and out-of-order rows before a learner can
    // navigate the catalog. A new course inserted at position 3 remains
    // legitimate when all 45 entries are renumbered consecutively.
    for(const auto [first,second] : {
            std::pair{0,2},std::pair{1,1},std::pair{1,3},
            std::pair{2,1},std::pair{1,-1}}) {
        JsonArray malformed;
        malformed.Append(row("qa_planned",first));
        malformed.Append(row("qa_later",second));
        write(malformed);
        rejects([&]{ (void)windows::load_catalog(fixture.root,route); },
            "An invalid course ordinal escaped the shared catalog loader");
    }
    write(planned);

    const auto write_index=[&](const std::filesystem::path& destination,const JsonObject& document) {
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream output(destination,std::ios::binary|std::ios::trunc);
        output << winrt::to_string(document.Stringify());
        output.flush();
        check(output.good(),"Cannot write a disposable unit or lesson index");
    };
    const auto announced=catalog_child(route,"qa_planned");
    const auto start=catalog_child(announced,"qa_planned__start");
    const auto course_file=fixture.root/"es"/catalog_index_path(announced);
    const auto unit_file=fixture.root/"es"/catalog_index_path(start);
    const auto course_index=[&](int second) {
        JsonArray items;items.Append(row("qa_planned__start",1));
        items.Append(row("qa_planned__later",second));
        JsonObject document;
        document.Insert(L"id",JsonValue::CreateStringValue(L"qa_planned"));
        document.Insert(L"title",JsonValue::CreateStringValue(L"QA course"));
        document.Insert(L"units",items);
        write_index(course_file,document);
    };
    course_index(3);
    rejects([&]{ (void)windows::load_catalog(fixture.root,announced); },
        "A unit numbering gap was allowed under a valid course parent");
    course_index(2);
    const auto units=windows::load_catalog(fixture.root,announced);
    check(units.entries.size()==2&&units.entries[0].number==1&&units.entries[1].number==2,
        "Contiguous unit ordinals were incorrectly rejected");
    catalog_matches_its_published_index(fixture.root,units);
    const auto lesson_index=[&](int second) {
        JsonArray items;items.Append(row("qa_planned__start__first",1));
        items.Append(row("qa_planned__start__second",second));
        JsonObject document;
        document.Insert(L"courseId",JsonValue::CreateStringValue(L"qa_planned"));
        document.Insert(L"unitId",JsonValue::CreateStringValue(L"qa_planned__start"));
        document.Insert(L"title",JsonValue::CreateStringValue(L"QA unit"));
        document.Insert(L"lessons",items);
        write_index(unit_file,document);
    };
    lesson_index(1);
    rejects([&]{ (void)windows::load_catalog(fixture.root,start); },
        "A repeated lesson ordinal was accepted inside a valid unit");
    lesson_index(2);
    const auto lessons=windows::load_catalog(fixture.root,start);
    check(lessons.entries.size()==2&&lessons.entries[0].number==1&&lessons.entries[1].number==2,
        "Contiguous lesson ordinals were incorrectly rejected");
    catalog_matches_its_published_index(fixture.root,lessons);
}

void inserted_course_resolves_real_content_and_keeps_id_owned_state(
        const std::filesystem::path& root, const lgaed::curriculum::CatalogPage& courses) {
    using namespace lgaed; using namespace curriculum;
    const auto find = [&](std::string_view id) {
        const auto entry = std::ranges::find(courses.entries,id,&CatalogEntry::id);
        check(entry != courses.entries.end(),"The expanded catalog lost a required course identity");
        return entry;
    };
    const auto previous = find("programming_practice_tools");
    const auto inserted = find("cpp_language_memory_oop");
    const auto shifted = find("systems_architecture_toolchain");
    check(previous < inserted && inserted < shifted && inserted->available && shifted->available,
        "The new course lost its published position or installed source");
    const auto course = catalog_child(courses.location,inserted->id);
    const auto units = windows::load_catalog(root,course);
    check(!units.entries.empty(),"The new course has no indexed units");
    state::StateData empty; const auto before = empty;
    std::set<std::string> installed_lessons;
    std::vector<std::string> indexed_lessons;
    std::optional<CatalogLocation> first;
    std::size_t opened = 0;
    for (const auto& unit : units.entries) {
        if (!unit.available) continue;
        const auto location = catalog_child(course,unit.id);
        const auto lessons = windows::load_catalog(root,location);
        for (const auto& entry : lessons.entries) {
            indexed_lessons.push_back(entry.id);
            if (!entry.available) continue;
            const auto lesson = windows::load_lesson(entry.asset,entry.asset_locale);
            const auto metadata = windows::load_lesson_progress_metadata(entry.asset,entry.asset_locale);
            const auto active = windows::active_catalog_lesson(root,location,entry.id);
            check(lesson.id == entry.id && metadata.id == entry.id &&
                lesson.pages.size() == metadata.pages.size() && !lesson.pages.empty() &&
                active && active->id == entry.id && active->asset == entry.asset &&
                active->asset_locale == entry.asset_locale,
                "The new course resolved another lesson, source language or progress owner");
            const auto progress = windows::catalog_entry_progress(root,location,entry,empty);
            check(progress.completed == 0 && progress.total == lesson.pages.size(),
                "Opening a new course lesson manufactured page completion");
            check(installed_lessons.insert(entry.id).second,"The new course indexed a lesson twice");
            if (!first) { first = location; first->lesson = entry.id; }
            ++opened;
        }
    }
    // These published lessons stay reachable while later lessons may be added.
    for (const auto id : {
            "cpp_language_memory_oop__objetos_tamanos_y_limites__separar_valor_y_tamano",
            "cpp_language_memory_oop__objetos_tamanos_y_limites__consultar_limites_y_evitar_desbordamientos",
            "cpp_language_memory_oop__objetos_tamanos_y_limites__elegir_tipo_y_seguir_conversiones",
            "cpp_language_memory_oop__referencias__fuentes_utilizadas"})
        check(installed_lessons.contains(id),"A published new-course lesson disappeared or became unavailable");
    const auto scope = windows::catalog_scope(root,course);
    check(scope.courses == std::vector<std::string>{inserted->id} && scope.lessons == indexed_lessons &&
        first && empty == before,"The new course lost its indexed scope or changed student state");

    const auto previous_course = catalog_child(courses.location,previous->id);
    const auto previous_units = windows::load_catalog(root,previous_course);
    std::optional<CatalogLocation> last;
    for (auto unit = previous_units.entries.rbegin(); unit != previous_units.entries.rend() && !last; ++unit) {
        if (!unit->available) continue;
        const auto location = catalog_child(previous_course,unit->id);
        const auto lessons = windows::load_catalog(root,location);
        for (auto entry = lessons.entries.rbegin(); entry != lessons.entries.rend(); ++entry)
            if (entry->available) { last = location; last->lesson = entry->id; break; }
    }
    check(last.has_value(),"The preceding course lost its final installed lesson");
    check(!windows::next_catalog_lesson(root,*last,true,false) &&
        windows::next_catalog_lesson(root,*last,true,true) == first,
        "Course continuation ignored its setting or skipped the newly inserted course");

    // The old third course moved to a later ordinal. Its IDs, paths, stored
    // completion and navigation keys remain the owners of the same data.
    const auto shifted_location = catalog_child(courses.location,shifted->id);
    const auto shifted_scope = windows::catalog_scope(root,shifted_location);
    check(!shifted_scope.lessons.empty(),"The historical shifted course lost its lessons");
    state::StateData saved;
    const auto& lesson_id = shifted_scope.lessons.front();
    saved.preferences["learning_progress"]["lesson_" + lesson_id + "_completed"] = true;
    saved.preferences["cpp_lesson_drafts"]["programming/" + lesson_id + "/0/0/source"] = std::string("unchanged draft");
    saved.preferences["narration_settings"]["voice"] = std::string("unchanged voice");
    const auto snapshot = saved;
    auto old_ordinal = *shifted; old_ordinal.number = 3;
    check(old_ordinal.number != shifted->number,"The insertion fixture no longer shifts the historical course ordinal");
    const auto old_progress = windows::catalog_entry_progress(root,courses.location,old_ordinal,saved);
    const auto new_progress = windows::catalog_entry_progress(root,courses.location,*shifted,saved);
    check(old_progress.completed == 1 && old_progress.total == shifted_scope.lessons.size() &&
        new_progress.completed == old_progress.completed && new_progress.total == old_progress.total &&
        catalog_position_key(catalog_child(courses.location,old_ordinal.id)) == catalog_position_key(shifted_location) &&
        state::decode(state::encode(saved)) == snapshot && saved == snapshot,
        "Changing only a course ordinal reassigned progress, navigation ownership or unrelated preferences");
    std::cout << "PASS catalog growth: published IDs retained, " << opened
        << " new-course lessons resolved, planned availability, ID-owned state and course continuation\n";
}

}
int main() {
    using namespace lgaed;
    using namespace curriculum;
    try {
        page_progress_matches_android_without_crediting_unanswered_puzzles();
        a_missing_compiler_evaluator_never_completes_a_curricular_page();
        dependent_puzzles_keep_historical_progress_without_granting_execution_proofs();
        compiler_puzzles_keep_captured_success_until_explicit_reset();
        required_compile_diagnostics_remain_pending_in_catalog_progress();
        stable_progress_groups_survive_splits_reordering_and_new_activities();
        unit_percentage_weights_lessons_like_android_and_keeps_completion_counts();
        catalog_metadata_does_not_claim_cmake_execution_or_round_math_payloads();
        choice_revision_reset_keeps_exact_scope_and_provenance();
        reached_page_reset_removes_only_selected_legacy_frontiers();
        invalid_scoped_coverage_cannot_partially_reset_student_progress();
        const auto root = std::filesystem::path(build::source_root) / "app/src/main/assets/content";
        CatalogLocation location;
        const auto routes = windows::load_catalog(root,location);
        check(routes.entries.size() == 2 && routes.entries[0].id == "programming" && routes.entries[1].id == "math_physics","Real route order changed");
        location = catalog_child(location,"programming"); const auto courses = windows::load_catalog(root,location);
        programming_catalog_preserves_published_ids(root,courses);
        catalog_rejects_duplicate_and_unsafe_ids_without_inventing_availability();
        inserted_course_resolves_real_content_and_keeps_id_owned_state(root,courses);
        location = catalog_child(location,"cpp_fundamentals"); const auto units = windows::load_catalog(root,location);
        catalog_matches_its_published_index(root,units);
        check(units.entries.size() >= 2 && units.entries.front().available,
            "The real navigation fixture needs an installed first unit and a following unit");
        location = catalog_child(location,units.entries.front().id); const auto lessons = windows::load_catalog(root,location);
        catalog_matches_its_published_index(root,lessons);
        check(lessons.entries.size() >= 2 && lessons.entries.front().available &&
            lessons.entries[1].available && lessons.entries.back().available,
            "The real navigation fixture needs installed first, second and final lessons");
        state::StateData pristine;
        const auto pristine_before=pristine;
        const auto lesson_metadata=windows::load_lesson_progress_metadata(lessons.entries[1].asset,lessons.entries[1].asset_locale);
        const auto lesson_card=windows::catalog_entry_progress(root,location,lessons.entries[1],pristine);
        const auto lesson_card_cached=windows::catalog_entry_progress(root,location,lessons.entries[1],pristine);
        check(lesson_metadata.id==lessons.entries[1].id &&
            lesson_card.total==lesson_metadata.pages.size() && lesson_card.completed==0 &&
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
        {
            // U11L7: its compiler was legitimately expanded before a later
            // physical source-format revision. The formatting predecessor
            // is the expanded contract, not the first-published compiler.
            // The Spanish edition no longer publishes historical U11. Its
            // preserved source still proves the formatting/expansion contract,
            // but must not be treated as an entry of the active course.
            const CatalogLocation u11{"es","programming","cpp_fundamentals",
                "cpp_fundamentals__texto_interpretacion_y_flujos"};
            CatalogEntry archived;
            archived.id="cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_lector_de_entrada_preparada";
            archived.asset=root/"es"/catalog_lesson_path(u11,archived.id);
            archived.asset_locale="es";archived.available=true;
            const auto metadata=windows::load_lesson_progress_metadata(archived.asset,archived.asset_locale);
            const auto opened=windows::load_lesson(archived.asset,archived.asset_locale);
            check(metadata.id==archived.id&&opened.id==metadata.id&&opened.pages.size()==metadata.pages.size()&&
                metadata.pages.size()>3&&metadata.pages[3].blocks.size()>5&&
                metadata.pages[3].blocks[5].output_check&&
                metadata.pages[3].blocks[5].output_check->cases.size()==21&&
                metadata.pages[3].blocks[5].source_format_original_initial_code.has_value(),
                "U11L7 lost its original source, expanded 21-case objective or progress metadata");
            rejects([&]{
                (void)windows::catalog_entry_progress(root,u11,archived,pristine);
            },"A preserved historical resource borrowed membership in the active Spanish edition");
            const auto card=catalog_lesson_page_progress(pristine,metadata,[](const Block&){return false;});
            check(card.total==opened.pages.size()&&card.completed==0&&pristine==pristine_before,
                "U11L7's preserved metadata manufactured progress or altered the user's state");
            // The earlier one-case compiler is not an acceptable source-format
            // predecessor once the expansion has been authenticated.
            auto forged=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(opened.raw_json));
            const auto layout=forged.GetNamedObject(L"stateLayout");
            const auto formats=layout.GetNamedArray(L"sourceFormatRevisions");
            const auto prior=layout.GetNamedArray(L"compilerRevisions").GetObjectAt(0).GetNamedObject(L"block");
            bool changed=false;
            for(unsigned i=0;i<formats.Size();++i){
                const auto entry=formats.GetObjectAt(i);
                if(winrt::to_string(entry.GetNamedString(L"id"))!="block_0003_0005")continue;
                entry.Insert(L"block",prior);changed=true;break;
            }
            check(changed,"U11L7's expanded compiler has no protected formatting archive");
            rejects([&]{
                (void)windows::parse_lesson_progress_metadata(winrt::to_string(forged.Stringify()),"es");
            },"A pre-expansion compiler snapshot passed as the revised formatting predecessor");
        }
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
        const auto following_unit = std::find_if(std::next(units.entries.begin()),units.entries.end(),
            [](const auto& entry){ return entry.available; });
        check(following_unit != units.entries.end() && next_unit && next_unit->course == location.course &&
            next_unit->unit == following_unit->id && next_unit->route == location.route,
            "Unit continuation skipped its indexed parent or changed routes");
        const auto final_unit = std::find_if(units.entries.rbegin(),units.entries.rend(),
            [](const auto& entry){ return entry.available; });
        check(final_unit != units.entries.rend(),"The active course has no installed final unit");
        auto course_end = catalog_child(catalog_parent(location),final_unit->id);
        const auto final_lessons = windows::load_catalog(root,course_end);
        catalog_matches_its_published_index(root,final_lessons);
        const auto final_lesson = std::find_if(final_lessons.entries.rbegin(),final_lessons.entries.rend(),
            [](const auto& entry){ return entry.available; });
        check(final_lesson != final_lessons.entries.rend(),"The final installed unit has no available lesson");
        course_end.lesson = final_lesson->id;
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
        const CatalogLocation historical{"es","programming","cpp_fundamentals",
            "cpp_fundamentals__programas_y_ejecucion"};
        const auto historical_lesson = historical.unit + "__primer_programa_y_salida";
        check(catalog_lesson_path(historical,historical_lesson).generic_string() ==
            "programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json",
            "Historical lesson path was renamed");
        rejects([&]{ (void)windows::load_catalog(root,historical); },
            "A preserved historical unit became part of the active Spanish edition");
        check(!windows::active_catalog_lesson(root,location,historical_lesson),
            "An old lesson ID was silently redirected to the new edition");
        for (const auto locale : {"es","en","fr"}) {
            auto course = catalog_parent(location); course.locale = locale;
            const auto translated_units = windows::load_catalog(root,course);
            catalog_matches_its_published_index(root,translated_units);
            check(!translated_units.entries.empty() && translated_units.entries.front().available,
                "A language lost its own published first unit");
            const auto translated = catalog_child(course,translated_units.entries.front().id);
            const auto page = windows::load_catalog(root,translated);
            catalog_matches_its_published_index(root,page);
            check(page.entries.size() >= 2 && page.entries[1].available,
                "The language fixture lost its published second lesson");
            const auto restored = windows::active_catalog_lesson(root,translated,page.entries[1].id);
            check(restored && restored->id == page.entries[1].id &&
                restored->asset == page.entries[1].asset && restored->asset_locale == page.entries[1].asset_locale &&
                translated.locale == locale,"Restoring active navigation substituted another identity or content language");
            if (translated.unit != location.unit)
                check(!windows::active_catalog_lesson(root,translated,lessons.entries[1].id),
                    "A language borrowed an active lesson from another edition");
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
        std::vector<std::string> indexed_lessons;
        for (const auto& entry : lessons.entries) indexed_lessons.push_back(entry.id);
        check(scope.lessons == indexed_lessons && scope.courses == std::vector<std::string>{"cpp_fundamentals"} &&
            scope.units == std::vector<std::string>{location.unit},"Reset scope differs from the exact published index");
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
        std::cout << "Catalog real indices, both routes, three language editions, preserved historical paths, safe parents and read-only progress: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
