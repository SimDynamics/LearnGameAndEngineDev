#pragma once
// Included after the private reader Fixture in lesson_reader_tests.cpp. Every
// bookmark comes from real PCM playback in a new hidden QA profile. Seeking
// below is an explicit preview, never evidence of hearing the skipped prose.
void u11_consult_choice_reader_revision(const std::shared_ptr<windows::NarrationWorker>& backend){
    const auto asset=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_una_tabla_persistente_de_nombres.json";
    const auto asset_bytes=state::read_bytes(asset);
    const auto original=test::choice_revision_before(windows::load_lesson(asset),"block_0000_0003");
    const auto current=test::revise_choice_lesson(original,"block_0000_0003");
    const auto at=identity_position(original,"block_0000_0003");const auto& old=original.pages[at.page].blocks[at.block];
    const auto& revised_choice=current.pages[at.page].blocks[at.block];
    const auto draft_at=identity_position(original,"block_0005_0002");const auto& draft_owner=original.pages[draft_at.page].blocks[draft_at.block];
    const auto draft_key=curriculum::draft_key("programming",original,draft_owner);
    const auto response_key=curriculum::exercise_key(original,old);
    state::Draft draft;draft.source=draft_owner.initial_code+"\n    // QA: fuente propia conservada\r\n";draft.input=" entrada\t\r\n";
    draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=3;draft.selection_end=11;
    check(at.page==0&&at.block==3&&old.correct_index==1&&old.options.size()==3&&current.pages.size()==8
        &&old.options[0]=="La consulta sigue siendo pura porque devuelve un valor neutro."
        &&revised_choice.options[0]=="Conserva el estado de la tabla porque el valor devuelto es neutro.",
        "The actual U11L18 choice reader fixture changed its exact published owner or wording");
    for(const unsigned phase:{0u,1u,2u}){
        const std::string phase_name=phase==0?"before-option":phase==1?"on-option":"after-option";
        // Pages three and two precede this lesson's only required compiler.
        // No completion receipts, answers or listening positions are seeded.
        Fixture f(backend,false,true,true,"es",original,false,2,true,true,[](HWND view){
            update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
        },[&](state::StateData& data){
            state::put_draft(data,draft_key,draft);data.preferences["u11_consult_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");
        });
        check(lesson_page_state(f.view).index==2&&!IsWindowVisible(f.host)
            &&narration::read_history(read_lesson_state(f.view)).entries.empty(),
            "The U11 consultation reader started elsewhere or acquired fabricated history");
        struct Recording{LessonReaderSegmentSnapshot part;std::string hash;std::filesystem::file_time_type stamp;};
        std::vector<Recording> recordings;
        const auto retain=[&](const LessonReaderSnapshot& snapshot){
            (void)verify_feedback_recording(snapshot.segments[snapshot.index]);
            for(const auto& part:snapshot.segments)if(part.available&&!part.recording.empty())
                recordings.push_back({part,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});
        };
        std::vector<narration::Bookmark> other_slots;
        for(const std::size_t page:{2u,1u}){
            if(lesson_page_state(f.view).index!=page)f.page(page);
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.offset>120;},
                "actual other-page U11 consultation playback");
            f.reader->pause();const auto paused=f.snapshot();retain(paused);
            const auto history=narration::read_history(read_lesson_state(f.view));const auto slot=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
            check(slot!=history.entries.end()&&slot->position.milliseconds==paused.offset&&slot->position.heard_prefix
                &&slot->position.heard_prefix->microseconds>0,"Another U11 consultation page slot was not earned through real playback");
            other_slots.push_back(*slot);
        }
        f.page(at.page);f.reader->restart();f.reader->pause();const auto planned=f.snapshot();
        const auto option0=child(f.view,1000+static_cast<int>(at.block)*100+21);
        const auto option1=child(f.view,1000+static_cast<int>(at.block)*100+23);
        const auto changed=std::ranges::find_if(planned.segments,[&](const auto& part){return part.target==option0&&part.source==old.options[0];});
        const auto later=std::ranges::find_if(planned.segments,[&](const auto& part){return part.target==option1&&part.source==old.options[1];});
        const auto earlier=std::ranges::find_if(planned.segments,[&](const auto& part){return part.block==0&&part.source==original.pages[0].blocks[0].text;});
        check(earlier!=planned.segments.end()&&changed!=planned.segments.end()&&later!=planned.segments.end()&&earlier<changed&&changed<later,
            "The actual old U11 consultation reader lost its heading or option order");
        const auto changed_index=static_cast<std::size_t>(changed-planned.segments.begin());
        const auto wanted=static_cast<std::size_t>((phase==0?earlier:phase==1?changed:later)-planned.segments.begin());
        if(phase>0){
            double before_option_ms=0;for(std::size_t index=0;index<changed_index;++index)before_option_ms+=planned.segments[index].duration_ms;
            f.reader->seek_seconds(static_cast<int>(std::floor(before_option_ms/1000.)));
            check(f.snapshot().index<=changed_index,"An explicit consultation preview skipped the old option");
        }
        bool heard_changed=false;f.reader->toggle();
        wait([&]{const auto value=f.snapshot();if(value.playing&&value.index==changed_index&&value.offset>120)heard_changed=true;
            check(value.index<=wanted,"Real U11 consultation narration skipped its selected historical pause");
            return value.playing&&value.index==wanted&&value.offset>120&&value.offset<value.segments[wanted].duration_ms-20;
        },"actual "+phase_name+" historical U11 consultation pause",90000);
        f.reader->pause();const auto previous=f.snapshot();retain(previous);
        check(phase==0||heard_changed,"The changed old consultation option was not actually played before the selected pause");
        const auto protected_data=read_lesson_state(f.view);const auto history=narration::read_history(protected_data);
        const auto old_slot=std::ranges::find(history.entries,previous.key,&narration::Bookmark::key);
        check(history.entries.size()==3&&old_slot!=history.entries.end()&&old_slot->position.milliseconds==previous.offset
            &&old_slot->position.heard_prefix&&old_slot->position.heard_prefix->microseconds>0,
            "The historical consultation pause is not genuine bounded playback evidence");
        const auto preserve_state=[&]{const auto data=read_lesson_state(f.view);const auto retained=state::get_draft(data,draft_key,draft_owner.initial_code);
            check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input&&retained.indentation_json==draft.indentation_json
                &&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end,
                "Consultation narration migration changed the independent compiler draft");
            check(data.preferences.at("u11_consult_reader_qa")==protected_data.preferences.at("u11_consult_reader_qa")
                &&!state::find(data,"learning_progress",response_key)&&!state::get_response(data,current,revised_choice),
                "Consultation narration changed unrelated preferences or manufactured a choice response");
        };
        const auto reopen=[&]{
            f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush real consultation playback before reopening");
            DestroyWindow(f.view);f.view=nullptr;
            LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
            hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
            hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
            f.view=create_lesson_view(f.host,f.environment,current,std::move(hooks));
            check(f.view&&lesson_page_state(f.view).index==at.page,"Cannot reopen the actual revised consultation page");
            MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
        };
        reopen();const auto revised=f.snapshot();const auto target=child(f.view,1000+static_cast<int>(at.block)*100+21);
        const auto label=std::ranges::find_if(revised.segments,[&](const auto& part){return part.target==target&&part.source==revised_choice.options[0];});
        const auto regions=lesson_reading_regions(f.view);
        check(label!=revised.segments.end()&&label->spoken!=changed->spoken&&label->recording!=changed->recording
            &&edit_text(target)==revised_choice.options[0]&&std::ranges::any_of(regions,[&](const auto& region){return region.target==target&&region.source_text==revised_choice.options[0];}),
            "The revised real consultation reader retained the old source, speech or WAV");
        const auto new_option_index=static_cast<std::size_t>(label-revised.segments.begin());
        check(revised.key!=previous.key&&!revised.playing&&!revised.allocated&&!revised.finished,
            "The revised consultation reader reused an obsolete key or started playback");
        if(phase==0)check(revised.offset==previous.offset&&revised.segments[revised.index].region==previous.segments[previous.index].region
            &&revised.segments[revised.index].recording==previous.segments[previous.index].recording,
            "A compatible pause before the changed U11 consultation option was lost");
        else check(revised.index==new_option_index&&revised.offset==0&&revised.heard_through<=static_cast<std::int64_t>(new_option_index),
            "An on/after-option consultation pause skipped the new option or inherited an old recording offset");
        const auto migrated=narration::read_history(read_lesson_state(f.view));const auto new_slot=std::ranges::find(migrated.entries,revised.key,&narration::Bookmark::key);
        check(migrated.entries.size()==3&&new_slot!=migrated.entries.end()&&new_slot-migrated.entries.begin()==old_slot-history.entries.begin()
            &&new_slot->page==old_slot->page&&migrated.active_owner==history.active_owner
            &&migrated.active_key==(history.active_key==previous.key?revised.key:history.active_key)
            &&std::ranges::none_of(migrated.entries,[&](const auto& slot){return slot.key==previous.key;}),
            "Consultation revision did not relabel its existing bounded history slot in place");
        if(phase==0)check(new_slot->position.heard_prefix==old_slot->position.heard_prefix,
            "A compatible consultation pause lost its real original recording certificate");
        else check(!new_slot->position.heard_prefix||new_slot->position.heard_prefix->microseconds==0,
            "The new consultation option inherited a positive old recording certificate");
        const auto preserve_slots=[&]{const auto saved=narration::read_history(read_lesson_state(f.view));check(saved.entries.size()==3,"Consultation narration exceeded three page slots");
            for(const auto& slot:other_slots){const auto found=std::ranges::find(saved.entries,slot.key,&narration::Bookmark::key);
                check(found!=saved.entries.end()&&*found==slot,"Consultation revision changed another genuinely played page slot");}
        };
        const auto preserve_wavs=[&]{const auto value=f.snapshot();for(const auto& recording:recordings){
            check(state::sha256(state::read_bytes(recording.part.recording))==recording.hash&&std::filesystem::last_write_time(recording.part.recording)==recording.stamp,
                "Consultation revision changed or deleted an existing real WAV");
            const auto compatible=std::ranges::find_if(value.segments,[&](const auto& part){return part.region==recording.part.region&&part.source==recording.part.source&&part.spoken==recording.part.spoken;});
            if(compatible!=value.segments.end())check(compatible->recording==recording.part.recording,"Consultation revision regenerated compatible speech instead of reusing its WAV");
        }};
        preserve_state();preserve_slots();preserve_wavs();const auto new_hash=verify_feedback_recording(*label);
        reopen();const auto silent=f.snapshot();check(silent.key==revised.key&&silent.index==revised.index&&silent.offset==revised.offset&&!silent.playing&&!silent.allocated,
            "A second silent reopening moved the migrated consultation pause");preserve_state();preserve_slots();
        if(phase>0){
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},silent,"Explicit revised consultation option playback");
            wait([&]{const auto value=f.snapshot();check(value.index==new_option_index,"Explicit playback skipped the revised consultation option");
                return value.playing&&value.offset>120;},"the revised consultation option really plays");
            f.reader->pause();const auto actual=f.snapshot();const auto actual_history=narration::read_history(read_lesson_state(f.view));
            const auto earned=std::ranges::find(actual_history.entries,actual.key,&narration::Bookmark::key);
            check(earned!=actual_history.entries.end()&&earned->position.heard_prefix&&earned->position.heard_prefix->microseconds>0
                &&earned->position.heard_prefix->fingerprint!=old_slot->position.heard_prefix->fingerprint,
                "The new consultation option did not earn its own real playback certificate");
            reopen();const auto restored=f.snapshot();check(restored.key==actual.key&&restored.index==actual.index&&restored.offset==actual.offset&&!restored.playing&&!restored.allocated,
                "The newly earned consultation option pause failed to reopen exactly");
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},restored,"Newly earned consultation pause resumes");f.reader->pause();
        }
        preserve_state();preserve_slots();preserve_wavs();check(state::sha256(state::read_bytes(label->recording))==new_hash&&state::read_bytes(asset)==asset_bytes&&!IsWindowVisible(f.host),
            "The consultation reader changed current PCM, curricular content or the user's desktop");
        std::cout<<"PASS authentic U11 consultation "<<phase_name<<": real old/new PCM, source-bound pause and same-slot reopen, independent draft, other two real slots and WAVs retained\n";
    }
    std::cout<<"PASS U11 consultation reader revision: three real pauses before/on/after option zero, new speech earns its own certificate; no synthetic compiler receipts, XML import, human listening or full-course acceptance claimed\n";
}
