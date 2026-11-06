#pragma once

// Included inside lesson_tests.cpp's anonymous namespace. These executions are
// deliberate synthetic model inputs, never claimed as real curricular runs.
void compiler_recheck_contract() {
    using namespace lgaed;
    using namespace lgaed::windows;
    const auto lesson=parse_lesson_json(R"json({"id":"synthetic_compiler_recheck","title":"Current verdict and earned achievement","compilerPuzzlesRequired":true,"pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"original","outputCheck":{"cases":[{"standardInput":"1","outputs":["2"],"explanation":"First case."},{"standardInput":"2","outputs":["4"],"explanation":"Second case."}]}}]}]})json");
    const auto& block=lesson.pages.front().blocks.front();
    state::StateData data;
    data.preferences["recheck_qa"]["opaque"]=std::string(" preserve\t\r\n保存 ");
    state::put_response(data,"unrelated_recheck",state::Response{"unrelated answer",false});
    CppRunRequest first;first.revision=7;first.source_utf8="synthetic original program";first.stdin_utf8="1";
    FreshExecution first_result{7,true,true,0,"1","2"};
    check(!compiler_puzzle_current_feedback(data,lesson,block,first),"An unattempted compiler acquired feedback without an actual verdict");
    check(!record_compiler_puzzle_case(data,lesson,block,first,first_result,"Correcto\nFirst case.")
        &&compiler_puzzle_case_count(data,lesson,block)==1,
        "Initial completion no longer requires all authored inputs of one captured program");
    check(!compiler_puzzle_current_feedback(data,lesson,block,first),
        "A first partial successful input falsely restored a whole-objective success message");
    auto second=first;second.revision=8;second.stdin_utf8="2";
    FreshExecution second_result{8,true,true,0,"2","4"};
    constexpr auto earned_text="Correcto\nBoth original cases checked.";
    check(record_compiler_puzzle_case(data,lesson,block,second,second_result,earned_text)
        &&compiler_puzzle_completed(data,lesson,block)&&compiler_puzzle_case_count(data,lesson,block)==2,
        "The two fresh inputs did not produce the original complete achievement");
    const auto achievement=data;
    auto selected=compiler_puzzle_current_feedback(data,lesson,block,second);
    check(selected&&selected->correct&&selected->text==earned_text,
        "The first earned success did not restore its feedback");

    // Rechecking a changed program must grade its new output before looking at
    // the already-earned receipt. A failed recheck preserves every old value.
    auto retry=second;retry.revision=9;retry.source_utf8+=" changed";
    auto wrong=second_result;wrong.source_revision=retry.revision;wrong.standard_output="wrong";
    check(!record_compiler_puzzle_case(data,lesson,block,retry,wrong,"Incorrecto")&&data==achievement,
        "An existing achievement turned a later wrong execution into success or was downgraded by it");
    constexpr auto retry_text="Incorrecto\nReview the newly executed output.";
    check(record_compiler_puzzle_review(data,lesson,block,retry,wrong,retry_text)
        &&compiler_puzzle_review(data,lesson,block,retry)==retry_text
        &&compiler_puzzle_completed(data,lesson,block)&&compiler_puzzle_feedback(data,lesson,block)==earned_text,
        "A new rejection after achievement was lost, hid the current attempt or erased the historical success");
    const auto reviewed=data;
    selected=compiler_puzzle_current_feedback(data,lesson,block,retry);
    check(selected&&!selected->correct&&selected->text==retry_text&&data==reviewed,
        "Historical success overrode the matching latest rejection or selection mutated stored data");
    for(const auto& [domain,values]:achievement.preferences)for(const auto& [name,value]:values)
        check(state::find(data,domain,name)&&*state::find(data,domain,name)==value,
            "Recording a newer rejection changed an earned receipt, draft or independent answer");
    auto reopened=state::decode(state::encode(data));
    check(reopened==reviewed&&compiler_puzzle_review(reopened,lesson,block,retry)==retry_text,
        "Persistence lost the newer reviewed rejection after achievement");
    selected=compiler_puzzle_current_feedback(reopened,lesson,block,retry);
    check(selected&&!selected->correct&&selected->text==retry_text,
        "Reopening preferred the old Correcto over the current Incorrecto");
    auto another_request=retry;++another_request.request_id;++another_request.revision;
    selected=compiler_puzzle_current_feedback(reopened,lesson,block,another_request);
    check(selected&&!selected->correct&&selected->text==retry_text,
        "Transient request/revision metadata made an identical restored draft lose its evaluated review");

    for(const auto mode:{"wrong","stale","build failure","uncompleted","unregistered input","stdin binding","target binding","build action","diagnostic action"}) {
        auto captured=retry;auto execution=wrong;const std::string_view change=mode;
        if(change=="stale")--execution.source_revision;
        if(change=="build failure")execution.compilation_succeeded=false;
        if(change=="uncompleted"){execution.execution_succeeded=false;execution.execution_completed=false;}
        if(change=="unregistered input"){captured.stdin_utf8="3";execution.standard_input="3";}
        if(change=="stdin binding"){execution.standard_input="1";execution.standard_output="2";}
        if(change=="target binding"){
            captured.cmake=CmakeConfig{"alpha",{},"Debug"};execution.executable="beta";execution.standard_output="4";
        }
        if(change=="build action"){captured.action=CppRunRequest::Action::build;execution.standard_output="4";}
        if(change=="diagnostic action"){captured.diagnostic=CppDiagnosticRequest{};execution.standard_output="4";}
        auto attempt=reviewed;
        check(!record_compiler_puzzle_case(attempt,lesson,block,captured,execution,"not a current success")&&attempt==reviewed,
            "An invalid/incorrect recheck bypassed evaluation after achievement or cleared a still-current rejection");
        if(change!="wrong")check(!record_compiler_puzzle_review(attempt,lesson,block,captured,execution,"not a genuine rejection")&&attempt==reviewed,
            "An unavailable or mismatched capture replaced the last genuine review after achievement");
    }

    auto different=retry;different.source_utf8+=" another edit";
    selected=compiler_puzzle_current_feedback(reviewed,lesson,block,different);
    check(selected&&selected->correct&&selected->text==earned_text
        &&compiler_puzzle_review(reviewed,lesson,block,different).empty(),
        "A different program borrowed the rejected attempt instead of retaining its independent earned history");
    auto edited=reviewed;invalidate_compiler_puzzle_attempt(edited,lesson,block,different);
    check(edited==achievement&&compiler_puzzle_review(edited,lesson,block,retry).empty(),
        "Editing retired the historical achievement or Undo resurrected the removed current rejection");
    auto new_guide=lesson;new_guide.pages.front().blocks.front().output_check->cases.back().explanation="Revised guide.";
    selected=compiler_puzzle_current_feedback(reviewed,new_guide,new_guide.pages.front().blocks.front(),retry);
    check(selected&&selected->correct&&selected->text==earned_text,
        "An outdated rejection guide overrode historical success after an editorial-only feedback update");

    auto correct=wrong;correct.standard_output="4";
    check(record_compiler_puzzle_case(reopened,lesson,block,retry,correct,"Correcto\nNew run.")
        &&compiler_puzzle_review(reopened,lesson,block,retry).empty()&&reopened==achievement,
        "A new correct recheck retained its rejection or rewrote the original completed proof");
    selected=compiler_puzzle_current_feedback(reopened,lesson,block,retry);
    check(selected&&selected->correct&&selected->text==earned_text,
        "A corrected recheck did not recover its earned-success presentation");
    reset_compiler_puzzle(reopened,lesson,block);
    check(!compiler_puzzle_current_feedback(reopened,lesson,block,retry)&&!compiler_puzzle_completed(reopened,lesson,block)
        &&reopened.preferences.at("recheck_qa")==achievement.preferences.at("recheck_qa"),
        "The explicit objective reset retained a verdict or changed unrelated data");
    std::cout<<"PASS repeated compiler grading, current rejection before earned success, exact reopen, valid capture guards and preserved historical achievement\n";
}
