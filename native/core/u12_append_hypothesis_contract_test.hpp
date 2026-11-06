#pragma once

// Included inside lesson_tests.cpp's existing anonymous namespace. All state
// below is explicitly synthetic unit data, never a student's curricular work.
void u12_append_hypothesis_contract(const std::filesystem::path& before_path,
        const std::filesystem::path& after_path) {
    using namespace lgaed;
    using namespace winrt::Windows::Data::Json;
    constexpr std::string_view identity="block_0002_0001";
    constexpr std::string_view old_full="3f3f15e209f0d54ce342167a2e007062ebf83bb1784531d40873a75dafd765ef";
    constexpr std::string_view new_full="b80a16cd7af0d46d6628a11784747299e7607317f58f87b48a41019f923157b2";
    constexpr std::string_view old_question="Supón que las operaciones de archivo se completan. Esta versión omite app en la segunda apertura. Predice el diagnóstico visible.";
    constexpr std::string_view new_question="Supón que las aperturas, escrituras y cierres tienen éxito. Esta versión omite std::ios::app en la segunda apertura. Predice el diagnóstico visible.";
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="81fb6029dc88ca86fd412f9084b758a953bc14d20322a98c6dbc7fbc6987d183"
        &&state::sha256(after_bytes)=="d5cafa688c73961ff3f4e60897d23d69f0df7826a9c192e723bca7a1e34c3dcc",
        "The complete U12L3 hypothesis fixtures differ from their pinned Git/candidate bytes");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    const auto old_metadata=windows::parse_lesson_progress_metadata(before.raw_json),metadata=windows::parse_lesson_progress_metadata(after.raw_json);
    const auto& previous=before.pages.at(2).blocks.at(1);const auto& current=after.pages.at(2).blocks.at(1);
    const auto fingerprint=[](const auto& value){return test::compile_check_test_fingerprint(value);};
    const auto original=JsonObject::Parse(winrt::to_hstring(before.raw_json));
    const auto document=JsonObject::Parse(winrt::to_hstring(after.raw_json));
    const auto target=[](const JsonObject& root){return root.GetNamedArray(L"pages").GetObjectAt(2).GetNamedArray(L"blocks").GetObjectAt(1);};
    const auto old_layout=original.GetNamedObject(L"stateLayout"),layout=document.GetNamedObject(L"stateLayout");
    const auto old_editorial=old_layout.GetNamedArray(L"editorial"),editorial=layout.GetNamedArray(L"editorial");
    check(before.id=="cpp_fundamentals__archivos_y_programa_completo__cerrar_reemplazar_y_anadir_datos"
        &&before.id==after.id&&before.locale=="es"&&after.locale=="es"
        &&before.pages.size()==9&&after.pages.size()==9
        &&previous.stable_id==identity&&current.stable_id==identity
        &&previous.id=="segunda_apertura_reemplaza"&&current.id==previous.id
        &&previous.type==BlockType::output_prediction&&current.type==previous.type
        &&previous.question==old_question&&current.question==new_question
        &&fingerprint(target(original))==old_full&&fingerprint(target(document))==new_full,
        "The U12L3 hypothesis clarification changed its exact owner, published pair or type");
    check(previous.answers==std::vector<std::string>{"Lectura incompleta"}&&current.answers==previous.answers
        &&previous.code==current.code&&previous.explanation==current.explanation
        &&previous.review_feedback==current.review_feedback&&previous.output_comparison==current.output_comparison
        &&before.state_layout.lineage==after.state_layout.lineage&&before.state_layout.signatures==after.state_layout.signatures
        &&exercise_key(before,previous)==exercise_key(after,current)
        &&exercise_key(metadata,metadata.pages.at(2).blocks.at(1))==exercise_key(before,previous),
        "The U12L3 question revision changed source, answer, explanation or historical state ownership");
    check(old_editorial.Size()==6&&editorial.Size()==7
        &&fingerprint(old_layout.GetNamedObject(L"family"))==fingerprint(layout.GetNamedObject(L"family"))
        &&fingerprint(old_layout.GetNamedArray(L"origin"))==fingerprint(layout.GetNamedArray(L"origin"))
        &&fingerprint(old_layout.GetNamedArray(L"current"))==fingerprint(layout.GetNamedArray(L"current")),
        "The U12L3 hypothesis revision replaced its family, origin, current map or six-edge prefix");
    for(unsigned index=0;index<old_editorial.Size();++index)
        check(fingerprint(old_editorial.GetAt(index))==fingerprint(editorial.GetAt(index)),
            "The U12L3 question revision rewrote an earlier editorial proof");
    const auto restored=JsonObject::Parse(document.Stringify());
    target(restored).Insert(L"question",JsonValue::CreateStringValue(winrt::to_hstring(old_question)));
    restored.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
    check(fingerprint(restored)==fingerprint(original),
        "The exact U12L3 candidate changed another block, dependency, source or metadata field");
    std::size_t count=0,sources=0;
    for(std::size_t p=0;p<before.pages.size();++p) {
        check(before.pages[p].blocks.size()==after.pages[p].blocks.size(),"The question revision changed another page's shape");
        for(std::size_t b=0;b<before.pages[p].blocks.size();++b) {
            const auto& old=before.pages[p].blocks[b];const auto& now=after.pages[p].blocks[b];++count;
            sources+=!old.code.empty();sources+=!old.initial_code.empty();
            check(old.stable_id==now.stable_id&&exercise_key(before,old)==exercise_key(after,now)
                &&draft_key("programming",before,old)==draft_key("programming",after,now),
                "The U12L3 clarification moved an independent state or editor owner");
            if(p!=2||b!=1)check(fingerprint(JsonObject::Parse(winrt::to_hstring(old.raw_json)))
                ==fingerprint(JsonObject::Parse(winrt::to_hstring(now.raw_json))),
                "The U12L3 clarification altered an independent complete block");
        }
    }
    check(count==33&&sources==8,"The authentic U12L3 fixture lost one of its 33 identities or eight code fields");
    check(before.state_layout.reading_revisions.empty()&&old_metadata.state_layout.reading_revisions.empty(),
        "An unchanged old question acquired unproved prior reading content");
    for(const auto* model:{&after,&metadata}) {
        const auto& revisions=model->state_layout.reading_revisions;
        check(revisions.size()==1&&revisions[0].editorial_prefix==6&&revisions[0].previous_blocks.size()==1
            &&revisions[0].previous_blocks.contains(std::string(identity))
            &&fingerprint(JsonObject::Parse(winrt::to_hstring(revisions[0].previous_blocks.at(std::string(identity)))))==old_full,
            "Runtime/catalog lacks the exact source-bound previous U12L3 question");
    }
    check(after.state_layout.reading_revisions[0].previous_blocks==metadata.state_layout.reading_revisions[0].previous_blocks,
        "Runtime and catalog disagree on the authenticated old U12L3 question");

    // Unit-level payloads exercise preservation independently of UI/playback.
    // Correct, reviewed error and unfinished answer are distinct stored states.
    const auto& compiler=before.pages.at(3).blocks.at(3);
    check(compiler.type==BlockType::cpp_compiler,"The independent U12L3 draft owner changed");
    const auto compiler_key=draft_key("programming",before,compiler);
    for(const auto mode:{0,1,2}) {
        state::StateData data;state::prepare_lesson_state(data,before);
        data.preferences["u12_hypothesis_contract_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
        state::Response response;response.answer=mode==0?"Lectura incompleta":mode==1?"Total=6":"Lectura incom";
        response.correct=mode==0;response.raw_json=R"({"incorrectAttempts":4,"opaque":{"selection":17}})";
        check(evaluate_text_answer(previous,response.answer).correct==response.correct
            &&evaluate_text_answer(current,response.answer).correct==response.correct,
            "The clarified hypothesis changed the evaluator's accepted answer or a rejected/unfinished answer");
        if(mode==1)state::mark_response_reviewed(response,previous);
        state::put_response(data,before,previous,response);
        state::Draft draft;draft.source=compiler.initial_code+"// QA: borrador ajeno\n";draft.input=" entrada\t\r\n";
        draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=3;draft.selection_end=11;
        state::put_draft(data,compiler_key,draft);state::save_current_page(data,before,2);
        const auto saved=data;const auto prior=state::get_response(saved,before,previous);
        const auto old_draft=state::get_draft(saved,compiler_key,compiler.initial_code);
        state::prepare_lesson_state(data,after);state::validate_lesson_state(data,after);
        const auto retained=state::get_response(data,after,current);
        const auto new_draft=state::get_draft(data,compiler_key,after.pages[3].blocks[3].initial_code);
        check(data==saved&&prior&&retained&&prior->raw_json==retained->raw_json&&retained->answer==response.answer
            &&retained->correct==response.correct&&state::response_reviewed(*retained,current)==(mode==1)
            &&new_draft.has_saved==old_draft.has_saved&&new_draft.source==old_draft.source&&new_draft.input==old_draft.input
            &&new_draft.indentation_json==old_draft.indentation_json&&new_draft.selection_start==old_draft.selection_start
            &&new_draft.selection_end==old_draft.selection_end&&state::current_page(data,after)==2,
            "Preparing the corrected U12L3 hypothesis changed a complete response, draft, opaque payload or current page");
    }

    const auto refresh=[&](const JsonObject& root){root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").GetObjectAt(6)
        .Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(fingerprint(target(root)))));};
    const auto no_annotation=[](const JsonObject& root,std::string_view locale="es") {
        const auto raw=winrt::to_string(root.Stringify());
        check(windows::parse_lesson_json(raw,locale).state_layout.reading_revisions.empty(),
            "An unreviewed U12L3 variant borrowed the closed prior question/audio identity");
        check(windows::parse_lesson_progress_metadata(raw,locale).state_layout.reading_revisions.empty(),
            "Catalog metadata borrowed U12L3 old audio for another payload, owner or history");
    };
    for(const auto change:{"question","guide","explanation","owner","family","prefix","position","locale","no_proof","proof_before","later"}) {
        const auto altered=JsonObject::Parse(document.Stringify());const auto map=altered.GetNamedObject(L"stateLayout");
        const auto mode=std::string_view(change);
        if(mode=="question") {target(altered).Insert(L"question",JsonValue::CreateStringValue(L"Otra hipótesis editorial."));refresh(altered);}
        else if(mode=="guide") {target(altered).Insert(L"reviewFeedback",JsonValue::CreateStringValue(L"Otra guía editorial."));refresh(altered);}
        else if(mode=="explanation") {target(altered).Insert(L"explanation",JsonValue::CreateStringValue(L"Otra explicación editorial."));refresh(altered);}
        else if(mode=="owner")altered.Insert(L"id",JsonValue::CreateStringValue(L"another_append_hypothesis_owner"));
        else if(mode=="family")map.GetNamedObject(L"family").GetNamedObject(L"fr").Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="prefix")map.GetNamedArray(L"editorial").GetObjectAt(0).Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="position") {
            const auto blocks=altered.GetNamedArray(L"pages").GetObjectAt(2).GetNamedArray(L"blocks"),ids=map.GetNamedArray(L"current").GetObjectAt(2).GetNamedArray(L"blocks");
            const auto old=blocks.GetAt(0),id=ids.GetAt(0);blocks.SetAt(0,blocks.GetAt(1));blocks.SetAt(1,old);ids.SetAt(0,ids.GetAt(1));ids.SetAt(1,id);
        } else if(mode=="locale") {
            map.Insert(L"locale",JsonValue::CreateStringValue(L"en"));
            map.GetNamedObject(L"family").Insert(L"en",JsonObject::Parse(map.GetNamedObject(L"family").GetNamedObject(L"es").Stringify()));
        } else if(mode=="no_proof")map.GetNamedArray(L"editorial").RemoveAtEnd();
        else if(mode=="proof_before")map.GetNamedArray(L"editorial").GetObjectAt(6).Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else {
            const auto paragraph=altered.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks").GetObjectAt(1);
            const auto old=fingerprint(paragraph);paragraph.Insert(L"text",JsonValue::CreateStringValue(L"Otra prosa posterior compatible."));
            JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(L"block_0000_0001"));
            edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(old)));
            edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(fingerprint(paragraph))));map.GetNamedArray(L"editorial").Append(edge);
        }
        no_annotation(altered,mode=="locale"?"en":"es");
    }
    for(const auto change:{"identity","type","answer","code","proof_after","origin","injected"}) {
        const auto altered=JsonObject::Parse(document.Stringify());const auto block=target(altered),map=altered.GetNamedObject(L"stateLayout");
        const auto mode=std::string_view(change);
        if(mode=="identity")block.Insert(L"id",JsonValue::CreateStringValue(L"another_prediction"));
        else if(mode=="type")block.Insert(L"type",JsonValue::CreateStringValue(L"fill_code"));
        else if(mode=="answer")block.GetNamedArray(L"answers").SetAt(0,JsonValue::CreateStringValue(L"Total=6"));
        else if(mode=="code")block.Insert(L"code",JsonValue::CreateStringValue(L"int main(){}\n"));
        else if(mode=="proof_after")map.GetNamedArray(L"editorial").GetObjectAt(6).Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="origin")map.GetNamedArray(L"origin").GetObjectAt(2).GetNamedArray(L"blocks").GetObjectAt(1).Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else map.Insert(L"reading_revisions",JsonArray{});
        if(mode=="identity"||mode=="type"||mode=="answer"||mode=="code")refresh(altered);
        const auto raw=winrt::to_string(altered.Stringify());
        rejects([&]{(void)windows::parse_lesson_json(raw);},"An incompatible/injected U12L3 state contract passed the native loader");
        rejects([&]{(void)windows::parse_lesson_progress_metadata(raw);},"Catalog accepted an incompatible/injected U12L3 state contract");
    }
    check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes,
        "The U12L3 contract checks rewrote their immutable input fixtures");
    std::cout<<"PASS U12L3 hypothesis contract: exact complete pair and one question only, authentic six-edge predecessor in runtime/catalog, 33 identities and eight code fields, original answers and independent source/input/indentation/selection across correct/error/draft unit states, coherent variants without borrowed reading provenance and incompatible payloads rejected; no compiler execution, student construction or listening claimed\n";
}
