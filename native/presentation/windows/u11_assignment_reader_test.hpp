#pragma once
// Included after the private reader Fixture. The two answers below use actual
// lesson controls; every positive listening certificate comes from real PCM playback.
void u11_assignment_outer_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
    const std::filesystem::path& before_path,const std::filesystem::path& after_path,
    const curriculum::Lesson& original,const curriculum::Lesson& current,unsigned phase);
void u11_assignment_editorial_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,unsigned phase,bool published=false){
    check(phase<2||(published&&phase<6),"Invalid assignment reader phase");
    const auto publication=published?"published21-to25":"candidate20-to24";
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)==(published?"9d884b47683e4e2f3891f572dab8456dcf7e8d6af65bbd33b7c8707f46ea6db8":
            "dfbbac04b7a04df14f09c88b4b7b84ed59ab2e6b4b13fc1261d042d93b743fe0")
        &&state::sha256(after_bytes)==(published?"69eef7a0633bafaea663bc5b3bfa23da2a8940cf25ecbbd8af60090587204cf9":
            "768458f3b6b8d646dcd44fe154b5f264f31f6eb2c3851bb36c5442ebad700157"),
        "Assignment reader fixtures differ from the reviewed complete Git documents");
    const auto original=windows::load_lesson(before_path),current=windows::load_lesson(after_path);
    check(original.id=="cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada"
        &&current.id==original.id&&original.locale=="es"&&current.locale=="es"
        &&original.state_layout.version==2&&current.state_layout.version==2
        &&original.state_layout.lineage==current.state_layout.lineage
        &&original.state_layout.signatures==current.state_layout.signatures,
        "Assignment reading changed its Spanish family or response contracts");
    using winrt::Windows::Data::Json::JsonObject;
    const auto old_edges=JsonObject::Parse(winrt::to_hstring(original.state_layout.raw_json)).GetNamedArray(L"editorial");
    const auto new_edges=JsonObject::Parse(winrt::to_hstring(current.state_layout.raw_json)).GetNamedArray(L"editorial");
    check(old_edges.Size()==(published?21u:20u)&&new_edges.Size()==old_edges.Size()+4,
        "Assignment reading selected a different editorial transition");
    for(unsigned i=0;i<old_edges.Size();++i)check(old_edges.GetAt(i).Stringify()==new_edges.GetAt(i).Stringify(),
        "Assignment reading rewrote the original editorial prefix");
    const auto archive=std::ranges::find(current.state_layout.reading_revisions,
        static_cast<std::size_t>(old_edges.Size()),&curriculum::StateReadingRevision::editorial_prefix);
    check(archive!=current.state_layout.reading_revisions.end()&&archive->previous_blocks.size()==4,
        "Assignment reading cannot authenticate all four owners at its selected source publication");
    for(const auto identity:{"block_0002_0003","block_0004_0001","block_0004_0002","block_0007_0001"}){
        const auto at=identity_position(original,identity);const auto previous=archive->previous_blocks.find(identity);
        check(previous!=archive->previous_blocks.end()
            &&test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(previous->second)))
                ==test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(original.pages[at.page].blocks[at.block].raw_json))),
            "Assignment reading archive differs from the complete original activity");
    }
    if(phase>=2){
        u11_assignment_outer_reader(backend,before_path,after_path,original,current,phase);return;
    }
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
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u11-assignment-reader"/publication/
        ("pcm-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(phase));
    test::CompilerReaderEntrySetup entry_setup;
    Fixture f(backend,false,true,true,"es",original,true,4,true,true,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[&](state::StateData& data){
        entry_setup=test::prepare_compiler_reader_entry(data,original,4);
        state::put_draft(data,draft_key,draft);data.preferences["u11_assignment_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");
    },cache);
    struct FailureShutdown {std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}
    } failure_shutdown{backend};
    std::cout<<"PHASE assignment "<<publication<<' '<<phase<<" actual controls and original speech\n";
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
        for(const auto& page:current.pages)for(const auto& block:page.blocks)
            if(block.stable_id!=predicted.stable_id&&block.stable_id!=choice.stable_id)
                check(test::compiler_reader_response_unchanged(data,current,block,entry_setup),
                    "Assignment narration changed a disclosed synthetic prerequisite or invented another response");
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
    std::cout<<"PASS U11L20 p5 "<<publication<<" editorial reader phase "<<phase<<": real answered controls and PCM pause; "
        <<(phase==0?"unchanged question offset/certificate retained":"changed explanations revisited with zero inherited hearing and fully played")
        <<"; one bounded slot, silent durable reopen, original WAVs/draft/responses intact; "<<entry_setup.ordinary_responses.size()
        <<" disclosed synthetic ordinary prerequisites. No imports, educational compiler runs, full-lesson or human-listening acceptance claimed.\n";
}

