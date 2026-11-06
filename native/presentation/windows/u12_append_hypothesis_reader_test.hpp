#pragma once
// The complete authenticated documents are QA fixtures, not a second active
// curriculum. Answers and pauses below are earned through real controls/audio.
void u12_append_hypothesis_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,unsigned phase,bool current_stage=false) {
    check(phase<2,"Invalid U12 append-hypothesis reader phase");
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    const auto before_hash=current_stage?"e08054fbdeb93756cb45ae6515279742ee273f55c4ddf6a1b6962b447ed50a4e"
        :"81fb6029dc88ca86fd412f9084b758a953bc14d20322a98c6dbc7fbc6987d183";
    const auto after_hash=current_stage?"9fa23a593751256a5936eabfe1ae7dc8a2388a2dd80a146af6338557c625a55b"
        :"d5cafa688c73961ff3f4e60897d23d69f0df7826a9c192e723bca7a1e34c3dcc";
    const std::size_t editorial_prefix=current_stage?11:6;
    check(state::sha256(before_bytes)==before_hash&&state::sha256(after_bytes)==after_hash,
        "The U12 append-hypothesis reader fixtures differ from their authenticated complete documents");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    const auto at=identity_position(before,"block_0002_0001"),revised_at=identity_position(after,"block_0002_0001");
    const auto draft_at=identity_position(before,"block_0003_0003");
    const auto& old=before.pages.at(at.page).blocks.at(at.block);
    const auto& revised=after.pages.at(revised_at.page).blocks.at(revised_at.block);
    const auto& compiler=before.pages.at(draft_at.page).blocks.at(draft_at.block);
    check(at==revised_at&&at.page==2&&at.block==1&&old.id=="segunda_apertura_reemplaza"
        &&old.type==curriculum::BlockType::output_prediction&&old.id==revised.id&&old.code==revised.code
        &&old.answers==std::vector<std::string>{"Lectura incompleta"}&&old.answers==revised.answers
        &&old.explanation==revised.explanation&&old.review_feedback==revised.review_feedback
        &&old.question=="Supón que las operaciones de archivo se completan. Esta versión omite app en la segunda apertura. Predice el diagnóstico visible."
        &&revised.question=="Supón que las aperturas, escrituras y cierres tienen éxito. Esta versión omite std::ios::app en la segunda apertura. Predice el diagnóstico visible."
        &&draft_at.page==3&&draft_at.block==3&&compiler.type==curriculum::BlockType::cpp_compiler
        &&before.state_layout.reading_revisions.empty()&&after.state_layout.reading_revisions.size()==1
        &&after.state_layout.reading_revisions.front().editorial_prefix==editorial_prefix,
        "The reviewed U12 hypothesis, independent draft owner or closed reading provenance changed");
    if(current_stage){
        check(!before.compiler_puzzles_required&&!after.compiler_puzzles_required
            &&before.reference_after.size()==1&&after.reference_after.size()==1
            &&before.reference_after.front().activity=="block_0003_0003"
            &&after.reference_after.front().activity==before.reference_after.front().activity
            &&before.reference_after.front().blocks==std::vector<std::string>{"block_0003_0004","block_0004_0000","block_0004_0001","block_0004_0002"}
            &&after.reference_after.front().blocks==before.reference_after.front().blocks
            &&before.activity_after.size()==1&&after.activity_after.size()==1
            &&before.activity_after.front().activity=="block_0004_0003"
            &&after.activity_after.front().activity==before.activity_after.front().activity
            &&before.activity_after.front().prerequisites==std::vector<std::string>{"block_0003_0003"}
            &&after.activity_after.front().prerequisites==before.activity_after.front().prerequisites
            &&after.pages.at(draft_at.page).blocks.at(draft_at.block).initial_code==compiler.initial_code,
            "The current 11-to-12 hypothesis pair changed the existing append editor, derived gates or references");
    }
    const auto draft_key=curriculum::draft_key("programming",before,compiler);
    state::Draft draft;draft.source=compiler.initial_code+"\n// QA: conservar borrador independiente\n";draft.input=" entrada\t\r\n";
    draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=3;draft.selection_end=12;
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u12-append-hypothesis-reader"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())
            +(current_stage?"-current-":"-historical-")+std::to_string(phase));
    Fixture f(backend,false,true,true,"es",before,true,at.page,true,false,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[&](state::StateData& data){state::put_draft(data,draft_key,draft);
        data.preferences["u12_append_hypothesis_qa"]["opaque"]=std::string("\t conservar\r\n保存");},cache);
    struct FailureShutdown{std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}} shutdown{backend};
    const auto ready=[&]{wait([&]{const auto value=f.snapshot();return value.planned&&!value.segments.empty()
        &&value.index<value.segments.size()&&std::ranges::all_of(value.segments,[](const auto& part){return part.available;});
    },"complete focal U12 hypothesis synthesis and word alignment",120000);};
    const auto control=[&](int part){return child(f.view,1000+static_cast<int>(at.block)*100+part);};
    std::cout<<"PHASE U12 hypothesis "<<(current_stage?"current":"historical")<<' '<<phase
        <<" editorial="<<editorial_prefix<<"->"<<editorial_prefix+1<<" preparing authentic p3\n";ready();f.reader->pause();
    check(!IsWindowVisible(f.host)&&narration::read_history(read_lesson_state(f.view)).entries.empty(),
        "The fresh U12 hypothesis fixture inherited hearing or owns the desktop");
    for(const auto& page:before.pages)for(const auto& block:page.blocks)
        check(!state::get_response(read_lesson_state(f.view),before,block),"A U12 hypothesis answer was seeded");
    const auto answer=phase==0?std::string("Total=0"):std::string("Lectura incompleta");
    const auto entered=wide(answer);SendMessageW(control(2),EM_SETSEL,0,-1);
    SendMessageW(control(2),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(entered.c_str()));
    SendMessageW(control(3),BM_CLICK,0,0);f.reader->pause();ready();
    const auto protected_data=read_lesson_state(f.view);
    const auto response=state::get_response(protected_data,before,old);
    check(response&&response->answer==answer&&response->correct==(phase==1)
        &&(phase==1||state::response_reviewed(*response,old)),"The real U12 prediction did not retain its independent answer oracle");
    const auto unrelated=protected_data.preferences.at("u12_append_hypothesis_qa");
    struct Recording{std::filesystem::path path;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Recording> recordings;
    const auto retain=[&](const LessonReaderSnapshot& value){for(const auto& part:value.segments)if(part.available&&!part.recording.empty())
        recordings.push_back({part.recording,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});};
    retain(f.snapshot());
    const auto privacy=[&]{for(const auto& region:lesson_reading_regions(f.view))
        check(region.target!=control(1)&&region.target!=control(2),"The U12 hypothesis reader exposed code bodies or typed answers");};
    privacy();
    const auto original_plan=f.snapshot();
    const auto question=std::ranges::find_if(original_plan.segments,[&](const auto& part){return part.block==at.block&&!part.feedback&&part.target==control(0);});
    const auto feedback=std::ranges::find_if(original_plan.segments,[&](const auto& part){return part.block==at.block&&part.feedback;});
    check(question!=original_plan.segments.end()&&feedback!=original_plan.segments.end(),"The original U12 question or submitted feedback is missing");
    const auto wanted=static_cast<std::size_t>((phase==0?question:feedback)-original_plan.segments.begin());
    f.reader->restart();f.reader->pause();
    double preceding=0;for(std::size_t index=0;index<wanted;++index)preceding+=original_plan.segments[index].duration_ms;
    // Preview is positioning only, including the unchanged successful-feedback
    // sentence in phase 1. Only the following Play earns the actual old pause.
    f.reader->seek_seconds(static_cast<int>(std::floor(preceding/1000.)));
    f.reader->toggle();wait([&]{const auto value=f.snapshot();check(value.index<=wanted,"Original playback skipped the selected authentic U12 pause");
        return value.playing&&value.index==wanted&&value.offset>150&&value.offset+150<value.segments[value.index].duration_ms;
    },phase==0?"real old U12 question pause":"real unchanged later U12 success-feedback pause");
    f.reader->pause();const auto paused=f.snapshot();
    (void)verify_feedback_recording(paused.segments[paused.index]);
    const auto history=narration::read_history(read_lesson_state(f.view));
    const auto old_slot=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
    check(history.entries.size()==1&&old_slot!=history.entries.end()&&old_slot->position.milliseconds==paused.offset
        &&old_slot->position.heard_prefix&&old_slot->position.heard_prefix->microseconds>0,
        "The old U12 pause lacks its genuine source-bound playback certificate");
    const auto preserve=[&]{const auto data=read_lesson_state(f.view);const auto restored=state::get_response(data,after,revised);
        check(restored&&restored->raw_json==response->raw_json&&restored->answer==response->answer&&restored->correct==response->correct
            &&restored->reviewed_answer_identity==response->reviewed_answer_identity&&edit_text(control(2))==answer,
            "U12 question migration changed its actual answer, submitted review or visible draft");
        const auto retained=state::get_draft(data,draft_key,compiler.initial_code);
        check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input
            &&retained.indentation_json==draft.indentation_json&&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end
            &&data.preferences.at("u12_append_hypothesis_qa")==unrelated,"U12 question migration changed an independent compiler draft or unrelated preference");
        for(const auto& page:after.pages)for(const auto& block:page.blocks){
            if(block.stable_id!=revised.stable_id)check(!state::get_response(data,after,block),"U12 migration fabricated an independent answer");
            if(curriculum::has_compiler_objective(block))check(windows::compiler_puzzle_case_count(data,after,block)==0,"U12 reader migration fabricated compiler execution credit");}
        for(const auto& recording:recordings)check(state::sha256(state::read_bytes(recording.path))==recording.hash
            &&std::filesystem::last_write_time(recording.path)==recording.stamp,"U12 hypothesis migration changed an existing real recording");
        check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host),
            "U12 hypothesis migration changed immutable source documents or desktop visibility");privacy();};
    const auto reopen=[&]{f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush isolated U12 hypothesis state");
        const auto durable=narration::read_history(read_lesson_state(f.view));DestroyWindow(f.view);f.view=nullptr;
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,after,std::move(hooks));check(f.view!=nullptr,"Cannot reopen the complete revised U12 lesson");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});ready();return durable;};
    reopen();const auto mapped=f.snapshot();
    const auto regions=lesson_reading_regions(f.view);
    const auto region=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==at.block&&!item.feedback&&item.target==control(0);});
    check(region!=regions.end()&&region->source_text==revised.question&&edit_text(control(0))==revised.question,
        "The revised U12 hypothesis is not the complete displayed and spoken source");
    const auto plan=backend->plan(revised.question,"es",{},region->reading_context,region->math_mode);
    std::vector<std::size_t> question_parts;const auto source=wide(revised.question);std::vector<bool> covered(source.size());
    for(std::size_t index=0;index<mapped.segments.size();++index){const auto& part=mapped.segments[index];if(part.region!=region->key)continue;
        const auto number=question_parts.size();check(number<plan.size()&&!part.feedback&&part.target==control(0)
            &&part.source==plan[number].text&&part.spoken==plan[number].spoken&&part.start==plan[number].start&&part.end==plan[number].end
            &&part.start<part.end&&part.end<=source.size()&&part.source==utf8(source.substr(part.start,part.end-part.start)),
            "The corrected U12 question lost its full contextual plan or UTF-16 source ownership");
        for(auto offset=part.start;offset<part.end;++offset)covered[offset]=true;
        std::cout<<"AUDIO U12 hypothesis phase="<<phase<<" part="<<number+1<<" sha256="<<verify_feedback_recording(part)
            <<" path="<<part.recording.generic_string()<<'\n';question_parts.push_back(index);}
    for(std::size_t offset=0;offset<source.size();++offset)check(iswspace(source[offset])||covered[offset],"The corrected U12 question plan truncated source text");
    check(!question_parts.empty()&&question_parts.size()==plan.size()&&mapped.key!=paused.key&&mapped.index==question_parts.front()
        &&mapped.offset==0&&!mapped.finished&&!mapped.playing&&!mapped.allocated&&mapped.heard_through<=static_cast<std::int64_t>(mapped.index),
        "The changed U12 hypothesis retained obsolete audio position/credit or autostarted");
    const auto migrated=narration::read_history(read_lesson_state(f.view));
    const auto slot=std::ranges::find(migrated.entries,mapped.key,&narration::Bookmark::key);
    check(migrated.entries.size()==1&&slot!=migrated.entries.end()&&slot->page==old_slot->page
        &&migrated.active_key==mapped.key&&migrated.active_owner==history.active_owner
        &&(!slot->position.heard_prefix||slot->position.heard_prefix->microseconds==0)
        &&std::ranges::none_of(migrated.entries,[&](const auto& entry){return entry.key==paused.key;}),
        "U12 question migration duplicated its bookmark, changed ownership or borrowed an old audio certificate");
    retain(mapped);preserve();
    const auto durable=reopen();const auto silent=f.snapshot();
    check(silent.key==mapped.key&&silent.index==mapped.index&&silent.offset==mapped.offset&&silent.heard_through==mapped.heard_through
        &&!silent.finished&&!silent.playing&&!silent.allocated&&narration::read_history(read_lesson_state(f.view))==durable,
        "A second revised U12 reopening changed its exact silent checkpoint");
    const auto until=GetTickCount64()+200;wait([&]{const auto value=f.snapshot();check(!value.playing&&!value.allocated&&value.offset==0,
        "A corrected U12 question started without explicit Play");return GetTickCount64()>=until;},"the migrated U12 question remains silent",2000);preserve();
    std::set<std::size_t> heard;f.reader->toggle();
    wait([&]{const auto value=f.snapshot();if(value.playing&&std::ranges::find(question_parts,value.index)!=question_parts.end())heard.insert(value.index);
        return !value.playing&&!value.allocated&&!value.preparing&&(phase==0?value.segments[value.index].feedback&&!value.segments[value.index].correct
            &&value.segments[value.index].block==at.block&&value.offset+1>=value.segments[value.index].duration_ms
            :value.waiting_gate&&value.segments[value.index].block==2);
    },phase==0?"complete corrected U12 question and original rejected-feedback retry stop":"complete corrected U12 question and actual following pending puzzle",120000);
    check(std::ranges::all_of(question_parts,[&](auto index){return heard.contains(index);})&&!f.snapshot().finished,
        "Explicit Play skipped part of the corrected U12 question or completed an unanswered page");preserve();
    std::cout<<"PASS "<<(current_stage?"current":"historical")<<" U12 append hypothesis "<<editorial_prefix<<"->"<<editorial_prefix+1
        <<" phase "<<phase<<": actual "<<(phase==0?"incorrect":"correct")
        <<" prediction retained; real old "<<(phase==0?"question":"unchanged later feedback")
        <<" pause maps to complete corrected question at zero without audio credit/autoplay; exact second reopening, full new contextual PCM/timing/UTF-16 plan and explicit playback, independent draft and original WAV/source preserved. No compiler execution, whole-lesson traversal or human listening acceptance claimed.\n";
}
