#pragma once
// Included after the real reader Fixture. Both destinations are complete Git
// publications. QA answers use native controls; no listening position is seeded.
void u11_remainder_editorial_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,
        unsigned phase,bool published30){
    check(phase<2,"Invalid remainder reader phase");
    const auto stage=published30?"published30":"published26";
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="e46f006765a5fa313430b13dd5b1b0a305a6add572beea529fe84f14ab0bf739"
        &&state::sha256(after_bytes)==(published30?"be8de9afbb1b2bbb5ac07cacb8e8935b448fae295ca7e5235fc51ddb0a924b49":
            "0589de4ab11564c318ff0250ad822e01dbef10b7e2caae58f32d7ac7e20f6770"),
        "Remainder reader fixtures differ from their complete immutable Git publications");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    const auto at=identity_position(before,"block_0002_0002"),new_at=identity_position(after,"block_0002_0002");
    const auto draft_at=identity_position(before,"block_0003_0004"),new_draft_at=identity_position(after,"block_0003_0004");
    const auto& old=before.pages.at(at.page).blocks.at(at.block);
    const auto& revised=after.pages.at(new_at.page).blocks.at(new_at.block);
    const auto& compiler=before.pages.at(draft_at.page).blocks.at(draft_at.block);
    const auto& new_compiler=after.pages.at(new_draft_at.page).blocks.at(new_draft_at.block);
    check(before.id=="cpp_fundamentals__texto_interpretacion_y_flujos__resto_y_asociacion_en_el_interprete"
        &&after.id==before.id&&before.locale=="es"&&after.locale=="es"&&at==new_at&&at.page==2&&at.block==2
        &&before.state_layout.version==2&&after.state_layout.version==2&&before.state_layout.lineage==after.state_layout.lineage
        &&old.type==curriculum::BlockType::order_code&&revised.type==old.type
        &&old.id==before.id+"__e04"&&revised.id==old.id&&old.question==revised.question
        &&old.fragments==revised.fragments&&old.correct_order==std::vector<std::size_t>{1,2,3,0}
        &&revised.correct_order==old.correct_order&&old.review_feedback==revised.review_feedback
        &&state::response_storage_key(before,old)==state::response_storage_key(after,revised)
        &&state::sha256(old.explanation)=="c3df0179bd09fbe11d54af6e11c1fb18a140dbc9ee57894070ef1bd11eb25d9b"
        &&state::sha256(revised.explanation)=="0d826e2c9c883119e498ec90650b531ec9fe8a1ed3ae5417dfa653e2f3c7238d",
        "Remainder reader changed its original owner, ordering, answers or exact editorial explanation");
    using winrt::Windows::Data::Json::JsonObject;
    const auto old_edges=JsonObject::Parse(winrt::to_hstring(before.state_layout.raw_json)).GetNamedArray(L"editorial");
    const auto new_edges=JsonObject::Parse(winrt::to_hstring(after.state_layout.raw_json)).GetNamedArray(L"editorial");
    check(old_edges.Size()==17&&new_edges.Size()==(published30?30u:26u),"Remainder reader selected another editorial publication");
    for(unsigned i=0;i<old_edges.Size();++i)check(old_edges.GetAt(i).Stringify()==new_edges.GetAt(i).Stringify(),
        "Remainder reader rewrote the historical editorial prefix");
    const auto archive=std::ranges::find(after.state_layout.reading_revisions,std::size_t{17},&curriculum::StateReadingRevision::editorial_prefix);
    check(archive!=after.state_layout.reading_revisions.end()&&archive->previous_blocks.contains(old.stable_id)
        &&test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(archive->previous_blocks.at(old.stable_id))))
            ==test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(old.raw_json))),
        "Remainder reader lacks the authenticated original activity at its seventeen-edge source");
    const auto& old_prose=before.pages.at(at.page).blocks.at(1);
    const auto& new_prose=after.pages.at(at.page).blocks.at(1);
    check((old_prose.text!=new_prose.text)==published30
        &&before.pages.at(at.page).blocks.at(0).text==after.pages.at(at.page).blocks.at(0).text
        &&before.pages.at(at.page).blocks.at(3).text==after.pages.at(at.page).blocks.at(3).text,
        "Remainder reader lost its unchanged heading/tail or the later preceding-paragraph revision");
    check(archive->previous_blocks.size()==(published30?2u:1u)&&!archive->positional_page_identity
        &&!archive->bibliography_only&&archive->previous_explanation_semantics.empty()
        &&archive->previous_blocks.contains(old_prose.stable_id)==published30,
        "Remainder reading provenance must own only the changed sources of its original stable page");
    if(published30)check(test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(archive->previous_blocks.at(old_prose.stable_id))))
        ==test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(old_prose.raw_json))),
        "Remainder reading provenance lost the complete original preceding paragraph");
    const auto published=JsonObject::Parse(winrt::to_hstring(after.raw_json));
    const auto original_page=JsonObject::Parse(winrt::to_hstring(before.raw_json)).GetNamedArray(L"pages").GetObjectAt(2);
    const auto reconstructed=JsonObject::Parse(published.GetNamedArray(L"pages").GetObjectAt(2).Stringify());
    const auto reconstructed_blocks=reconstructed.GetNamedArray(L"blocks");
    for(unsigned index=0;index<reconstructed_blocks.Size();++index){
        const auto found=archive->previous_blocks.find(after.state_layout.current.at(at.page).blocks.at(index));
        if(found!=archive->previous_blocks.end())reconstructed_blocks.SetAt(index,JsonObject::Parse(winrt::to_hstring(found->second)));
    }
    check(test::compile_check_test_fingerprint(reconstructed)==test::compile_check_test_fingerprint(original_page),
        "Remainder reading provenance combined sources that never formed the original complete page");
    const auto same_archive=[&](const curriculum::Lesson& loaded){
        const auto found=std::ranges::find(loaded.state_layout.reading_revisions,std::size_t{17},&curriculum::StateReadingRevision::editorial_prefix);
        check(found!=loaded.state_layout.reading_revisions.end()&&found->previous_blocks==archive->previous_blocks
            &&found->positional_page_identity==archive->positional_page_identity&&found->bibliography_only==archive->bibliography_only
            &&found->previous_explanation_semantics==archive->previous_explanation_semantics,
            "The full and progress-only loaders must authenticate the same remainder reading sources");
    };
    const auto canonical_source=winrt::to_string(published.Stringify());
    same_archive(windows::parse_lesson_json(canonical_source));
    same_archive(windows::parse_lesson_progress_metadata(canonical_source));
    // Coherent QA variants still load normally. They cannot inherit an archive
    // authenticated for another ordered history, payload or semantic authoring.
    const auto without_archive=[&](const JsonObject& source,const char* message){
        const auto absent=[&](const curriculum::Lesson& loaded){
            check(std::ranges::none_of(loaded.state_layout.reading_revisions,[](const auto& revision){return revision.editorial_prefix==17;}),message);
        };
        const auto bytes=winrt::to_string(source.Stringify());
        absent(windows::parse_lesson_json(bytes));absent(windows::parse_lesson_progress_metadata(bytes));
    };
    const auto reordered=JsonObject::Parse(published.Stringify());
    const auto reordered_edges=reordered.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    const auto first_edge=reordered_edges.GetAt(0),second_edge=reordered_edges.GetAt(1);
    check(first_edge.GetObject().GetNamedString(L"id")!=second_edge.GetObject().GetNamedString(L"id"),
        "The history negative requires independent owner chains");
    reordered_edges.SetAt(0,second_edge);reordered_edges.SetAt(1,first_edge);
    without_archive(reordered,"A coherent reordered history inherited the published remainder archive");
    const auto changed=JsonObject::Parse(published.Stringify());
    const auto changed_block=changed.GetNamedArray(L"pages").GetObjectAt(2).GetNamedArray(L"blocks").GetObjectAt(2);
    changed_block.Insert(L"explanation",winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(revised.explanation+" QA")));
    const auto changed_edge=changed.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").GetObjectAt(19);
    check(changed_edge.GetNamedString(L"id")==winrt::to_hstring(old.stable_id),"The explanation negative selected another owner");
    changed_edge.Insert(L"afterSha256",winrt::Windows::Data::Json::JsonValue::CreateStringValue(
        winrt::to_hstring(test::compile_check_test_fingerprint(changed_block))));
    without_archive(changed,"A coherent different explanation inherited the published remainder archive");
    if(published30){
        const auto unmarked=JsonObject::Parse(published.Stringify());
        const auto fields=unmarked.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");bool removed=false;
        for(unsigned index=0;index<fields.Size();++index){const auto field=fields.GetObjectAt(index);
            if(field.GetNamedString(L"block")==winrt::to_hstring(old_prose.stable_id)&&field.GetNamedString(L"path")==L"/text"){
                fields.RemoveAt(index);removed=true;break;
            }
        }
        check(removed,"The semantic negative lost the actual current paragraph descriptor");
        without_archive(unmarked,"Different semantic authoring inherited the published remainder archive");
    }
    const auto draft_key=curriculum::draft_key("programming",before,compiler);
    check(compiler.type==curriculum::BlockType::cpp_compiler&&new_compiler.type==compiler.type
        &&draft_key==curriculum::draft_key("programming",after,new_compiler),"Remainder reader changed the independent compiler draft owner");
    state::Draft draft;draft.source=compiler.initial_code+"\n// QA: borrador propio conservado\n";draft.input=" entrada\t\r\n";
    draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=3;draft.selection_end=11;
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u11-remainder-reader"/stage/
        ("pcm-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64())+'-'+std::to_string(phase));
    test::CompilerReaderEntrySetup prerequisites;
    Fixture f(backend,false,true,true,"es",before,true,at.page,true,true,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[&](state::StateData& data){
        // Earlier ordinary answers are disclosed synthetic QA prerequisites,
        // evaluated by the same contracts. The focal answer remains unset;
        // this page precedes every compiler, so no execution receipt is made.
        prerequisites=test::prepare_compiler_reader_entry(data,before,at.page);
        // Select Spanish interface verdicts independently of the narration voice/runtime.
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        state::put_draft(data,draft_key,draft);
        data.preferences["u11_remainder_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");},cache);
    struct FailureShutdown{std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}} shutdown{backend};
    check(!IsWindowVisible(f.host)&&lesson_page_state(f.view).index==at.page
        &&narration::read_history(read_lesson_state(f.view)).entries.empty(),
        "Remainder QA exposed a window, opened another page or fabricated listening history");
    const auto control=[&](int part){return child(f.view,1000+static_cast<int>(at.block)*100+part);};
    const auto submit=[&]{check(IsWindowEnabled(control(3)),"The actual remainder Check control is disabled");
        SendMessageW(control(3),BM_CLICK,0,0);f.reader->pause();f.ready();
        check(!f.snapshot().playing&&!f.snapshot().allocated,"Remainder feedback overrode an explicit pause");};
    // The untouched identity order is deliberately not a submitted attempt.
    // A real first tap selects that same first row and enables Check.
    f.reader->pause();
    const auto first_row=GetDlgItem(control(5),100);check(first_row!=nullptr,"Missing original remainder fragment");
    SendMessageW(first_row,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));SendMessageW(first_row,WM_LBUTTONUP,0,MAKELPARAM(4,4));pump();
    submit();const auto wrong=state::get_response(read_lesson_state(f.view),before,old);
    check(wrong&&!wrong->correct&&wrong->order==std::vector<std::int64_t>{0,1,2,3}&&state::response_reviewed(*wrong,old),
        "The original remainder order was not rejected by the real controls");
    auto order=wrong->order;
    for(std::size_t target=0;target<old.correct_order.size();++target){
        auto index=static_cast<std::size_t>(std::ranges::find(order,static_cast<std::int64_t>(old.correct_order[target]))-order.begin());
        check(index<order.size(),"The remainder answer lost an actual fragment");
        while(index>target){const auto row=GetDlgItem(control(5),100+static_cast<int>(order[index]));
            check(row!=nullptr,"The remainder order lost a native fragment row");
            SendMessageW(row,WM_SYSKEYDOWN,VK_UP,LPARAM{1}<<29);pump();std::swap(order[index],order[index-1]);--index;}
    }
    submit();const auto protected_data=read_lesson_state(f.view);
    const auto accepted=state::get_response(protected_data,before,old);
    check(accepted&&accepted->correct&&accepted->order==order&&accepted->reviewed_answer_identity.empty(),
        "The repaired remainder order was not accepted through its native controls");
    const auto prepared_history=narration::read_history(protected_data);
    check(std::ranges::all_of(prepared_history.entries,[](const auto& slot){return !slot.position.finished
        &&(!slot.position.heard_prefix||slot.position.heard_prefix->microseconds==0);}),
        "Answer preparation fabricated positive remainder listening evidence");
    struct Recording{LessonReaderSegmentSnapshot part;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Recording> recordings;
    for(const auto& part:f.snapshot().segments)if(part.available&&!part.recording.empty())
        recordings.push_back({part,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});
    f.reader->restart();f.reader->pause();f.reader->toggle();
    wait([&]{const auto value=f.snapshot();const auto& part=value.segments.at(value.index);
        return value.playing&&part.block==(phase==0?0:at.block)&&part.feedback==(phase==1)
            &&value.offset>150&&value.offset+150<part.duration_ms;
    },phase==0?"real unchanged remainder-heading pause":"real original remainder-explanation pause",120000);
    f.reader->pause();const auto previous=f.snapshot();(void)verify_feedback_recording(previous.segments.at(previous.index));
    const auto old_history=narration::read_history(read_lesson_state(f.view));
    const auto old_slot=std::ranges::find(old_history.entries,previous.key,&narration::Bookmark::key);
    check(!previous.playing&&!previous.allocated&&!previous.finished&&old_history.entries.size()==1
        &&old_slot!=old_history.entries.end()&&old_slot->position.milliseconds==previous.offset
        &&old_slot->position.heard_prefix&&old_slot->position.heard_prefix->microseconds>0,
        "The remainder bookmark was not created by actual partial PCM playback");
    const auto reopen=[&]{f.reader.reset();check(flush_lesson_view(f.view),"Cannot save the actual remainder pause");
        DestroyWindow(f.view);f.view=nullptr;LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,after,std::move(hooks));
        check(f.view&&lesson_page_state(f.view).index==at.page,"Cannot reopen the published remainder page");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
    };
    reopen();const auto mapped=f.snapshot();
    const auto first_changed=std::ranges::find_if(mapped.segments,[&](const auto& part){
        return published30?(part.block==1&&!part.feedback):(part.block==at.block&&part.feedback&&part.correct);});
    check(first_changed!=mapped.segments.end(),"The first changed remainder passage is absent from the reader");
    const auto first_changed_index=static_cast<std::size_t>(first_changed-mapped.segments.begin());
    check(mapped.key!=previous.key&&!mapped.playing&&!mapped.allocated&&!mapped.finished,
        "Published remainder speech reused the obsolete key or started automatically");
    if(phase==0)check(mapped.offset==previous.offset&&mapped.segments.at(mapped.index).region==previous.segments.at(previous.index).region
        &&mapped.segments.at(mapped.index).recording==previous.segments.at(previous.index).recording
        &&mapped.heard_through<=static_cast<std::int64_t>(first_changed_index),
        "The unchanged remainder heading lost its compatible pause or credited later changed speech");
    else check(mapped.index==first_changed_index&&mapped.offset==0&&mapped.heard_through<=static_cast<std::int64_t>(first_changed_index),
        "Remainder migration skipped the first changed passage or inherited old listening time");
    const auto migrated=narration::read_history(read_lesson_state(f.view));
    check(migrated.entries.size()==1&&migrated.entries.front().key==mapped.key&&migrated.entries.front().page==old_slot->page
        &&migrated.active_owner==old_history.active_owner&&migrated.active_key==mapped.key,
        "Remainder migration did not relabel the same bounded page slot");
    if(phase==0)check(migrated.entries.front().position.heard_prefix==old_slot->position.heard_prefix,
        "A compatible remainder heading lost its actual recording certificate");
    else check(!migrated.entries.front().position.heard_prefix||migrated.entries.front().position.heard_prefix->microseconds==0,
        "A revised remainder passage inherited a positive certificate from old speech");
    const auto preserve=[&]{const auto data=read_lesson_state(f.view);const auto restored=state::get_response(data,after,revised);
        check(restored&&restored->raw_json==accepted->raw_json&&restored->correct&&restored->order==accepted->order
            &&restored->reviewed_answer_identity==accepted->reviewed_answer_identity,
            "Remainder narration changed the actually accepted response or its attempt data");
        for(const auto domain:{"attempt_reserve_seconds_test","u11_remainder_reader_qa","u11_compiler_reader_synthetic_prerequisites_qa"}){
            const auto old_values=protected_data.preferences.find(domain),new_values=data.preferences.find(domain);
            check((old_values==protected_data.preferences.end())==(new_values==data.preferences.end())
                &&(old_values==protected_data.preferences.end()||old_values->second==new_values->second),
                "Remainder narration changed the existing attempt reserve or opaque preferences");}
        const auto retained=state::get_draft(data,draft_key,compiler.initial_code);
        check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input&&retained.indentation_json==draft.indentation_json
            &&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end,
            "Remainder narration changed the independent compiler draft");
        for(const auto& page:after.pages)for(const auto& block:page.blocks){
            if(block.stable_id!=revised.stable_id)check(test::compiler_reader_response_unchanged(data,after,block,prerequisites),
                "Remainder narration changed a disclosed prerequisite or fabricated another activity response");
            if(curriculum::has_compiler_objective(block))check(!windows::compiler_puzzle_completed(data,after,block)
                &&windows::compiler_puzzle_case_count(data,after,block)==0
                &&windows::compiler_puzzle_historical_case_count(data,after,block)==0,"Remainder narration invented a compiler execution");}
        const auto now=f.snapshot();for(const auto& recording:recordings){
            check(state::sha256(state::read_bytes(recording.part.recording))==recording.hash
                &&std::filesystem::last_write_time(recording.part.recording)==recording.stamp,"Remainder revision replaced or deleted an original WAV");
            const auto compatible=std::ranges::find_if(now.segments,[&](const auto& part){return part.region==recording.part.region
                &&part.source==recording.part.source&&part.spoken==recording.part.spoken;});
            if(compatible!=now.segments.end())check(compatible->recording==recording.part.recording,"Remainder revision regenerated compatible speech");}
        check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host),
            "Remainder QA altered immutable lesson bytes or exposed a desktop window");
    };
    const auto verify_passage=[&](std::size_t block,bool feedback,const std::string& text){
        const auto source=wide(text);std::vector<bool> covered(source.size());std::vector<std::size_t> parts;
        for(std::size_t i=0;i<mapped.segments.size();++i){const auto& part=mapped.segments[i];if(part.block!=block||part.feedback!=feedback)continue;
            check((!feedback||part.correct)&&part.start<part.end&&part.end<=source.size()
                &&part.source==utf8(source.substr(part.start,part.end-part.start)),"Published remainder speech lost exact UTF-16 source ownership");
            for(auto offset=part.start;offset<part.end;++offset)covered[offset]=true;
            (void)verify_feedback_recording(part);parts.push_back(i);}
        check(!parts.empty(),"Published remainder passage has no actual PCM segments");
        for(std::size_t i=0;i<source.size();++i)check(iswspace(source[i])||covered[i],"Published remainder speech omitted authored text");
        auto visible=text;std::erase(visible,'`');std::erase(visible,'\r');
        check(edit_text(child(f.view,1000+static_cast<int>(block)*100+(feedback?4:0)))==visible,
            "Published remainder passage differs from its complete native control");return parts;
    };
    auto expected_playback=verify_passage(at.block,true,"✓ Correcto\n\n"+revised.explanation);
    if(published30){const auto prose=verify_passage(1,false,new_prose.text);expected_playback.insert(expected_playback.end(),prose.begin(),prose.end());}
    preserve();
    // Checkpoint once before the second reopening: preparing an available WAV
    // may add its zero certificate, but cannot add listening or start playback.
    f.reader.reset();check(flush_lesson_view(f.view),"Cannot save the mapped remainder checkpoint");
    const auto durable=narration::read_history(read_lesson_state(f.view));reopen();const auto silent=f.snapshot();
    check(silent.key==mapped.key&&silent.index==mapped.index&&silent.offset==mapped.offset&&silent.heard_through==mapped.heard_through
        &&!silent.playing&&!silent.allocated&&narration::read_history(read_lesson_state(f.view))==durable,
        "A silent remainder reopen changed its mapped pause, history or playback ownership");preserve();
    if(phase==1){std::set<std::size_t> observed;f.reader->toggle();
        wait([&]{const auto value=f.snapshot();if(value.playing)observed.insert(value.index);
            check(!value.finished,"Remainder playback finished before reaching the actual following paragraph");
            return value.playing&&value.segments.at(value.index).block==3&&value.offset>100;
        },"complete revised remainder passages followed by the existing paragraph",120000);
        f.reader->pause();const auto played=f.snapshot();
        check(std::ranges::all_of(expected_playback,[&](auto index){return observed.contains(index)
            &&played.heard_through>static_cast<std::int64_t>(index)
            &&(!played.segments[index].feedback||played.segments[index].feedback_fully_played);}),
            "The revised remainder paragraph or explanation was skipped instead of being fully played");preserve();
    }
    std::cout<<"PASS U11L16 p3 "<<stage<<" reader phase "<<phase
        <<": actual rejected/repaired order, real historical PCM pause, same-slot silent migration and durable reopen; "
        <<(phase==0?"compatible heading retained":"first revised passage revisited and complete current feedback played")
        <<"; responses, attempts, independent draft and original WAVs retained; "<<prerequisites.ordinary_responses.size()
        <<" earlier ordinary answers are synthetic QA prerequisites. No XML import, compiler runs, full-lesson or human listening acceptance claimed.\n";
}