// Published phases 2/3 exercise p3 from its old question/feedback; phases 4/5
// exercise p8 from its unchanged question/changed feedback. The caller has
// authenticated the complete 21-to-25 pair and its four original owners.
void u11_assignment_outer_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,
        const curriculum::Lesson& original,const curriculum::Lesson& current,unsigned phase){
    check(phase>=2&&phase<6,"Invalid outer assignment reader phase");
    const bool order_page=phase<4,old_feedback=phase%2==1,compatible=!order_page&&!old_feedback;
    const auto at=identity_position(original,order_page?"block_0002_0003":"block_0007_0001");
    const auto& old=original.pages[at.page].blocks[at.block];
    const auto current_at=identity_position(current,old.stable_id);
    const auto& revised=current.pages[current_at.page].blocks[current_at.block];
    check(at.page==(order_page?2u:7u)&&at.block==(order_page?3u:1u)
        &&current_at.page==at.page&&current_at.block==at.block
        &&old.type==(order_page?curriculum::BlockType::order_code:curriculum::BlockType::output_prediction)
        &&old.type==revised.type&&old.id==revised.id&&old.explanation!=revised.explanation
        &&(order_page?old.question!=revised.question:old.question==revised.question)
        &&old.fragments==revised.fragments&&old.correct_order==revised.correct_order
        &&old.answers==revised.answers&&old.code==revised.code,
        "The published outer assignment owner changed its location or answer contract");
    check(order_page?old.correct_order==std::vector<std::size_t>{2,1,3,0}:old.answers.size()==1,
        "The published outer assignment answer oracle changed");
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    const auto draft_at=identity_position(original,"block_0005_0002");
    const auto& compiler=original.pages[draft_at.page].blocks[draft_at.block];
    const auto draft_key=curriculum::draft_key("programming",original,compiler);
    state::Draft draft;draft.source=compiler.initial_code+"\n// QA: borrador independiente de la lectura\n";
    draft.input=order_page?" entrada\t\r\n":"";draft.indentation_json=state::make_indentation_metadata(draft.source,{});
    draft.selection_start=3;draft.selection_end=11;
    test::CompilerReaderEntrySetup entry_setup;
    const auto entry_page=order_page?at.page:draft_at.page;
    std::map<std::filesystem::path,std::string> compiler_files;
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u11-assignment-reader/published21-to25"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(phase));
    Fixture f(backend,false,true,true,"es",original,true,entry_page,true,order_page,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[&](state::StateData& data){
        entry_setup=test::prepare_compiler_reader_entry(data,original,entry_page);state::put_draft(data,draft_key,draft);
        data.preferences["u11_assignment_outer_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");
    },cache);
    struct FailureShutdown {std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}
    } failure_shutdown{backend};
    check(!IsWindowVisible(f.host)&&lesson_page_state(f.view).index==entry_page
        &&narration::read_history(read_lesson_state(f.view)).entries.empty(),
        "Outer assignment QA opened another page or seeded listening");
    if(!order_page){
        // The p8 entry requires a real educational receipt from p6. Use the
        // later authored reference only in this isolated QA; keep the reader
        // detached so no compiler-feedback bookmark is mistaken for listening.
        f.reader.reset();
        const auto reference_at=identity_position(original,"block_0008_0001");
        const auto& reference=original.pages[reference_at.page].blocks[reference_at.block];
        check(draft_at.page==5&&reference_at.page==8&&reference.type==curriculum::BlockType::code
            &&compiler.output_check&&compiler.output_check->cases.size()==1
            &&compiler.output_check->cases.front().standard_input.empty(),
            "The published p8 entry requires a newly reviewed compiler setup");
        const auto editor=child(f.view,1010+static_cast<int>(draft_at.block)*100);
        const auto write_program=[&](const std::string& source){const auto value=wide(source);CHARRANGE all{0,-1};
            SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
            SendMessageW(editor,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));pump();
            check(edit_text(editor)==source,"The actual compiler editor changed the authenticated reference");};
        const auto runs=f.environment.paths.projects/"cpp-runs";std::set<std::filesystem::path> earlier;
        if(std::filesystem::exists(runs))for(const auto& item:std::filesystem::directory_iterator(runs))
            if(item.is_directory())earlier.insert(item.path());
        write_program(reference.code);const auto run=child(f.view,1011+static_cast<int>(draft_at.block)*100);
        check(IsWindowEnabled(run),"The prerequisite compiler is not available");SendMessageW(run,BM_CLICK,0,0);
        wait([&]{return IsWindowEnabled(run);},"actual published assignment prerequisite compiler execution");
        check(flush_lesson_view(f.view),"Cannot save the actual prerequisite compiler receipt");
        const auto executed=read_lesson_state(f.view);
        check(windows::compiler_puzzle_completed(executed,original,compiler)
            &&windows::compiler_puzzle_case_count(executed,original,compiler)==1,
            "The actual authored reference did not earn its prerequisite objective");
        std::vector<std::filesystem::path> created;
        for(const auto& item:std::filesystem::directory_iterator(runs))if(item.is_directory()&&!earlier.contains(item.path()))created.push_back(item.path());
        check(created.size()==1,"The p8 setup did not create exactly one educational run");
        for(const auto name:{"main.cpp","stdin.bin","execution-stdout.bin","execution-stderr.bin"}){
            const auto file=created.front()/name;compiler_files.emplace(file,state::sha256(state::read_bytes(file)));}
        const auto program=state::read_bytes(created.front()/"main.cpp");
        check(std::string(program.begin(),program.end())==reference.code
            &&state::read_bytes(created.front()/"stdin.bin").empty()&&state::read_bytes(created.front()/"execution-stderr.bin").empty(),
            "The prerequisite receipt lost its actual captured program or successful execution");
        write_program(reference.code+"\n// QA: borrador posterior al logro; conservar al migrar la lectura\n");
        check(flush_lesson_view(f.view),"Cannot save the independent post-run compiler draft");
        draft=state::get_draft(read_lesson_state(f.view),draft_key,compiler.initial_code);
        check(draft.has_saved&&draft.source!=reference.code,"The later compiler draft was not retained separately from its earned receipt");
        for(auto page=entry_page+1;page<=at.page;++page)
            check(navigate_lesson_view(f.view,page),"An actual prerequisite still blocks the sequential p8 arrival");
        f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
        check(lesson_page_state(f.view).index==at.page&&narration::read_history(read_lesson_state(f.view)).entries.empty(),
            "The actual p8 entry fabricated listening or selected another page");
    }
    if(order_page){
        const auto list=child(f.view,1005+static_cast<int>(at.block)*100);
        const auto order=[&]{std::vector<std::pair<LONG,std::size_t>> rows;
            for(std::size_t index=0;index<old.fragments.size();++index){RECT bounds{};
                const auto row=GetDlgItem(list,100+static_cast<int>(index));
                check(row&&GetWindowRect(row,&bounds),"Missing actual assignment order row");rows.emplace_back(bounds.top,index);}
            std::ranges::sort(rows);std::vector<std::size_t> result;
            for(const auto& [top,index]:rows)result.push_back(index);return result;
        };
        for(std::size_t target=0;target<old.correct_order.size();++target){const auto positions=order();
            auto from=std::ranges::find(positions,old.correct_order[target])-positions.begin();
            check(from<static_cast<std::ptrdiff_t>(positions.size()),"A real assignment fragment disappeared");
            while(from>static_cast<std::ptrdiff_t>(target)){
                SendMessageW(GetDlgItem(list,100+static_cast<int>(old.correct_order[target])),WM_SYSKEYDOWN,VK_UP,LPARAM{1}<<29);pump();--from;
            }
        }
        check(order()==old.correct_order,"The actual assignment rows did not retain the requested order");
        const auto submit=child(f.view,1003+static_cast<int>(at.block)*100);
        check(IsWindowEnabled(submit),"The real assignment order cannot be submitted");
        SendMessageW(submit,BM_CLICK,0,0);f.reader->pause();f.ready();
    }else f.answer(old.answers.front(),at.block);
    const auto accepted=read_lesson_state(f.view);const auto response=state::get_response(accepted,original,old);
    check(response&&response->correct&&response->reviewed_answer_identity.empty()
        &&(order_page?response->order==std::vector<std::int64_t>{2,1,3,0}:response->answer==old.answers.front()),
        "Outer assignment was not accepted through its real lesson controls");
    const auto answered_history=narration::read_history(accepted);
    check(std::ranges::all_of(answered_history.entries,[](const auto& slot){
        return !slot.position.heard_prefix||slot.position.heard_prefix->microseconds==0;}),
        "Answer submission inherited listening before actual PCM playback");
    struct Recording{LessonReaderSegmentSnapshot part;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Recording> recordings;
    for(const auto& part:f.snapshot().segments)if(part.available&&!part.recording.empty())
        recordings.push_back({part,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});
    f.reader->restart();f.reader->pause();const auto planned=f.snapshot();
    const auto prompt_target=child(f.view,1000+static_cast<int>(at.block)*100);
    const auto wanted=std::ranges::find_if(planned.segments,[&](const auto& part){return part.block==at.block
        &&part.feedback==old_feedback&&(old_feedback?part.correct:part.target==prompt_target);});
    check(wanted!=planned.segments.end(),"The authentic outer assignment pause has no spoken source");
    const auto wanted_index=static_cast<std::size_t>(wanted-planned.segments.begin());double preceding=0;
    for(std::size_t index=0;index<wanted_index;++index)preceding+=planned.segments[index].duration_ms;
    // Seeking is only a preview. The following PCM playback earns the pause;
    // no synthetic position or listening certificate is inserted into state.
    f.reader->seek_seconds(static_cast<int>(std::floor(preceding/1000.)));f.reader->toggle();
    wait([&]{const auto value=f.snapshot();check(value.index<=wanted_index,"Old playback skipped the selected outer assignment source");
        return value.playing&&value.index==wanted_index&&value.offset>150&&value.offset+150<value.segments[value.index].duration_ms;
    },"authentic published outer assignment partial PCM playback");
    f.reader->pause();const auto previous=f.snapshot();(void)verify_feedback_recording(previous.segments[previous.index]);
    const auto protected_data=read_lesson_state(f.view);const auto old_history=narration::read_history(protected_data);
    const auto old_slot=std::ranges::find(old_history.entries,previous.key,&narration::Bookmark::key);
    check(old_history.entries.size()==1&&old_slot!=old_history.entries.end()&&old_slot->position.heard_prefix
        &&old_slot->position.heard_prefix->microseconds>0&&old_slot->position.milliseconds==previous.offset
        &&!previous.playing&&!previous.allocated&&!previous.finished,
        "Outer assignment pause lacks released real playback and its source-bound certificate");
    const auto reopen=[&]{
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot checkpoint outer assignment reader QA");
        const auto durable=narration::read_history(read_lesson_state(f.view));DestroyWindow(f.view);f.view=nullptr;
        LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,current,std::move(hooks));
        check(f.view&&lesson_page_state(f.view).index==at.page,"Cannot reopen the published outer assignment page");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();return durable;
    };
    reopen();const auto mapped=f.snapshot();
    const auto new_prompt_target=child(f.view,1000+static_cast<int>(at.block)*100);
    const auto first=std::ranges::find_if(mapped.segments,[&](const auto& part){return part.block==at.block
        &&(order_page?!part.feedback&&part.target==new_prompt_target:part.feedback&&part.correct);});
    check(first!=mapped.segments.end(),"The first revised outer assignment passage is absent");
    const auto first_index=static_cast<std::size_t>(first-mapped.segments.begin());
    check(mapped.key!=previous.key&&!mapped.playing&&!mapped.allocated&&!mapped.finished,
        "Published outer assignment reused an obsolete page key or started playback");
    if(compatible)check(mapped.offset==previous.offset
        &&mapped.segments[mapped.index].region==previous.segments[previous.index].region
        &&mapped.segments[mapped.index].recording==previous.segments[previous.index].recording
        &&mapped.heard_through<=static_cast<std::int64_t>(first_index),
        "The unchanged p8 question lost compatible audio/pause or gained explanation hearing");
    else check(mapped.index==first_index&&mapped.offset==0&&mapped.heard_through<=static_cast<std::int64_t>(first_index),
        "Published outer assignment skipped its first changed question/explanation or inherited listening");
    const auto migrated=narration::read_history(read_lesson_state(f.view));
    check(migrated.entries.size()==1&&migrated.entries.front().key==mapped.key
        &&migrated.entries.front().page==old_slot->page&&migrated.active_key==mapped.key
        &&migrated.active_owner==old_history.active_owner,
        "Published outer assignment did not relabel its existing bookmark in place");
    if(compatible)check(migrated.entries.front().position.heard_prefix==old_slot->position.heard_prefix,
        "The unchanged p8 question lost its genuine recorded hearing");
    else check(!migrated.entries.front().position.heard_prefix||migrated.entries.front().position.heard_prefix->microseconds==0,
        "The revised outer assignment passage inherited an old positive certificate");
    std::vector<std::size_t> feedback,changed_question;
    const auto verify_source=[&](bool is_feedback,const std::string& source,HWND target){
        const auto text=wide(source);std::vector<bool> covered(text.size());
        auto displayed=utf8(text);std::erase(displayed,'`');std::erase(displayed,'\r');
        check(edit_text(target)==displayed,"A revised outer assignment field is not fully displayed");
        for(std::size_t index=0;index<mapped.segments.size();++index){const auto& part=mapped.segments[index];
            if(part.block!=at.block||part.feedback!=is_feedback||part.target!=target)continue;
            check((!is_feedback||part.correct)&&part.start<part.end&&part.end<=text.size()
                &&part.source==utf8(text.substr(part.start,part.end-part.start)),
                "Outer assignment narration differs from its complete displayed UTF-16 source");
            for(auto offset=part.start;offset<part.end;++offset)covered[offset]=true;
            (void)verify_feedback_recording(part);(is_feedback?feedback:changed_question).push_back(index);
        }
        for(std::size_t index=0;index<text.size();++index)
            check(text[index]==L' '||text[index]==L'\n'||text[index]==L'\r'||text[index]==L'\t'||covered[index],
                "Revised outer assignment narration omitted authored text");
    };
    verify_source(true,"✓ Correcto\n\n"+revised.explanation,child(f.view,1004+static_cast<int>(at.block)*100));
    if(order_page)verify_source(false,revised.question,new_prompt_target);
    check(!feedback.empty()&&(!order_page||!changed_question.empty()),"The revised outer assignment has no authored speech");
    const auto preserve=[&]{const auto data=read_lesson_state(f.view);const auto restored=state::get_response(data,current,revised);
        check(restored&&restored->raw_json==response->raw_json&&restored->correct==response->correct
            &&restored->answer==response->answer&&restored->selected==response->selected&&restored->order==response->order
            &&restored->reviewed_answer_identity==response->reviewed_answer_identity,
            "Outer assignment narration changed its real accepted response/review");
        const auto retained=state::get_draft(data,draft_key,compiler.initial_code);
        check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input
            &&retained.indentation_json==draft.indentation_json&&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end
            &&data.preferences.at("u11_assignment_outer_reader_qa")==protected_data.preferences.at("u11_assignment_outer_reader_qa"),
            "Outer assignment narration changed an unrelated draft or preference");
        for(const auto& page:current.pages)for(const auto& block:page.blocks){
            if(curriculum::has_compiler_objective(block))check(
                windows::compiler_puzzle_case_count(data,current,block)==windows::compiler_puzzle_case_count(protected_data,original,block)
                &&windows::compiler_puzzle_completed(data,current,block)==windows::compiler_puzzle_completed(protected_data,original,block),
                "Outer assignment reading changed independently earned compiler credit");
            if(block.stable_id!=revised.stable_id)check(test::compiler_reader_response_unchanged(data,current,block,entry_setup),
                "Outer assignment reading changed a disclosed prerequisite or invented another response");
        }
        for(const auto& [file,hash]:compiler_files)check(state::sha256(state::read_bytes(file))==hash,
            "The reading migration changed the genuine prerequisite compiler run");
        const auto native=protected_data.preferences.find("native_windows");
        if(native!=protected_data.preferences.end())check(data.preferences.contains("native_windows")
            &&data.preferences.at("native_windows")==native->second,"The reading migration changed independent native receipts/preferences");
        for(const auto& recording:recordings){
            check(state::sha256(state::read_bytes(recording.part.recording))==recording.hash
                &&std::filesystem::last_write_time(recording.part.recording)==recording.stamp,
                "Published outer assignment deleted or rewrote original audio");
            const auto now=f.snapshot();const auto retained_part=std::ranges::find_if(now.segments,[&](const auto& part){
                return part.region==recording.part.region&&part.source==recording.part.source&&part.spoken==recording.part.spoken;});
            if(retained_part!=now.segments.end())check(retained_part->recording==recording.part.recording,
                "Published outer assignment regenerated compatible speech");
        }
        check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host),
            "Outer assignment QA altered historical inputs or foreground visibility");
    };
    preserve();const auto durable=reopen();const auto silent=f.snapshot();
    check(silent.key==mapped.key&&silent.index==mapped.index&&silent.offset==mapped.offset&&silent.heard_through==mapped.heard_through
        &&!silent.playing&&!silent.allocated&&narration::read_history(read_lesson_state(f.view))==durable,
        "Published outer assignment did not reopen its mapped pause silently and exactly");preserve();
    if(!compatible){
        std::set<std::size_t> observed;f.reader->toggle();
        wait([&]{const auto value=f.snapshot();if(value.playing)observed.insert(value.index);
            return value.finished&&!value.playing&&!value.allocated;
        },"complete revised outer assignment question/explanation playback",120000);
        check(std::ranges::all_of(feedback,[&](auto index){return observed.contains(index);})
            &&std::ranges::all_of(changed_question,[&](auto index){return observed.contains(index);})
            &&f.snapshot().heard_through>static_cast<std::int64_t>(feedback.back()),
            "Outer assignment completion skipped a revised authored question or explanation");preserve();
    }
    std::cout<<"PASS U11L20 p"<<at.page+1<<" published21-to25 editorial reader phase "<<phase
        <<": actual answer and old PCM pause; "<<(compatible?"compatible question pause retained":"first revised passage revisited at zero and fully played")
        <<"; silent durable reopen, one bounded history slot, response/draft/original audio retained; "<<entry_setup.ordinary_responses.size()
        <<" disclosed synthetic ordinary prerequisites"<<(order_page?"; no educational compiler run":"; one real prerequisite compiler run and its distinct later draft retained")
        <<". No XML import, full-lesson or human-listening acceptance claimed.\n";
}
