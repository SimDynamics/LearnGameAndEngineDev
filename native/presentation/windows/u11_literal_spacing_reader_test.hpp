#pragma once
// Included after the existing reader Fixture. Historical pauses and all three
// bookmarks are earned from real playback, never injected listening credit.
void u11_literal_spacing_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,unsigned phase){
    check(phase<2,"Invalid literal-spacing reader phase");
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    const auto before_hash=state::sha256(before_bytes),after_hash=state::sha256(after_bytes);
    const bool historical=before_hash=="35c2ec10d50202f99fd957d77410f2fa987575f6b97c995d5354c1ed8be53d3f"
        &&after_hash=="a44d7467cdd2f4aa0aa12b6f428ba6b595b20a9019c8d5976ea6c7be69c96f49";
    const bool current=before_hash=="9012b0d2a268a5df7fde68a74922748262c7b1d08e21d6d21225743db76c2933"
        &&after_hash=="9e248574ca08044e9d533f58cba482f033b7ea0e9fc091b9939bcce2c824b8ba";
    check(historical||current,"Literal-spacing reader fixtures differ from either authenticated complete document pair");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    const auto at=identity_position(before,"block_0001_0004"),current_at=identity_position(after,"block_0001_0004");
    const auto& old=before.pages[at.page].blocks[at.block];const auto& revised=after.pages[current_at.page].blocks[current_at.block];
    const auto compiler_at=identity_position(before,"block_0003_0001"),revised_compiler_at=identity_position(after,"block_0003_0001");
    const auto& compiler=before.pages[compiler_at.page].blocks[compiler_at.block];
    check(at==current_at&&at.page==1&&at.block==4&&old.type==curriculum::BlockType::multiple_choice
        &&old.id=="cpp_fundamentals__texto_interpretacion_y_flujos__construir_una_sesion_con_nombres_y_estado__e01"
        &&old.id==revised.id&&old.correct_index==1&&old.options==revised.options&&old.correct_index==revised.correct_index
        &&old.question==revised.question&&old.review_feedback_options==revised.review_feedback_options
        &&old.explanation=="La creación completa guardó cero. El literal100 se rechaza y su candidato no sustituye ese estado."
        &&revised.explanation=="La creación completa guardó cero. El literal 100 se rechaza y su candidato no sustituye ese estado."
        &&compiler.type==curriculum::BlockType::cpp_compiler&&compiler_at==revised_compiler_at
        &&compiler_at.page==3&&compiler_at.block==(current?4u:1u)
        &&compiler.initial_code==after.pages[revised_compiler_at.page].blocks[revised_compiler_at.block].initial_code
        &&curriculum::reference_objective_frame(before,compiler)==curriculum::reference_objective_frame(after,
            after.pages[revised_compiler_at.page].blocks[revised_compiler_at.block]),
        "The focal literal-spacing identity, source pair or independent draft owner changed");
    const auto draft_key=curriculum::draft_key("programming",before,compiler);
    state::Draft draft;draft.source=compiler.initial_code+"\n// QA: conservar borrador independiente\n";draft.input=" entrada\t\r\n";
    draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=3;draft.selection_end=11;
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u11-literal-spacing-reader"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(phase));
    std::cout<<"PHASE session "<<phase<<" opening initial bookmark page 3 without whole-page preparation\n";
    Fixture f(backend,false,true,true,"es",before,true,2,true,false,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[&](state::StateData& data){state::put_draft(data,draft_key,draft);
        data.preferences["u11_literal_spacing_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");},cache);
    struct FailureShutdown{std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}} failure_shutdown{backend};
    const auto ready_session=[&](bool all){
        std::cout<<"PHASE session "<<phase<<" preparing p"<<lesson_page_state(f.view).index+1
            <<(all?" complete plan":" current bookmark segment")<<'\n';
        wait([&]{const auto value=f.snapshot();return value.planned&&!value.segments.empty()&&value.index<value.segments.size()
            &&(all?std::ranges::all_of(value.segments,[](const auto& part){return part.available;}):value.segments[value.index].available);
        },all?"complete focal session synthesis/word alignment":"current other-page session bookmark recording",120000);
    };
    const auto navigate_session=[&](std::size_t page,bool all){
        std::cout<<"PHASE session "<<phase<<" changing to p"<<page+1<<'\n';
        f.reader->pause();check(navigate_lesson_view(f.view,page),"Cannot navigate the isolated literal-spacing session page");
        f.reader->content_changed();ready_session(all);
    };
    check(!IsWindowVisible(f.host)&&narration::read_history(read_lesson_state(f.view)).entries.empty(),
        "Literal-spacing reader started with seeded hearing or visible desktop ownership");
    struct Recording{LessonReaderSegmentSnapshot part;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Recording> recordings;
    const auto retain=[&](const LessonReaderSnapshot& value){for(const auto& part:value.segments)if(part.available&&!part.recording.empty())
        recordings.push_back({part,verify_feedback_recording(part),std::filesystem::last_write_time(part.recording)});};
    std::vector<narration::Bookmark> other_slots;
    for(const std::size_t page:{2u,0u}){
        if(lesson_page_state(f.view).index!=page)navigate_session(page,false);else ready_session(false);
        f.reader->toggle();
        wait([&]{const auto value=f.snapshot();return value.playing&&value.offset>100;},"another literal-spacing page really plays");
        f.reader->pause();const auto paused=f.snapshot();retain(paused);const auto history=narration::read_history(read_lesson_state(f.view));
        const auto slot=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
        check(slot!=history.entries.end()&&slot->position.milliseconds==paused.offset&&slot->position.heard_prefix
            &&slot->position.heard_prefix->microseconds>0,"Another session page lacks its actual partial-playback certificate");
        other_slots.push_back(*slot);
    }
    navigate_session(at.page,true);SendMessageW(child(f.view,1000+static_cast<int>(at.block)*100+22),BM_CLICK,0,0);
    SendMessageW(child(f.view,1003+static_cast<int>(at.block)*100),BM_CLICK,0,0);f.reader->pause();ready_session(true);
    const auto protected_data=read_lesson_state(f.view);const auto response=state::get_response(protected_data,before,old);
    check(response&&response->correct&&response->selected==1,"The actual session choice did not accept its independent second-option oracle");
    f.reader->restart();f.reader->pause();const auto planned=f.snapshot();
    const auto changed=std::ranges::find_if(planned.segments,[&](const auto& part){return part.block==at.block&&part.feedback
        &&part.correct&&part.source.find("literal100")!=std::string::npos;});
    const auto prompt=std::ranges::find_if(planned.segments,[&](const auto& part){return part.block==at.block&&!part.feedback;});
    check(changed!=planned.segments.end()&&prompt!=planned.segments.end(),"The original actual prompt or protected explanation is missing");
    const auto wanted=static_cast<std::size_t>((phase==0?prompt:changed)-planned.segments.begin());double preceding=0;
    for(std::size_t index=0;index<wanted;++index)preceding+=planned.segments[index].duration_ms;
    // Preview positioning earns no hearing. Only the following real playback
    // supplies the positive certificate inspected below.
    f.reader->seek_seconds(static_cast<int>(std::floor(preceding/1000.)));f.reader->toggle();
    wait([&]{const auto value=f.snapshot();check(value.index<=wanted,"Playback skipped the selected authentic session pause");
        return value.playing&&value.index==wanted&&value.offset>150&&value.offset+150<value.segments[value.index].duration_ms;
    },"the actual old session question/explanation reaches a partial pause");
    f.reader->pause();const auto previous=f.snapshot();retain(previous);
    const auto old_history=narration::read_history(read_lesson_state(f.view));
    const auto old_slot=std::ranges::find(old_history.entries,previous.key,&narration::Bookmark::key);
    check(old_history.entries.size()==3&&old_slot!=old_history.entries.end()&&old_slot->position.heard_prefix
        &&old_slot->position.heard_prefix->microseconds>0&&old_slot->position.milliseconds==previous.offset,
        "The session pause was not earned from the recording actually played");
    const auto reopen=[&]{
        std::cout<<"PHASE session "<<phase<<" reopening revised p"<<at.page+1<<'\n';
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot checkpoint literal-spacing QA");
        const auto durable=narration::read_history(read_lesson_state(f.view));DestroyWindow(f.view);f.view=nullptr;
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,after,std::move(hooks));check(f.view&&lesson_page_state(f.view).index==at.page,"Cannot reopen the revised session page");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});ready_session(true);return durable;
    };
    reopen();const auto mapped=f.snapshot();
    const auto first=std::ranges::find_if(mapped.segments,[&](const auto& part){return part.block==at.block&&part.feedback&&part.correct;});
    const auto spaced=std::ranges::find_if(mapped.segments,[&](const auto& part){return part.block==at.block&&part.feedback
        &&part.correct&&part.source.find("literal 100")!=std::string::npos;});
    check(first!=mapped.segments.end()&&spaced!=mapped.segments.end()&&mapped.key!=previous.key&&!mapped.playing&&!mapped.allocated&&!mapped.finished,
        "The revised session lost its corrected source, reused an obsolete page key or autostarted");
    check(std::ranges::none_of(mapped.segments,[](const auto& part){return part.source.find("literal100")!=std::string::npos;}),
        "The revised session still plans its obsolete joined source token");
    const auto first_index=static_cast<std::size_t>(first-mapped.segments.begin());
    if(phase==0)check(mapped.offset==previous.offset&&mapped.segments[mapped.index].region==previous.segments[previous.index].region
        &&mapped.segments[mapped.index].recording==previous.segments[previous.index].recording,
        "The compatible session-question pause lost its recording or offset");
    else check(mapped.index==first_index&&mapped.offset==0&&mapped.heard_through<=static_cast<std::int64_t>(first_index),
        "An incompatible session pause did not revisit its corrected passage at zero");
    const auto source=wide("✓ Correcto\n\n"+revised.explanation);std::vector<bool> covered(source.size());std::vector<std::size_t> feedback;
    check(edit_text(child(f.view,1004+static_cast<int>(at.block)*100))==utf8(source),"The revised explanation is not completely visible");
    for(std::size_t index=0;index<mapped.segments.size();++index){const auto& part=mapped.segments[index];
        if(part.block!=at.block||!part.feedback)continue;
        check(part.correct&&part.start<part.end&&part.end<=source.size()&&part.source==utf8(source.substr(part.start,part.end-part.start)),
            "The revised session feedback differs from its original complete UTF-16 source");
        for(auto offset=part.start;offset<part.end;++offset)covered[offset]=true;(void)verify_feedback_recording(part);feedback.push_back(index);
    }
    for(std::size_t offset=0;offset<source.size();++offset)check(iswspace(source[offset])||covered[offset],"The revised session plan truncated authored feedback");
    const auto corrected=wide(spaced->source);
    const auto word=corrected.find(L"literal"),number=corrected.find(L"100",word);
    check(word!=corrected.npos&&number==word+8,"The prepared original source did not separate literal and 100 with one space");
    bool word_timing=false,number_timing=false;
    for(const auto& timing:spaced->timings){
        if(timing.start<=word&&timing.end>=word+7&&timing.end<=number)word_timing=true;
        if(timing.start>=number&&timing.start<=number+2&&timing.end>=number+3)number_timing=true;
        check(!(timing.start<=word&&timing.end>=number+3),"One word timing still fuses the source word literal with 100");
    }
    check(word_timing&&number_timing,"The revised source lacks distinct word/numeral timing ownership");
    std::cout<<"MIGRATE session phase="<<phase<<" old/new index="<<previous.index<<'/'<<mapped.index
        <<" same_prepared_speech="<<(changed->spoken==spaced->spoken)<<" same_wav="<<(changed->recording==spaced->recording)<<'\n';
    auto expected_active_owner=old_history.active_owner;
    const auto preserve=[&]{
        const auto data=read_lesson_state(f.view);const auto restored=state::get_response(data,after,revised);
        check(restored&&restored->raw_json==response->raw_json&&restored->answer==response->answer&&restored->correct
            &&restored->selected==response->selected&&restored->reviewed_answer_identity==response->reviewed_answer_identity,
            "Session narration migration changed its actual accepted answer/review");
        const auto retained=state::get_draft(data,draft_key,compiler.initial_code);
        check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input&&retained.indentation_json==draft.indentation_json
            &&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end
            &&data.preferences.at("u11_literal_spacing_reader_qa")==protected_data.preferences.at("u11_literal_spacing_reader_qa"),
            "Session narration migration changed the independent compiler draft or unrelated preferences");
        for(const auto& page:after.pages)for(const auto& block:page.blocks){
            if(curriculum::has_compiler_objective(block))check(windows::compiler_puzzle_case_count(data,after,block)==0,"Reader QA fabricated compiler execution credit");
            if(block.stable_id!=revised.stable_id)check(!state::get_response(data,after,block),"Reader QA fabricated another curricular response");
        }
        const auto history=narration::read_history(data);const auto slot=std::ranges::find(history.entries,mapped.key,&narration::Bookmark::key);
        check(history.entries.size()==3&&slot!=history.entries.end()&&slot-history.entries.begin()==old_slot-old_history.entries.begin()
            &&slot->page==old_slot->page
            &&std::ranges::none_of(history.entries,[&](const auto& item){return item.key==previous.key;}),
            "Session migration did not relabel its existing bounded page slot in place");
        const auto now=f.snapshot();
        const bool owner_matches=now.finished?history.active_key.empty()&&history.active_owner.empty()&&slot->position.finished
            :history.active_key==mapped.key&&history.active_owner==expected_active_owner&&!slot->position.finished;
        if(!owner_matches)std::cout<<"OWNER session phase="<<phase<<" finished="<<now.finished
            <<" expected_owner="<<expected_active_owner<<" actual_owner="<<history.active_owner
            <<" active_key="<<history.active_key<<" expected_key="<<mapped.key<<" slot_finished="<<slot->position.finished<<'\n';
        check(owner_matches,"Session narration did not retain its current playback owner or release finished ownership");
        for(const auto& other:other_slots){const auto found=std::ranges::find(history.entries,other.key,&narration::Bookmark::key);
            check(found!=history.entries.end()&&*found==other,"Session migration changed another genuinely played page bookmark");}
        for(const auto& recording:recordings){check(state::sha256(state::read_bytes(recording.part.recording))==recording.hash
            &&std::filesystem::last_write_time(recording.part.recording)==recording.stamp,"Session migration rewrote or removed an existing real WAV");
            const auto compatible=std::ranges::find_if(now.segments,[&](const auto& part){return part.region==recording.part.region
                &&part.source==recording.part.source&&part.spoken==recording.part.spoken;});
            if(compatible!=now.segments.end())check(compatible->recording==recording.part.recording,"Session migration regenerated compatible speech");}
        check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host),
            "Session reader QA changed immutable fixture bytes or desktop visibility");
    };
    preserve();const auto migrated=narration::read_history(read_lesson_state(f.view));
    const auto migrated_slot=std::ranges::find(migrated.entries,mapped.key,&narration::Bookmark::key);
    if(phase==0)check(migrated_slot->position.heard_prefix==old_slot->position.heard_prefix,"Compatible session speech lost its actual certificate");
    else check(!migrated_slot->position.heard_prefix||migrated_slot->position.heard_prefix->microseconds==0,
        "The corrected session passage inherited positive hearing from its previous recording");
    const auto durable=reopen();const auto silent=f.snapshot();
    check(silent.key==mapped.key&&silent.index==mapped.index&&silent.offset==mapped.offset&&silent.heard_through==mapped.heard_through
        &&!silent.playing&&!silent.allocated&&narration::read_history(read_lesson_state(f.view))==durable,
        "A second session reopening changed the exact silent mapped checkpoint");preserve();
    if(phase==1){
        std::set<std::size_t> observed;f.reader->toggle();await_resumed_player([&]{return f.snapshot();},silent,"Explicit revised session playback");
        wait([&]{const auto value=f.snapshot();check(value.index==first_index,"Explicit playback skipped the revised session passage");
            return value.playing&&value.offset>100&&value.offset+100<value.segments[value.index].duration_ms;
        },"the revised session earns its own partial playback");
        f.reader->pause();const auto earned=f.snapshot();observed.insert(earned.index);
        const auto earned_history=narration::read_history(read_lesson_state(f.view));
        const auto earned_slot=std::ranges::find(earned_history.entries,earned.key,&narration::Bookmark::key);
        check(earned_history.active_key==mapped.key&&!earned_history.active_owner.empty()&&earned_history.active_owner!=expected_active_owner,
            "Explicit revised playback failed to acquire its own new active owner");
        expected_active_owner=earned_history.active_owner;
        check(earned_slot!=earned_history.entries.end()&&earned_slot->position.heard_prefix
            &&earned_slot->position.heard_prefix->microseconds>0&&earned_slot->position.milliseconds==earned.offset,
            "Explicit revised playback failed to earn its own source-bound certificate");
        const auto earned_durable=reopen();const auto resumed=f.snapshot();
        check(resumed.key==earned.key&&resumed.index==earned.index&&resumed.offset==earned.offset&&resumed.heard_through==earned.heard_through
            &&!resumed.playing&&!resumed.allocated&&narration::read_history(read_lesson_state(f.view))==earned_durable,
            "The newly earned session pause did not reopen silently and exactly");preserve();
        f.reader->toggle();await_resumed_player([&]{return f.snapshot();},resumed,"The newly earned session pause resumes");
        wait([&]{const auto value=f.snapshot();if(value.playing&&value.segments[value.index].feedback&&value.segments[value.index].correct)observed.insert(value.index);
            return value.finished&&!value.playing&&!value.allocated;
        },"the complete corrected session explanation actually plays");
        check(std::ranges::all_of(feedback,[&](auto index){return observed.contains(index);})
            &&f.snapshot().heard_through>static_cast<std::int64_t>(feedback.back()),"Session continuation skipped part of the corrected explanation");preserve();
    }
    std::cout<<"PASS "<<(current?"current":"historical")<<" U11L22 literal-spacing reader phase "<<phase<<": real choice and three earned page bookmarks; "
        <<(phase==0?"compatible question pause/certificate retained":"incompatible explanation revisited at zero, full corrected feedback played")
        <<"; distinct source word/numeral timings, non-silent PCM, silent reopen, unchanged draft/answers/other slots/WAVs. Equal prepared speech may legitimately reuse PCM. No program execution, import, full-page traversal or human listening claimed.\n";
}
