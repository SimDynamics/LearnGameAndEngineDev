#pragma once
// Included after the private reader Fixture. The two answers below use actual
// lesson controls; every listening bookmark comes from real PCM playback.
void u11_assignment_editorial_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,unsigned phase){
    check(phase<2,"Invalid assignment reader phase");
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="dfbbac04b7a04df14f09c88b4b7b84ed59ab2e6b4b13fc1261d042d93b743fe0"
        &&state::sha256(after_bytes)=="768458f3b6b8d646dcd44fe154b5f264f31f6eb2c3851bb36c5442ebad700157",
        "Assignment reader fixtures differ from the reviewed complete Git documents");
    const auto original=windows::load_lesson(before_path),current=windows::load_lesson(after_path);
    const auto predicted_at=identity_position(original,"block_0004_0001"),choice_at=identity_position(original,"block_0004_0002");
    const auto draft_at=identity_position(original,"block_0005_0002");
    const auto& predicted=original.pages[predicted_at.page].blocks[predicted_at.block];
    const auto& choice=original.pages[choice_at.page].blocks[choice_at.block];
    const auto& new_prediction=current.pages[predicted_at.page].blocks[predicted_at.block];
    const auto& new_choice=current.pages[choice_at.page].blocks[choice_at.block];
    const auto following_at=identity_position(current,"block_0004_0003");
    const auto& following=current.pages[following_at.page].blocks[following_at.block];
    const auto& draft_owner=original.pages[draft_at.page].blocks[draft_at.block];
    check(predicted_at.page==4&&predicted_at.block==1&&choice_at.page==4&&choice_at.block==2
        &&choice.correct_index==1&&choice.options.size()==3&&predicted.answers.size()==1
        &&new_prediction.answers==predicted.answers&&new_choice.options==choice.options
        &&new_choice.correct_index==choice.correct_index&&predicted.explanation!=new_prediction.explanation
        &&choice.question!=new_choice.question&&choice.explanation!=new_choice.explanation
        &&following_at.page==4&&following_at.block==3&&following.type==curriculum::BlockType::multiple_choice,
        "The assignment reader no longer covers the two exact current page-five activities");
    const auto draft_key=curriculum::draft_key("programming",original,draft_owner);
    state::Draft draft;draft.source=draft_owner.initial_code+"\n// QA: borrador independiente\n";
    draft.input=" entrada\t\r\n";draft.indentation_json=state::make_indentation_metadata(draft.source,{});
    draft.selection_start=2;draft.selection_end=9;
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u11-assignment-reader"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(phase));
    Fixture f(backend,false,true,true,"es",original,true,4,true,true,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[&](state::StateData& data){
        state::put_draft(data,draft_key,draft);data.preferences["u11_assignment_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");
    },cache);
    struct FailureShutdown {std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}
    } failure_shutdown{backend};
    std::cout<<"PHASE assignment "<<phase<<" actual controls and original speech\n";
    check(!IsWindowVisible(f.host)&&lesson_page_state(f.view).index==4
        &&narration::read_history(read_lesson_state(f.view)).entries.empty(),
        "Assignment reader QA became visible, opened another page or seeded listening");
    f.answer(predicted.answers.front(),predicted_at.block);
    SendMessageW(child(f.view,1000+static_cast<int>(choice_at.block)*100+20+static_cast<int>(*choice.correct_index)*2),BM_CLICK,0,0);
    SendMessageW(child(f.view,1003+static_cast<int>(choice_at.block)*100),BM_CLICK,0,0);
    f.reader->pause();f.ready();
    const auto accepted=read_lesson_state(f.view);
    const auto prediction_response=state::get_response(accepted,original,predicted),choice_response=state::get_response(accepted,original,choice);
    check(prediction_response&&prediction_response->correct&&choice_response&&choice_response->correct,
        "Assignment reader prerequisites were not answered through their actual lesson controls");
    check(!windows::compiler_puzzle_completed(accepted,original,draft_owner),
        "The reader fixture fabricated an unrelated compiler achievement");
    struct Recording{LessonReaderSegmentSnapshot part;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Recording> recordings;
    for(const auto& part:f.snapshot().segments)if(part.available&&!part.recording.empty())
        recordings.push_back({part,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});
    f.reader->restart();f.reader->pause();f.reader->toggle();
    std::cout<<"PHASE assignment "<<phase<<" original partial playback\n";
    wait([&]{const auto value=f.snapshot();const auto& part=value.segments[value.index];
        return value.playing&&part.block==(phase==0?predicted_at.block:choice_at.block)
            &&part.feedback==(phase==1)&&value.offset>150&&value.offset+250<part.duration_ms;
    },phase==0?"real compatible assignment-question pause":"real old assignment-explanation pause");
    f.reader->pause();const auto previous=f.snapshot();
    check(previous.offset>0&&!previous.playing&&!previous.allocated&&!previous.finished,
        "Assignment narration has no released partial real-recording pause");
    (void)verify_feedback_recording(previous.segments[previous.index]);
    const auto protected_data=read_lesson_state(f.view);const auto old_history=narration::read_history(protected_data);
    const auto old_slot=std::ranges::find(old_history.entries,previous.key,&narration::Bookmark::key);
    check(old_history.entries.size()==1&&old_slot!=old_history.entries.end()&&old_slot->position.heard_prefix
        &&old_slot->position.heard_prefix->microseconds>0&&old_slot->position.milliseconds==previous.offset,
        "Assignment bookmark lacks genuine played PCM evidence");
    const auto reopen=[&]{
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot checkpoint assignment reader QA");DestroyWindow(f.view);f.view=nullptr;
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,current,std::move(hooks));
        check(f.view&&lesson_page_state(f.view).index==4,"Cannot reopen the revised assignment page");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
    };
    std::cout<<"PHASE assignment "<<phase<<" opening the reviewed text\n";
    reopen();const auto revised=f.snapshot();
    check(revised.key!=previous.key&&!revised.playing&&!revised.allocated&&!revised.finished,
        "The revised assignment reader reused its obsolete key or started playback");
    const auto first_new_feedback=std::ranges::find_if(revised.segments,[&](const auto& part){
        return part.block==predicted_at.block&&part.feedback&&part.correct;});
    check(first_new_feedback!=revised.segments.end(),"The current prediction explanation is missing from narration");
    const auto first_new_index=static_cast<std::size_t>(first_new_feedback-revised.segments.begin());
    std::cout<<"MIGRATE assignment phase="<<phase<<" previous(index/offset/heard)="<<previous.index<<'/'<<previous.offset<<'/'<<previous.heard_through
        <<" revised="<<revised.index<<'/'<<revised.offset<<'/'<<revised.heard_through
        <<" first_new_feedback="<<first_new_index
        <<" same_region="<<(revised.segments[revised.index].region==previous.segments[previous.index].region)
        <<" same_recording="<<(revised.segments[revised.index].recording==previous.segments[previous.index].recording)<<'\n';
    if(phase==0){
        const auto old_feedback=std::ranges::find_if(previous.segments,[](const auto& part){return part.feedback&&part.correct;});
        check(old_feedback!=previous.segments.end()
            &&previous.heard_through<=old_feedback-previous.segments.begin()
            &&revised.heard_through<=static_cast<std::int64_t>(first_new_index)
            &&revised.offset==previous.offset
            &&revised.segments[revised.index].region==previous.segments[previous.index].region
            &&revised.segments[revised.index].recording==previous.segments[previous.index].recording,
            "The unchanged question lost its compatible real pause or recording");
    }else check(revised.index==first_new_index&&revised.offset==0
            &&revised.heard_through<=static_cast<std::int64_t>(first_new_index),
        "An old later pause skipped the first changed explanation or inherited its listening time");
    const auto migrated=narration::read_history(read_lesson_state(f.view));
    check(migrated.entries.size()==1&&migrated.entries.front().key==revised.key
        &&migrated.entries.front().page==old_slot->page&&migrated.active_owner==old_history.active_owner
        &&migrated.active_key==revised.key,
        "Assignment revision did not relabel its existing page slot without another history");
    if(phase==0)check(migrated.entries.front().position.heard_prefix==old_slot->position.heard_prefix,
        "A compatible assignment pause lost its real recording certificate");
    else check(!migrated.entries.front().position.heard_prefix||migrated.entries.front().position.heard_prefix->microseconds==0,
        "The changed explanation inherited an old recording's positive listening certificate");
    const auto preserve=[&]{
        const auto data=read_lesson_state(f.view);
        for(const auto* old:{&predicted,&choice}){
            const auto stored=state::get_response(protected_data,original,*old),restored=state::get_response(data,current,*old);
            check(stored&&restored&&stored->raw_json==restored->raw_json&&stored->correct==restored->correct
                &&stored->answer==restored->answer&&stored->selected==restored->selected,
                "Assignment reader changed an actually accepted answer");
        }
        const auto retained=state::get_draft(data,draft_key,draft_owner.initial_code);
        check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input
            &&retained.indentation_json==draft.indentation_json&&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end
            &&data.preferences.at("u11_assignment_reader_qa")==protected_data.preferences.at("u11_assignment_reader_qa")
            &&!windows::compiler_puzzle_completed(data,current,draft_owner),
            "Assignment narration changed its independent draft/preferences or granted compiler credit");
        check(!state::get_response(data,current,following),"Assignment narration fabricated the following unanswered choice");
        const auto value=f.snapshot();
        for(const auto& recording:recordings){
            check(state::sha256(state::read_bytes(recording.part.recording))==recording.hash
                &&std::filesystem::last_write_time(recording.part.recording)==recording.stamp,
                "Assignment revision deleted or rewrote an existing WAV");
            const auto compatible=std::ranges::find_if(value.segments,[&](const auto& part){
                return part.region==recording.part.region&&part.source==recording.part.source&&part.spoken==recording.part.spoken;});
            if(compatible!=value.segments.end())check(compatible->recording==recording.part.recording,
                "Assignment revision regenerated compatible speech");
        }
        check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host),
            "Assignment QA altered fixture bytes or foreground desktop visibility");
    };
    std::vector<std::size_t> feedback;
    for(const auto& at:{predicted_at,choice_at}){
        const auto& block=current.pages[at.page].blocks[at.block];const auto text=wide("✓ Correcto\n\n"+block.explanation);
        std::vector<bool> covered(text.size());
        auto displayed=utf8(text);std::erase(displayed,'`');std::erase(displayed,'\r');
        check(edit_text(child(f.view,1004+static_cast<int>(at.block)*100))==displayed,
            "The revised success explanation is not fully displayed");
        for(std::size_t index=0;index<revised.segments.size();++index){const auto& part=revised.segments[index];
            if(part.block!=at.block||!part.feedback)continue;
            check(part.correct&&part.start<part.end&&part.end<=text.size()
                &&part.source==utf8(text.substr(part.start,part.end-part.start)),
                "The revised feedback differs from its complete displayed UTF-16 source");
            for(auto offset=part.start;offset<part.end;++offset)covered[offset]=true;
            (void)verify_feedback_recording(part);feedback.push_back(index);
        }
        for(std::size_t index=0;index<text.size();++index)
            check(text[index]==L' '||text[index]==L'\n'||text[index]==L'\r'||text[index]==L'\t'||covered[index],
                "The prepared assignment feedback omitted authored text");
    }
    check(!feedback.empty(),"The revised assignment has no feedback recordings");preserve();
    // Reopening the newly mapped checkpoint must be silent and exact. Compare
    // the durable close checkpoint, including a newly prepared zero certificate.
    f.reader.reset();check(flush_lesson_view(f.view),"Cannot save revised assignment QA");
    const auto durable=narration::read_history(read_lesson_state(f.view));
    DestroyWindow(f.view);f.view=nullptr;
    LessonViewHooks hooks;hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
    hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
    f.view=create_lesson_view(f.host,f.environment,current,std::move(hooks));check(f.view!=nullptr,"Cannot reopen mapped assignment checkpoint");
    MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
    const auto again=f.snapshot();
    check(again.key==revised.key&&again.index==revised.index&&again.offset==revised.offset&&again.heard_through==revised.heard_through
        &&!again.playing&&!again.allocated&&narration::read_history(read_lesson_state(f.view))==durable,
        "Reopening the mapped assignment checkpoint changed pause, history or playback ownership");
    preserve();
    if(phase==1){
        std::cout<<"PHASE assignment "<<phase<<" playing the complete reviewed explanations\n";
        std::set<std::size_t> observed;std::optional<std::size_t> last_part;f.reader->toggle();
        wait([&]{const auto value=f.snapshot();
            if(!last_part||*last_part!=value.index||value.waiting_gate){
                std::cout<<"PLAY assignment index="<<value.index<<" offset="<<value.offset<<" heard="<<value.heard_through
                    <<" block="<<value.segments[value.index].block<<" feedback="<<value.segments[value.index].feedback
                    <<" playing="<<value.playing<<" allocated="<<value.allocated<<" waiting="<<value.waiting_gate
                    <<" finished="<<value.finished<<" duration="<<value.segments[value.index].duration_ms<<'\n';
                last_part=value.index;
            }
            if(value.playing&&value.segments[value.index].feedback&&value.segments[value.index].correct)observed.insert(value.index);
            check(!value.finished,"Assignment narration falsely finished the page with a pending third exercise");
            if(value.waiting_gate&&!value.playing&&!value.allocated){
                check(value.segments[value.index].block==following_at.block,
                    "The revised assignment stopped before reaching its actual unanswered third exercise");return true;
            }
            return false;
        },"complete revised assignment explanations followed by the next unanswered choice",120000);
        check(std::ranges::all_of(feedback,[&](auto index){return observed.contains(index);})
            &&f.snapshot().heard_through>static_cast<std::int64_t>(feedback.back()),
            "Assignment page finished before the revised explanations actually played");
        preserve();
    }
    std::cout<<"PASS U11L20 p5 editorial reader phase "<<phase<<": real answered controls and PCM pause; "
        <<(phase==0?"unchanged question offset/certificate retained":"changed explanations revisited with zero inherited hearing and fully played")
        <<"; one bounded slot, silent durable reopen, original WAVs/draft/responses intact. No imports, educational compiler runs, full-lesson or human-listening acceptance claimed.\n";
}
