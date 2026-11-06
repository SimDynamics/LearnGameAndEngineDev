#pragma once

// Included inside lesson_tests.cpp's anonymous namespace. All executions here
// are synthetic model inputs; the platform suite separately runs real programs.
void intentional_exit_code_contract() {
    using lgaed::windows::parse_lesson_json;
    using lgaed::windows::parse_lesson_progress_metadata;
    const auto fixture=[](std::string_view exit_member) {
        return std::string(R"json({"id":"synthetic_exit_contract","title":"Intentional process result","compilerPuzzlesRequired":true,"pages":[{"blocks":[{"type":"cpp_compiler","language":"cpp","initialCode":"int main() { return 1; }","outputCheck":{"cases":[{"standardInput":"","outputs":["Entrada invalida|9:4"],"explanation":"The output and process result agree.","reviewFeedback":[{"outputs":["wrong"],"text":"Check the output."}])json")
            +std::string(exit_member)+R"json(}]}}]}]})json";
    };
    const auto old=parse_lesson_json(fixture(""));
    const auto zero=parse_lesson_json(fixture(R"json(,"exitCode":0)json"));
    const auto one=parse_lesson_json(fixture(R"json(,"exitCode":1)json"));
    const auto& old_block=old.pages.front().blocks.front();
    const auto& zero_block=zero.pages.front().blocks.front();
    const auto& block=one.pages.front().blocks.front();
    const auto& goal=*block.output_check;
    check(old_block.output_check->cases.front().exit_code==0&&zero_block.output_check->cases.front().exit_code==0
        &&goal.cases.front().exit_code==1,"A missing process result stopped meaning zero or the authored result was lost");
    const auto metadata=parse_lesson_progress_metadata(one.raw_json);
    check(metadata.pages.front().blocks.front().output_check
        &&metadata.pages.front().blocks.front().output_check->cases.front().exit_code==1
        &&exercise_key(metadata,metadata.pages.front().blocks.front())==exercise_key(one,block),
        "Required-compiler progress omitted the process result or changed the answer address");
    const auto old_frame=reference_objective_frame(old,old_block);
    check(old_frame==reference_objective_frame(zero,zero_block)
        &&old_frame.find("process-exit-code/v1")==old_frame.npos
        &&old_frame!=reference_objective_frame(one,block),
        "Explicit zero changed historical proofs or a nonzero objective inherited their identity");

    FreshExecution run{11,true,false,1,"","Entrada invalida|9:4",{},"",true};
    auto evaluated=evaluate_output_check(goal,run,11);
    check(evaluated.verdict==OutputVerdict::correct&&evaluated.stdout_correct&&evaluated.stderr_correct
        &&evaluated.exit_code_correct&&evaluated.feedback==goal.cases.front().explanation,
        "A fresh normally completed intentional return 1 could not satisfy all authored criteria");
    check(evaluate_output_check(*old_block.output_check,run,11).verdict==OutputVerdict::unavailable,
        "Legacy zero-exit exercises began accepting or grading runtime failures");
    for(const int code:{0,2,255}) {
        auto changed=run;changed.exit_code=code;changed.execution_succeeded=code==0;
        evaluated=evaluate_output_check(goal,changed,11);
        check(evaluated.verdict==OutputVerdict::incorrect&&evaluated.stdout_correct&&evaluated.stderr_correct
            &&!evaluated.exit_code_correct&&evaluated.feedback.empty(),
            "The exact stdout with another normal process result was credited or revealed success feedback");
    }
    for(const int code:{-1,-1073741819,256,259}) {
        auto changed=run;changed.exit_code=code;
        check(evaluate_output_check(goal,changed,11).verdict==OutputVerdict::unavailable,
            "A crash, running-process sentinel or out-of-contract exit became an educational answer");
    }
    for(const auto revision:{10u,12u})
        check(evaluate_output_check(goal,run,revision).verdict==OutputVerdict::unavailable,
            "An intentional exit reused an execution captured before the current edit");
    auto changed=run;changed.compilation_succeeded=false;
    check(evaluate_output_check(goal,changed,11).verdict==OutputVerdict::unavailable,
        "A build failure manufactured a runtime proof");
    changed=run;changed.execution_completed=false;
    check(evaluate_output_check(goal,changed,11).verdict==OutputVerdict::unavailable,
        "A timeout/cancellation or mere nonzero result without runner completion satisfied the objective");
    changed.execution_succeeded=true;
    check(evaluate_output_check(goal,changed,11).verdict==OutputVerdict::unavailable,
        "The legacy success flag authenticated a nonzero completion without the new runner evidence");
    changed=run;changed.exit_code=0;changed.execution_completed=false;
    check(evaluate_output_check(goal,changed,11).verdict==OutputVerdict::unavailable,
        "Compilation alone or an uncompleted process with a zero placeholder was graded");
    changed=run;changed.standard_output+="\n";
    evaluated=evaluate_output_check(goal,changed,11);
    check(evaluated.verdict==OutputVerdict::incorrect&&!evaluated.stdout_correct&&evaluated.exit_code_correct,
        "Return 1 hid a significant trailing newline");
    changed.standard_output="wrong";
    check(evaluate_output_check(goal,changed,11).feedback=="Check the output.",
        "Normal intentional completion lost its authored output review");
    changed.exit_code=2;
    check(evaluate_output_check(goal,changed,11).feedback.empty(),
        "A wrong process result received feedback written for a matching-status output");
    changed=run;changed.standard_input="other";
    check(evaluate_output_check(goal,changed,11).verdict==OutputVerdict::unavailable,
        "An unrequested input borrowed an intentional-exit objective");

    auto channels=goal;channels.cases.front().standard_error=StandardErrorExpectation::non_blank;
    evaluated=evaluate_output_check(channels,run,11);
    check(evaluated.verdict==OutputVerdict::incorrect&&evaluated.stdout_correct&&!evaluated.stderr_correct
        &&evaluated.exit_code_correct,"Intentional exit skipped the independent runtime stderr objective");
    changed=run;changed.standard_error="expected diagnostic";
    check(evaluate_output_check(channels,changed,11).verdict==OutputVerdict::correct,
        "The stdout/stderr/exit-code conjunction could not complete");
    auto targeted=goal;targeted.cases.front().executable="first";
    auto other=targeted.cases.front();other.executable="second";other.exit_code=2;
    targeted.cases.push_back(other);
    changed=run;changed.executable="second";
    check(evaluate_output_check(targeted,changed,11).verdict==OutputVerdict::incorrect,
        "An exit code selected a different executable's case instead of checking the executed target");
    changed.exit_code=2;
    check(evaluate_output_check(targeted,changed,11).verdict==OutputVerdict::correct,
        "The selected target could not satisfy its own process result");
    targeted.cases.back().executable="first";changed.executable="first";
    rejects([&]{(void)evaluate_output_check(targeted,changed,11);},
        "Different exit codes concealed ambiguous input/target cases");

    // Keep old aggregate clients source-compatible: a successful zero result
    // remains authentic without requiring a newly appended member.
    FreshExecution legacy{11,true,true,0,"","Entrada invalida|9:4",{}};
    check(evaluate_output_check(*old_block.output_check,legacy,11).verdict==OutputVerdict::correct,
        "Appending runner-completion evidence broke an existing successful execution aggregate");
    check(evaluate_output_check(goal,legacy,11).verdict==OutputVerdict::incorrect,
        "An old zero-exit proof completed the newly authored return-1 objective");
    for(const auto token:{"true","false","null","1.0","1.5","1e0","-0","-1","256","3221225477",R"("1")","[]","{}"}) {
        const auto invalid=fixture(std::string(",\"exitCode\":")+token);
        rejects([&]{(void)parse_lesson_json(invalid);},"The native loader accepted a noninteger or unsafe process result");
        rejects([&]{(void)parse_lesson_progress_metadata(invalid);},"Required progress accepted an invalid process result");
    }
    for(const auto token:{"0","1","2","255"})
        check(parse_lesson_json(fixture(std::string(",\"exitCode\":")+token)).pages.front().blocks.front()
            .output_check->cases.front().exit_code==std::stoi(token),"A valid normal exit-code boundary failed to load");
    auto ambiguous=fixture(R"json(,"exitCode":1)json");
    replace_once(ambiguous,R"json(,"exitCode":1})json",
        R"json(,"exitCode":1},{"standardInput":"","outputs":["other"],"exitCode":2})json");
    rejects([&]{(void)parse_lesson_json(ambiguous);},"An authored exit status disambiguated otherwise identical cases");

    // Exercise the complete real adoption without changing an active asset.
    // The fixture owns the unchanged historical source plus one exact new goal.
    {
        using namespace winrt::Windows::Data::Json;
        const auto before=lgaed::test::load_pre_intentional_exit_lesson();
        const auto adopted=lgaed::test::with_intentional_exit(before);
        const auto position=stable_block_position(adopted,lgaed::test::intentional_exit_editor_id);
        check(position.has_value(),"Intentional-exit adoption lost the real stable editor");
        const auto& previous=before.pages[position->page].blocks[position->block];
        const auto& actual=adopted.pages[position->page].blocks[position->block];
        check(!previous.output_check&&actual.output_check&&actual.initial_code==previous.initial_code
            &&actual.output_check->cases.size()==1&&actual.output_check->cases.front().exit_code==1
            &&actual.output_check->cases.front().standard_input.empty()
            &&actual.output_check->cases.front().outputs==std::vector<std::string>{"Entrada invalida|9:4"},
            "Adoption changed the real program or its exact intentional-exit criterion");
        check(before.state_layout.lineage==adopted.state_layout.lineage
            &&before.state_layout.legacy_lineage==adopted.state_layout.legacy_lineage
            &&before.state_layout.signatures==adopted.state_layout.signatures
            &&exercise_key(before,previous)==exercise_key(adopted,actual)
            &&draft_key("programming",before,previous)==draft_key("programming",adopted,actual),
            "Adding the new process criterion reset an old state/draft owner or language lineage");
        const auto projected=parse_lesson_progress_metadata(adopted.raw_json);
        const auto& catalog=projected.pages[position->page].blocks[position->block];
        check(catalog.output_check&&catalog.output_check->cases.front().exit_code==1
            &&catalog.initial_code==previous.initial_code
            &&reference_objective_frame(projected,catalog)==reference_objective_frame(adopted,actual),
            "Required-compiler metadata omitted the exact new objective or changed its proof identity");
        const auto old_document=JsonObject::Parse(winrt::to_hstring(before.raw_json));
        const auto current_document=JsonObject::Parse(winrt::to_hstring(adopted.raw_json));
        const auto old_layout=old_document.GetNamedObject(L"stateLayout");
        const auto current_layout=current_document.GetNamedObject(L"stateLayout");
        for(const auto field:{L"origin",L"family",L"current",L"additions",L"retired",L"retirementHistory"}) {
            check(old_layout.HasKey(field)==current_layout.HasKey(field),"Adoption added or removed an existing layout/archive field");
            if(old_layout.HasKey(field))check(old_layout.GetNamedValue(field).Stringify()==current_layout.GetNamedValue(field).Stringify(),
                "Adoption rewrote the original family, layout, additions or archives");
        }
        const auto old_proofs=old_layout.GetNamedArray(L"editorial");
        const auto current_proofs=current_layout.GetNamedArray(L"editorial");
        check(current_proofs.Size()==old_proofs.Size()+1,"Adoption failed to append exactly one provenance edge");
        for(unsigned index=0;index<old_proofs.Size();++index)
            check(old_proofs.GetAt(index).Stringify()==current_proofs.GetAt(index).Stringify(),
                "Adoption rewrote a previously published editorial proof");
        const auto target=[&](const JsonObject& document) {
            return document.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page))
                .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));
        };
        const auto rewrite_tail=[&](const JsonObject& document) {
            const auto proofs=document.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
            proofs.GetObjectAt(proofs.Size()-1).Insert(L"afterSha256",JsonValue::CreateStringValue(
                winrt::to_hstring(lgaed::test::compile_check_test_fingerprint(target(document)))));
        };
        const auto rejected_by_both=[&](const JsonObject& document) {
            const auto raw=winrt::to_string(document.Stringify());
            rejects([&]{(void)parse_lesson_json(raw);},"An unreviewed real intentional-exit adoption passed the runtime loader");
            rejects([&]{(void)parse_lesson_progress_metadata(raw);},"An unreviewed intentional-exit adoption passed required progress");
        };
        for(const auto change:{"exit0","exit2","input","stdout","newline","source","lesson","identity","edge","unmapped","v1"}) {
            const auto invalid=JsonObject::Parse(winrt::to_hstring(adopted.raw_json));
            const auto editor=target(invalid);
            const auto criterion=editor.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
            const std::string_view mode=change;
            if(mode=="exit0"||mode=="exit2")criterion.Insert(L"exitCode",JsonValue::CreateNumberValue(mode=="exit0"?0:2));
            else if(mode=="input")criterion.Insert(L"standardInput",JsonValue::CreateStringValue(L"other"));
            else if(mode=="stdout"||mode=="newline") {
                JsonArray outputs;outputs.Append(JsonValue::CreateStringValue(mode=="stdout"?L"other":L"Entrada invalida|9:4\n"));
                criterion.Insert(L"outputs",outputs);
            } else if(mode=="source")editor.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(previous.initial_code+"\n// altered source")));
            if(mode=="exit0"||mode=="exit2"||mode=="input"||mode=="stdout"||mode=="newline"||mode=="source")rewrite_tail(invalid);
            else if(mode=="lesson")invalid.Insert(L"id",JsonValue::CreateStringValue(L"different_owner"));
            else if(mode=="identity") {
                const auto ids=invalid.GetNamedObject(L"stateLayout").GetNamedArray(L"current")
                    .GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks");
                const auto index=static_cast<unsigned>(position->block);const auto other=index-1;
                const auto first=ids.GetAt(index),second=ids.GetAt(other);ids.SetAt(index,second);ids.SetAt(other,first);
            } else if(mode=="edge")invalid.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
            else if(mode=="unmapped")invalid.Remove(L"stateLayout");
            else if(mode=="v1") {
                const auto layout=invalid.GetNamedObject(L"stateLayout");JsonObject v1;
                v1.Insert(L"version",JsonValue::CreateNumberValue(1));v1.Insert(L"origin",layout.GetNamedArray(L"origin"));
                v1.Insert(L"current",layout.GetNamedArray(L"current"));invalid.Insert(L"stateLayout",v1);
            }
            rejected_by_both(invalid);
        }
        // Even an otherwise well-formed explicit return to the old exploratory
        // block cannot remove an already published process objective.
        const auto downgrade=JsonObject::Parse(winrt::to_hstring(adopted.raw_json));
        const auto preceding=JsonObject::Parse(target(downgrade).Stringify());target(downgrade).Remove(L"outputCheck");
        const auto layout=downgrade.GetNamedObject(L"stateLayout");JsonObject provenance;
        provenance.Insert(L"lessonId",downgrade.GetNamedValue(L"id"));provenance.Insert(L"locale",layout.GetNamedValue(L"locale"));
        provenance.Insert(L"editorial",layout.GetNamedArray(L"editorial"));
        JsonObject returns;returns.Insert(L"revision",JsonValue::CreateNumberValue(0));
        returns.Insert(L"historySha256",JsonValue::CreateStringValue(winrt::to_hstring(lgaed::test::compile_check_test_fingerprint(provenance))));
        returns.Insert(L"previousBlock",preceding);JsonObject edge;
        edge.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(lgaed::test::intentional_exit_editor_id)));
        edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(lgaed::test::compile_check_test_fingerprint(preceding))));
        edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(lgaed::test::intentional_exit_editor_before)));
        edge.Insert(L"returnTo",returns);layout.GetNamedArray(L"editorial").Append(edge);
        rejected_by_both(downgrade);
    }
    std::cout<<"PASS intentional normal process exit, exact stdout/stderr, fresh completion, default-zero identities and strict native schema\n";
}
