#pragma once

// Included inside lesson_tests.cpp's anonymous namespace. These are isolated
// model payloads, not successful executions, student progress or listening.
void u12_composed_feedback_contract(const std::filesystem::path& before_path,
        const std::filesystem::path& after_path) {
    using namespace lgaed;
    using namespace winrt::Windows::Data::Json;
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="ef77e1f7d47ab04a43690723c0bf18a97d52bb8ab7323c0df48f34997b807d87"
        &&state::sha256(after_bytes)=="72b6f46499f7b902559a51e4a5ee998710489774c49dd42a836e783ad85cd2f1",
        "The U12L4 feedback fixtures differ from their complete pinned publication/proposal bytes");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    const auto old_metadata=windows::parse_lesson_progress_metadata(before.raw_json),metadata=windows::parse_lesson_progress_metadata(after.raw_json);
    const auto original=JsonObject::Parse(winrt::to_hstring(before.raw_json)),document=JsonObject::Parse(winrt::to_hstring(after.raw_json));
    const auto fingerprint=[](const auto& value){return test::compile_check_test_fingerprint(value);};
    const auto target=[](const JsonObject& root,unsigned page,unsigned block){return root.GetNamedArray(L"pages").GetObjectAt(page).GetNamedArray(L"blocks").GetObjectAt(block);};
    const auto old_layout=original.GetNamedObject(L"stateLayout"),layout=document.GetNamedObject(L"stateLayout");
    const auto old_editorial=old_layout.GetNamedArray(L"editorial"),editorial=layout.GetNamedArray(L"editorial");
    check(before.id=="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"
        &&after.id==before.id&&before.locale=="es"&&after.locale=="es"
        &&before.state_layout.version==2&&after.state_layout.version==2&&before.pages.size()==5&&after.pages.size()==5
        &&old_editorial.Size()==15&&editorial.Size()==17
        &&fingerprint(old_editorial)=="317364cd08c3847423c5a3d3a6b8b017e1882f91be895b738361aef13a68a956"
        &&fingerprint(editorial)=="3ff7da20dbe7d2e0709b927606c59890b4dd3d975bfd6e67ad0f44ecb29a0c29",
        "The U12L4 feedback pair lost its owner/language or exact fifteen-to-seventeen proof chain");
    for(const auto field:{L"family",L"origin",L"current",L"additions"})
        check(fingerprint(old_layout.GetNamedValue(field))==fingerprint(layout.GetNamedValue(field)),
            "The U12L4 feedback edit changed the historical family, maps or additions");
    for(unsigned i=0;i<15;++i)check(fingerprint(old_editorial.GetAt(i))==fingerprint(editorial.GetAt(i)),
        "The U12L4 feedback edit rewrote an earlier editorial proof");
    struct Change{const char* id;unsigned page,block;const char* before;const char* after;};
    constexpr std::array<Change,2> changes{{
        {"block_0002_0003",2,3,"628f37fb8e1fb1f82180b90a6ff241b9e868482914e4ce9dd84149e453b8e00d","2a22d63af34fb093c3feeef538fdb794fa6b57074fddcc601b473162937043b7"},
        {"block_0004_0001",4,1,"cf06a21a5f5a9b85cc94cf0d4e41cebc672813a7a82c30728832291dc25bcdb8","4bddac581f21a70aa212ec0039f9ab2ab64badbcd315419e2e149a7ae7240166"}
    }};
    const auto restored=JsonObject::Parse(document.Stringify());
    for(unsigned i=0;i<changes.size();++i){const auto& item=changes[i];const auto edge=editorial.GetObjectAt(15+i);
        check(fingerprint(target(original,item.page,item.block))==item.before&&fingerprint(target(document,item.page,item.block))==item.after
            &&edge.Size()==3&&winrt::to_string(edge.GetNamedString(L"id"))==item.id
            &&winrt::to_string(edge.GetNamedString(L"beforeSha256"))==item.before
            &&winrt::to_string(edge.GetNamedString(L"afterSha256"))==item.after,
            "The U12L4 feedback pair changed a complete reviewed payload or its ordered proof");
        restored.GetNamedArray(L"pages").GetObjectAt(item.page).GetNamedArray(L"blocks").SetAt(item.block,target(original,item.page,item.block));
    }
    restored.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
    restored.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
    check(fingerprint(restored)==fingerprint(original),"The U12L4 feedback edit changed fields outside the two reviewed payloads");
    const auto& old_compiler=before.pages[2].blocks[3];const auto& compiler=after.pages[2].blocks[3];
    const auto& old_prediction=before.pages[4].blocks[1];const auto& prediction=after.pages[4].blocks[1];
    check(compiler.type==BlockType::cpp_compiler&&compiler.stable_id==changes[0].id&&compiler.initial_code==old_compiler.initial_code
        &&old_compiler.output_check&&compiler.output_check&&!old_compiler.compiler_revision&&!compiler.compiler_revision
        &&reference_objective_frame(before,old_compiler)==reference_objective_frame(after,compiler)
        &&compiler.output_check->cases.size()==1&&old_compiler.output_check->cases.size()==1,
        "Editorial feedback changed the compiler's program/objective identity or invented a compiler migration");
    const auto& old_case=old_compiler.output_check->cases.front();const auto& current_case=compiler.output_check->cases.front();
    constexpr std::string_view expected="Validos=2;Rechazados=1\nValidos=0;Rechazados=0\nValidos=5;Rechazados=3\n";
    constexpr std::string_view explanation="La salida coincide con las tres líneas pedidas, en su orden y con un salto después de cada una. Comprueba también que tu función forma los campos desde sus parámetros, devuelve la cadena y deja la presentación a main; la coincidencia de la salida no certifica esa organización.";
    constexpr std::string_view empty_review="La ejecución terminó sin mostrar texto. Completa la composición y las tres llamadas, guarda sus resultados y envíalos a la salida desde main.";
    constexpr std::string_view partial_review="Esta salida contiene sólo dos líneas y no tiene salto final. Revisa la tercera llamada y los saltos solicitados para la batería completa.";
    check(old_case.outputs==std::vector<std::string>{std::string(expected)}&&current_case.outputs==old_case.outputs
        &&old_case.standard_input.empty()&&current_case.standard_input.empty()&&old_case.explanation.empty()&&old_case.review_feedback.empty()
        &&current_case.explanation==explanation&&current_case.review_feedback.size()==2
        &&current_case.review_feedback[0].outputs==std::vector<std::string>{""}&&current_case.review_feedback[0].text==empty_review
        &&current_case.review_feedback[1].outputs==std::vector<std::string>{"Validos=2;Rechazados=1\nValidos=0;Rechazados=0"}
        &&current_case.review_feedback[1].text==partial_review,
        "The U12L4 output case lost exact final LF, its unchanged oracle or complete case/incorrect guides");
    check(prediction.type==BlockType::output_prediction&&prediction.stable_id==changes[1].id&&prediction.id=="destino_archivo_no_es_stdout"
        &&old_prediction.question=="Supón que se completan apertura y escritura. ¿Qué texto aparece en la salida estándar de este programa?"
        &&prediction.question=="Supón que la apertura, la escritura y el cierre tienen éxito. ¿Qué texto aparece en la salida estándar de este programa?"
        &&prediction.code==old_prediction.code&&prediction.answers==std::vector<std::string>{"Guardado"}
        &&prediction.answers==old_prediction.answers&&prediction.explanation==old_prediction.explanation,
        "The U12L4 hypothesis changed source, expected Guardado, explanation or exact success assumption");
    FreshExecution execution{9,true,true,0,"",std::string(expected),{}};
    check(evaluate_output_check(*compiler.output_check,execution,9).verdict==OutputVerdict::correct
        &&evaluate_output_check(*compiler.output_check,execution,9).feedback==explanation,
        "The unchanged complete output no longer returns the exact pedagogical explanation");
    for(const auto& sample:std::array<std::pair<std::string,std::string_view>,2>{{{"",empty_review},{"Validos=2;Rechazados=1\nValidos=0;Rechazados=0",partial_review}}}){
        execution.standard_output=sample.first;const auto verdict=evaluate_output_check(*compiler.output_check,execution,9);
        check(verdict.verdict==OutputVerdict::incorrect&&verdict.feedback==sample.second,
            "An empty or real two-line partial output bypassed its exact rejected-output guide");
    }
    execution.standard_output=std::string(expected);execution.standard_output.pop_back();
    check(evaluate_output_check(*compiler.output_check,execution,9).verdict==OutputVerdict::incorrect
        &&evaluate_output_check(*compiler.output_check,execution,10).verdict==OutputVerdict::unavailable,
        "The feedback update started trimming a required final LF or reusing stale execution");
    check(before.state_layout.signatures==after.state_layout.signatures&&before.state_layout.lineage==after.state_layout.lineage,
        "The U12L4 feedback edit altered storage signatures or family lineage");
    const std::array<std::size_t,5> shape{5,6,5,6,3};std::size_t identities=0;
    for(std::size_t p=0;p<shape.size();++p){check(before.pages[p].blocks.size()==shape[p]&&after.pages[p].blocks.size()==shape[p],"The U12L4 feedback edit moved a block");
        for(std::size_t b=0;b<shape[p];++b){const auto& old=before.pages[p].blocks[b];const auto& current=after.pages[p].blocks[b];++identities;
            check(old.id==current.id&&old.stable_id==current.stable_id&&old.type==current.type
                &&exercise_key(before,old)==exercise_key(after,current)&&draft_key("programming",before,old)==draft_key("programming",after,current)
                &&exercise_key(after,current)==exercise_key(metadata,metadata.pages[p].blocks[b]),
                "The U12L4 feedback edit changed an ID or response/draft address");
        }
    }
    check(identities==25,"The U12L4 feedback pair lost one of its 25 historical identities");
    for(const auto* model:{&before,&old_metadata,&after,&metadata}){
        const auto& revisions=model->state_layout.reading_revisions;
        check(revisions.size()==(model==&before||model==&old_metadata?1u:2u)
            &&revisions[0].editorial_prefix==8&&revisions[0].positional_page_identity&&revisions[0].previous_blocks.size()==8,
            "Published15/proposed17 lost the separate eight-block positional p1/p2 bridge");
        for(const auto& [id,raw]:revisions[0].previous_blocks){(void)raw;const auto at=stable_block_position(*model,id);
            check(at&&at->page<2,"The legacy positional bridge borrowed a later p3/p4/p5 script");}
        if(revisions.size()==2){const auto& revision=revisions[1];
            check(revision.editorial_prefix==15&&!revision.positional_page_identity&&revision.previous_blocks.size()==2,
                "The reviewed v2 feedback/question archive borrowed the positional page identity or wrong prefix");
            const auto prior_pages=JsonArray::Parse(document.GetNamedArray(L"pages").Stringify());
            for(const auto& item:changes){const auto found=revision.previous_blocks.find(item.id);
                check(found!=revision.previous_blocks.end()&&fingerprint(JsonObject::Parse(winrt::to_hstring(found->second)))==item.before,
                    "The reviewed archive did not contain the complete authentic compiler/question predecessor");
                prior_pages.GetObjectAt(item.page).GetNamedArray(L"blocks").SetAt(item.block,JsonObject::Parse(winrt::to_hstring(found->second)));
            }
            check(fingerprint(prior_pages)==fingerprint(original.GetNamedArray(L"pages")),
                "The new archive did not reconstruct the actual five-page published15 snapshot");
        }
    }
    check(before.state_layout.reading_revisions[0].previous_blocks==after.state_layout.reading_revisions[0].previous_blocks
        &&after.state_layout.reading_revisions[1].previous_blocks==metadata.state_layout.reading_revisions[1].previous_blocks,
        "The two independent archive families diverged between runtime/catalog or published/proposed versions");
    for(const auto mode:{0,1,2}){
        state::StateData data;state::prepare_lesson_state(data,before);
        data.preferences["u12_composed_feedback_contract_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
        for(std::size_t p=0;p<shape.size();++p)for(std::size_t b=0;b<shape[p];++b){const auto& old=before.pages[p].blocks[b];
            if(old.type!=BlockType::multiple_choice&&old.type!=BlockType::output_prediction)continue;
            state::Response response;response.correct=mode==0;response.raw_json=R"({"incorrectAttempts":4,"opaque":{"selection":17}})";
            if(old.type==BlockType::multiple_choice)response.selected=mode==0?static_cast<std::int64_t>(*old.correct_index):mode==1?static_cast<std::int64_t>((*old.correct_index+1)%old.options.size()):-1;
            else response.answer=mode==0?old.answers.front():mode==1?"QA incorrecto":"QA incompleto";
            if(mode==1)state::mark_response_reviewed(response,old);state::put_response(data,before,old,response);
        }
        const auto key=draft_key("programming",before,old_compiler);state::Draft draft;
        draft.source=old_compiler.initial_code+"// QA: conservar borrador\n";draft.input=" entrada\t\r\n";
        draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=3;draft.selection_end=11;
        state::put_draft(data,key,draft);state::save_current_page(data,before,4);const auto saved=data;
        state::prepare_lesson_state(data,after);state::validate_lesson_state(data,after);
        check(data==saved,"The feedback-only edit changed an existing response, draft, attempt, preference or page address");
        const auto reopened=state::get_draft(data,key,compiler.initial_code);
        check(reopened.has_saved&&reopened.source==draft.source&&reopened.input==draft.input&&reopened.indentation_json==draft.indentation_json
            &&reopened.selection_start==draft.selection_start&&reopened.selection_end==draft.selection_end&&state::current_page(data,after)==4,
            "The U12L4 feedback edit altered independent draft source/input/indentation/selection or current page");
        for(std::size_t p=0;p<shape.size();++p)for(std::size_t b=0;b<shape[p];++b){const auto& old=before.pages[p].blocks[b];
            if(old.type!=BlockType::multiple_choice&&old.type!=BlockType::output_prediction)continue;
            const auto response=state::get_response(data,after,after.pages[p].blocks[b]);
            check(response&&response->correct==(mode==0)&&state::response_reviewed(*response,after.pages[p].blocks[b])==(mode==1),
                "A correct, reviewed wrong or unfinished answer changed status after the U12L4 feedback edit");
        }
    }
    // Explicit platform-API unit doubles: the evaluator result is synthetic.
    // The separate native view fixture supplies real compilation evidence.
    {
        state::StateData data;state::prepare_lesson_state(data,before);
        windows::CppRunRequest captured;captured.revision=9;captured.source_utf8="// synthetic captured program for receipt contract only\n";
        FreshExecution accepted{9,true,true,0,"",std::string(expected),{}};
        check(windows::record_compiler_puzzle_case(data,before,old_compiler,captured,accepted,"Correcto")
            &&windows::compiler_puzzle_feedback(data,before,old_compiler)=="Correcto",
            "The synthetic successful predecessor did not retain its original compiler receipt/text");
        const auto saved=data;state::prepare_lesson_state(data,after);
        windows::validate_compiler_puzzle_state(data,after,compiler);
        check(data==saved&&windows::compiler_puzzle_completed(data,after,compiler)
            &&windows::compiler_puzzle_case_count(data,after,compiler)==1
            &&windows::compiler_puzzle_feedback(data,after,compiler)=="Correcto\n"+std::string(explanation)&&data==saved,
            "The exact feedback-only migration lost the old success or failed to present its new explanation without modifying stored evidence");
        auto unreviewed=after;unreviewed.state_layout.reading_revisions.clear();
        check(windows::compiler_puzzle_feedback(data,unreviewed,unreviewed.pages[2].blocks[3])=="Correcto"&&data==saved,
            "A model without the closed review borrowed the special historical feedback upgrade");
        state::StateData empty;state::prepare_lesson_state(empty,after);const auto unchanged=empty;
        check(!windows::compiler_puzzle_completed(empty,after,compiler)
            &&windows::compiler_puzzle_feedback(empty,after,compiler).empty()&&empty==unchanged,
            "The new explanation fabricated a completed objective or altered a fresh unit profile");
    }
    {
        state::StateData data;state::prepare_lesson_state(data,before);
        windows::CppRunRequest captured;captured.revision=9;captured.source_utf8="// synthetic rejected program for review contract only\n";
        FreshExecution rejected{9,true,true,0,"","",{}};
        check(windows::record_compiler_puzzle_review(data,before,old_compiler,captured,rejected,"Incorrecto")
            &&windows::compiler_puzzle_review(data,before,old_compiler,captured)=="Incorrecto",
            "The synthetic predecessor rejection lost its exact saved-review provenance");
        const auto saved=data;state::prepare_lesson_state(data,after);
        check(data==saved&&!windows::compiler_puzzle_completed(data,after,compiler)
            &&windows::compiler_puzzle_review(data,after,compiler,captured).empty()&&data==saved,
            "A changed review guide resurrected stale feedback, invented a success or rewrote original rejection data");
    }
    const auto refresh=[&](const JsonObject& root,unsigned changed){const auto& item=changes[changed];
        root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").GetObjectAt(15+changed).Insert(L"afterSha256",
            JsonValue::CreateStringValue(winrt::to_hstring(fingerprint(target(root,item.page,item.block)))));};
    const auto no_archive=[](const JsonObject& root,std::string_view locale="es"){
        const auto raw=winrt::to_string(root.Stringify());
        check(windows::parse_lesson_json(raw,locale).state_layout.reading_revisions.empty(),"A coherent unreviewed U12L4 variant borrowed the closed historical archive");
        check(windows::parse_lesson_progress_metadata(raw,locale).state_layout.reading_revisions.empty(),"Catalog borrowed the closed U12L4 history for a different coherent publication");
    };
    for(const auto change:{"owner","locale","family","origin","current","prefix","proof_order","question","explanation","guide","neighbor"}){
        const auto root=JsonObject::Parse(document.Stringify()),map=root.GetNamedObject(L"stateLayout");const auto mode=std::string_view(change);
        if(mode=="owner")root.Insert(L"id",JsonValue::CreateStringValue(L"another_composed_feedback_owner"));
        else if(mode=="locale"){map.Insert(L"locale",JsonValue::CreateStringValue(L"en"));map.GetNamedObject(L"family").Insert(L"en",JsonObject::Parse(map.GetNamedObject(L"family").GetNamedObject(L"es").Stringify()));}
        else if(mode=="family")map.GetNamedObject(L"family").GetNamedObject(L"fr").Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="origin"){
            const auto blocks=map.GetNamedArray(L"origin").GetObjectAt(0).GetNamedArray(L"blocks");const auto first=blocks.GetAt(0);blocks.SetAt(0,blocks.GetAt(2));blocks.SetAt(2,first);
            map.GetNamedObject(L"family").GetNamedObject(L"es").Insert(L"origin",JsonArray::Parse(map.GetNamedArray(L"origin").Stringify()));
        }else if(mode=="current"){
            const auto blocks=root.GetNamedArray(L"pages").GetObjectAt(4).GetNamedArray(L"blocks"),ids=map.GetNamedArray(L"current").GetObjectAt(4).GetNamedArray(L"blocks");
            const auto first=blocks.GetAt(0),id=ids.GetAt(0);blocks.SetAt(0,blocks.GetAt(2));blocks.SetAt(2,first);ids.SetAt(0,ids.GetAt(2));ids.SetAt(2,id);
        }else if(mode=="prefix")map.GetNamedArray(L"editorial").GetObjectAt(0).Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="proof_order"){const auto edges=map.GetNamedArray(L"editorial");const auto first=edges.GetAt(15);edges.SetAt(15,edges.GetAt(16));edges.SetAt(16,first);}
        else if(mode=="question"){target(root,4,1).Insert(L"question",JsonValue::CreateStringValue(L"Otra hipótesis editorial coherente."));refresh(root,1);}
        else if(mode=="explanation"||mode=="guide"){
            const auto item=target(root,2,3).GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
            if(mode=="explanation")item.Insert(L"explanation",JsonValue::CreateStringValue(L"Otra explicación compatible."));
            else item.GetNamedArray(L"reviewFeedback").GetObjectAt(0).Insert(L"text",JsonValue::CreateStringValue(L"Otra guía compatible."));refresh(root,0);
        }else{
            const auto item=target(root,4,2);const auto prior=fingerprint(item);item.Insert(L"text",JsonValue::CreateStringValue(L"Otra prosa posterior no revisada."));
            JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(L"block_0004_0002"));edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(prior)));
            edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(fingerprint(item))));map.GetNamedArray(L"editorial").Append(edge);
        }
        no_archive(root,mode=="locale"?"en":"es");
    }
    for(const auto change:{"identity","answer","code","initial","output","stdin","proof_after","origin_signature","injected"}){
        const auto root=JsonObject::Parse(document.Stringify()),map=root.GetNamedObject(L"stateLayout");const auto mode=std::string_view(change);unsigned edited=1;
        if(mode=="identity")target(root,4,1).Insert(L"id",JsonValue::CreateStringValue(L"another_prediction_owner"));
        else if(mode=="answer")target(root,4,1).GetNamedArray(L"answers").SetAt(0,JsonValue::CreateStringValue(L"Error escritura"));
        else if(mode=="code")target(root,4,1).Insert(L"code",JsonValue::CreateStringValue(L"int main(){}\n"));
        else if(mode=="initial"){target(root,2,3).Insert(L"initialCode",JsonValue::CreateStringValue(L"int main(){}\n"));edited=0;}
        else if(mode=="output"||mode=="stdin"){
            const auto item=target(root,2,3).GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);edited=0;
            if(mode=="output")item.GetNamedArray(L"outputs").SetAt(0,JsonValue::CreateStringValue(L"Otro resultado\n"));
            else item.Insert(L"standardInput",JsonValue::CreateStringValue(L"Otra entrada"));
        }else if(mode=="proof_after")map.GetNamedArray(L"editorial").GetObjectAt(16).Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="origin_signature")map.GetNamedArray(L"origin").GetObjectAt(4).GetNamedArray(L"blocks").GetObjectAt(1).Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else map.Insert(L"reading_revisions",JsonArray{});
        if(mode=="identity"||mode=="answer"||mode=="code"||mode=="initial"||mode=="output"||mode=="stdin")refresh(root,edited);
        const auto raw=winrt::to_string(root.Stringify());
        rejects([&]{(void)windows::parse_lesson_json(raw);},"An incompatible U12L4 objective/source/identity passed the runtime loader");
        rejects([&]{(void)windows::parse_lesson_progress_metadata(raw);},"An incompatible U12L4 objective/source/identity passed catalog metadata");
    }
    check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes,
        "The U12L4 feedback model contract rewrote immutable fixture inputs");
    std::cout<<"PASS U12L4 feedback contract: exact published15/proposed17 pair, 25 unchanged identities and response/draft addresses, unchanged compiler objective and final-LF oracle with two precise rejection guides, separate positional p1/p2 and stable prefix15 two-block archives in runtime/catalog, response/draft/attempt preservation and closed provenance negatives; no actual execution, UI or listening claim\n";
}
