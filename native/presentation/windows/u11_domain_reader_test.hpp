#pragma once
// Included after the private reader Fixture in lesson_reader_tests.cpp. Every
// bookmark below comes from real PCM playback in a fresh hidden QA profile.
void u11_domain_label_reader_revision(const std::shared_ptr<windows::NarrationWorker>& backend,bool immediate_only=false){
    using namespace winrt::Windows::Data::Json;
    const auto asset=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json";
    const auto asset_bytes=state::read_bytes(asset);const auto revision=test::u11_domain_label_fixture(windows::load_lesson(asset));
    const auto at=revision.position;const auto& old=revision.before.pages[at.page].blocks[at.block];
    const auto& current=revision.after.pages[at.page].blocks[at.block];const auto previous_choice=identity_position(revision.before,"block_0008_0001");
    const auto& choice=revision.before.pages[previous_choice.page].blocks[previous_choice.block];
    const auto draft_at=identity_position(revision.before,"block_0002_0002");const auto& draft_owner=revision.before.pages[draft_at.page].blocks[draft_at.block];
    const auto draft_key=curriculum::draft_key("programming",revision.before,draft_owner);
    state::Draft draft{"// QA: conservar \t fuente\r\nint main() { return 0; }\n"," entrada\t\r\n",{},3,11,true};
    draft.indentation_json=state::make_indentation_metadata(draft.source,{});
    check(at.page==8&&at.block==2&&previous_choice.page==8&&previous_choice.block==1&&choice.correct_index
        &&revision.before.pages.size()==11&&revision.before.compiler_puzzles_required,"The actual U11L15 reader prerequisites changed");
    const auto matching=JsonObject::Parse(wide(old.raw_json));
    const auto expected=matching.GetNamedArray(L"responses").GetObjectAt(0).GetNamedObject(L"validator").GetNamedObject(L"expected");
    const std::vector<unsigned> phases=immediate_only?std::vector<unsigned>{3u}:std::vector<unsigned>{0u,1u,2u};
    for(const unsigned phase:phases){
        const std::string phase_name=phase==0?"before-label":phase==1?"on-label":phase==2?"after-label":"immediate-reopen";
        std::set<std::string> prerequisite_ids;
        Fixture f(backend,false,true,true,"es",revision.before,false,10,true,true,[](HWND view){
            update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
        },[&](state::StateData& data){
            // These two compiler receipts isolate page nine. They are explicit
            // synthetic prerequisites, never claimed student/MSVC executions.
            for(const auto& page:revision.before.pages)for(const auto& block:page.blocks)if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
                windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
                for(const auto& goal:block.output_check->cases){curriculum::FreshExecution execution{1,true,true,0,goal.standard_input,goal.outputs.front()};
                    execution.executable=goal.executable;check(windows::record_compiler_puzzle_case(data,revision.before,block,request,execution,
                        "Synthetic U11 domain reader prerequisite; no execution"),"Cannot isolate the current U11 domain reader");}
                prerequisite_ids.insert(block.stable_id);
            }
            state::put_draft(data,draft_key,draft);data.preferences["u11_domain_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");
        },{},true);
        check(prerequisite_ids==std::set<std::string>{"block_0002_0002","block_0006_0003"}&&!IsWindowVisible(f.host)
            &&narration::read_history(read_lesson_state(f.view)).entries.empty(),"The U11 reader started with fabricated history or another prerequisite");
        struct Recording{LessonReaderSegmentSnapshot part;std::string hash;std::filesystem::file_time_type stamp;};std::vector<Recording> recordings;
        const auto retain=[&](const LessonReaderSnapshot& snapshot){
            (void)verify_feedback_recording(snapshot.segments[snapshot.index]);
            for(const auto& part:snapshot.segments)if(part.available&&!part.recording.empty())
                recordings.push_back({part,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});
        };
        std::vector<narration::Bookmark> other_slots;
        for(const std::size_t page:{10u,9u}){
            if(lesson_page_state(f.view).index!=page)f.page(page);
            f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.playing&&value.offset>100;},"actual other-page U11 domain playback");
            f.reader->pause();const auto paused=f.snapshot();retain(paused);const auto history=narration::read_history(read_lesson_state(f.view));
            const auto slot=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
            check(slot!=history.entries.end()&&slot->position.milliseconds==paused.offset&&slot->position.heard_prefix
                &&slot->position.heard_prefix->microseconds>0,"Another U11 page slot was not earned through real playback");other_slots.push_back(*slot);
        }
        f.page(at.page);
        const auto card=[&](std::string_view suffix){const auto regions=lesson_reading_regions(f.view);
            const auto found=std::ranges::find_if(regions,[&](const auto& region){return region.key.ends_with(suffix);});
            check(found!=regions.end(),"Missing U11 domain reader card "+std::string(suffix));return *found;};
        const auto tap=[&](std::string_view suffix){const auto target=card(suffix).target;
            pointer_message(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4),false);pointer_message(target,WM_LBUTTONUP,0,MAKELPARAM(4,4),false);pump();};
        // Use the actual preceding choice and six native matching pairs. The
        // deliberately swapped pair is checked by the real mathematical worker.
        SendMessageW(child(f.view,1000+static_cast<int>(previous_choice.block)*100+20+static_cast<int>(*choice.correct_index)*2),BM_CLICK,0,0);
        SendMessageW(child(f.view,1003+static_cast<int>(previous_choice.block)*100),BM_CLICK,0,0);f.reader->pause();f.ready();
        const auto chosen=state::get_response(read_lesson_state(f.view),revision.before,choice);
        check(chosen&&chosen->correct,"The actual preceding U11 choice was not answered by its controls");
        auto wrong=JsonObject::Parse(expected.Stringify());wrong.Insert(L"dominio",JsonValue::CreateStringValue(L"completa"));wrong.Insert(L"valida",JsonValue::CreateStringValue(L"limite"));
        for(const auto& pair:wrong){tap("casos/source/"+utf8(pair.Key()));tap("casos/target/"+utf8(pair.Value().GetString()));}
        SendMessageW(child(f.view,1003+static_cast<int>(at.block)*100),BM_CLICK,0,0);f.reader->pause();
        wait([&]{const auto response=state::get_response(read_lesson_state(f.view),revision.before,old);
            return response&&!response->correct&&state::response_reviewed(*response,old)&&IsWindowEnabled(child(f.view,1003+static_cast<int>(at.block)*100));},
            "actual wrong U11 matching attempt with its stored pairs");f.ready();f.reader->pause();
        const auto protected_data=read_lesson_state(f.view);const auto response=state::get_response(protected_data,revision.before,old);
        const auto expected_guide=edit_text(child(f.view,1004+static_cast<int>(at.block)*100));
        const auto reserve=state::find(protected_data,"attempt_reserve_seconds_test","reserve_programming");
        check(response&&reserve&&std::get_if<double>(reserve)&&std::get<double>(*reserve)<8.,
            "The reader regression did not preserve an actual rejected attempt");
        const auto preserve_state=[&]{const auto data=read_lesson_state(f.view);const auto restored=state::get_response(data,revision.after,current);
            const auto selected=state::get_response(data,revision.after,revision.after.pages[previous_choice.page].blocks[previous_choice.block]);
            check(restored&&restored->raw_json==response->raw_json&&restored->correct==response->correct
                &&restored->reviewed_answer_identity==response->reviewed_answer_identity&&selected&&selected->raw_json==chosen->raw_json&&selected->correct,
                "Narration migration changed the matching pairs, review/attempts or preceding choice");
            const auto retained=state::get_draft(data,draft_key,draft_owner.initial_code);
            check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input&&retained.indentation_json==draft.indentation_json
                &&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end,
                "Narration migration changed the independent compiler draft");
            check(data.preferences.at("u11_domain_reader_qa")==protected_data.preferences.at("u11_domain_reader_qa"),"Narration migration changed unrelated preferences");
            const auto attempts=protected_data.preferences.find("attempt_reserve_seconds_test"),now=data.preferences.find("attempt_reserve_seconds_test");
            check((attempts==protected_data.preferences.end())==(now==data.preferences.end())&&(attempts==protected_data.preferences.end()||attempts->second==now->second),
                "Reader restoration changed the real attempt reserve");
            for(const auto& page:revision.after.pages)for(const auto& compiler:page.blocks)if(compiler.type==curriculum::BlockType::cpp_compiler&&compiler.output_check)
                check(windows::compiler_puzzle_case_count(data,revision.after,compiler)==windows::compiler_puzzle_case_count(protected_data,revision.before,compiler),
                    "Reader restoration changed its explicit synthetic prerequisites");
        };
        f.reader->restart();f.reader->pause();const auto planned=f.snapshot();
        const auto changed=std::ranges::find_if(planned.segments,[](const auto& part){return part.region.ends_with("casos/source/dominio");});
        const auto later=std::ranges::find_if(planned.segments,[](const auto& part){return part.region.ends_with("casos/target/niveles");});
        check(changed!=planned.segments.end()&&changed->source=="100+1;"&&later!=planned.segments.end()&&changed<later,
            "The authentic old U11 source/compatible later card order changed");
        const auto changed_index=static_cast<std::size_t>(changed-planned.segments.begin());
        const auto wanted=phase==0?0:phase==2?static_cast<std::size_t>(later-planned.segments.begin()):changed_index;
        bool heard_changed=false;f.reader->toggle();
        wait([&]{const auto value=f.snapshot();if(value.playing&&value.index==changed_index)heard_changed=true;
            check(value.index<=wanted,"Real U11 narration skipped the selected historical pause");
            return value.playing&&value.index==wanted&&value.offset>100&&value.offset<value.segments[wanted].duration_ms-20;
        },"actual "+phase_name+" historical U11 pause",90000);f.reader->pause();const auto previous=f.snapshot();retain(previous);
        check(phase==0||heard_changed,"The old changed card was not actually played before the selected pause");
        const auto history=narration::read_history(read_lesson_state(f.view));const auto old_slot=std::ranges::find(history.entries,previous.key,&narration::Bookmark::key);
        check(history.entries.size()==3&&old_slot!=history.entries.end()&&old_slot->position.milliseconds==previous.offset
            &&old_slot->position.heard_prefix&&old_slot->position.heard_prefix->microseconds>0,"The historical U11 pause is not genuine bounded playback evidence");
        const auto reopen=[&]{f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush real U11 domain playback before reopening");
            const auto history_to_restore=narration::read_history(read_lesson_state(f.view));DestroyWindow(f.view);f.view=nullptr;
            const bool immediate=phase==3;
            struct RestoreGate{std::atomic_bool entered=false,released=false;};const auto gate=std::make_shared<RestoreGate>();gate->released.store(!immediate);
            LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
            hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
            hooks.validate_math=[backend,gate](std::string_view block,std::string_view answers,std::string_view locale,std::stop_token stop){
                gate->entered.store(true);
                // Main attaches its reader immediately. Hold only this QA
                // restore before entering the real worker, so its partial
                // script can finish planning without a transport deadlock.
                while(!gate->released.load()&&!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(2));
                if(stop.stop_requested())throw std::runtime_error("Cancelled test-owned mathematical restore gate");
                return backend->validate_math(block,answers,locale,stop);
            };
            f.view=create_lesson_view(f.host,f.environment,revision.after,std::move(hooks));check(f.view,"Cannot reopen the actual revised U11 domain lesson");MoveWindow(f.view,0,0,980,720,TRUE);
            if(immediate){
                f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});
                wait([&]{const auto value=f.snapshot();check(!value.playing&&!value.allocated,"A partial U11 restore started playback");
                    return gate->entered.load()&&value.planned&&!value.segments.empty()&&value.segments[value.index].available;
                },"the immediate reader plans while the authentic mathematical restore is withheld");
                const auto partial=f.snapshot();const auto partial_regions=lesson_reading_regions(f.view);
                check(std::ranges::none_of(partial_regions,[&](const auto& region){return region.block_index==at.block&&region.feedback&&region.source_text==expected_guide;})
                    &&std::ranges::none_of(history_to_restore.entries,[&](const auto& slot){return slot.key==partial.key;})
                    &&narration::read_history(read_lesson_state(f.view))==history_to_restore,
                    "The transient partial U11 script consumed the stored full-page bookmark or exposed the unverified actual guide");
                gate->released.store(true);
            }
            wait([&]{const auto saved=state::get_response(read_lesson_state(f.view),revision.after,current);const auto regions=lesson_reading_regions(f.view);
                return saved&&state::response_reviewed(*saved,current)&&edit_text(child(f.view,1004+static_cast<int>(at.block)*100))==expected_guide
                    &&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==at.block&&region.feedback&&region.source_text==expected_guide;});},
                "restored authentic U11 rejected matching response");
            if(!immediate)f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.ready();
        };
        reopen();const auto revised=f.snapshot();const auto label=std::ranges::find_if(revised.segments,[](const auto& part){return part.region.ends_with("casos/source/dominio");});
        check(label!=revised.segments.end()&&label->source=="99+2;"&&label->spoken!=changed->spoken&&label->recording!=changed->recording
            &&card("casos/source/dominio").source_text=="99+2;"&&edit_text(card("casos/source/dominio").target)=="99+2;",
            "The revised real U11 reader retained the old source, speech or WAV");
        const auto new_label_index=static_cast<std::size_t>(label-revised.segments.begin());
        check(revised.key!=previous.key&&!revised.playing&&!revised.allocated&&!revised.finished,"The revised U11 reader reused an obsolete page key or autostarted");
        if(phase==0)check(revised.offset==previous.offset&&revised.segments[revised.index].region==previous.segments[previous.index].region
            &&revised.segments[revised.index].recording==previous.segments[previous.index].recording,
            "A compatible pause before the changed U11 card was lost");
        else check(revised.index==new_label_index&&revised.offset==0&&revised.heard_through<=static_cast<std::int64_t>(new_label_index),
            "An on/after-label U11 pause skipped the revised card or inherited its old listening offset");
        const auto migrated=narration::read_history(read_lesson_state(f.view));const auto new_slot=std::ranges::find(migrated.entries,revised.key,&narration::Bookmark::key);
        check(migrated.entries.size()==3&&new_slot!=migrated.entries.end()&&new_slot-migrated.entries.begin()==old_slot-history.entries.begin()
            &&new_slot->page==old_slot->page&&migrated.active_owner==history.active_owner
            &&migrated.active_key==(history.active_key==previous.key?revised.key:history.active_key)
            &&std::ranges::none_of(migrated.entries,[&](const auto& slot){return slot.key==previous.key;}),
            "The U11 label migration did not relabel its existing bounded history slot in place");
        if(phase>0)check(!new_slot->position.heard_prefix||new_slot->position.heard_prefix->microseconds==0,
            "The revised U11 card inherited a positive old recording certificate");
        const auto preserve_slots=[&]{const auto saved=narration::read_history(read_lesson_state(f.view));check(saved.entries.size()==3,"U11 domain narration exceeded three page slots");
            for(const auto& slot:other_slots){const auto found=std::ranges::find(saved.entries,slot.key,&narration::Bookmark::key);check(found!=saved.entries.end()&&*found==slot,"U11 label migration changed another genuinely played page slot");}};
        const auto preserve_wavs=[&]{const auto value=f.snapshot();for(const auto& recording:recordings){
            check(state::sha256(state::read_bytes(recording.part.recording))==recording.hash&&std::filesystem::last_write_time(recording.part.recording)==recording.stamp,
                "U11 label migration changed or deleted an existing real WAV");
            const auto compatible=std::ranges::find_if(value.segments,[&](const auto& part){return part.region==recording.part.region&&part.source==recording.part.source&&part.spoken==recording.part.spoken;});
            if(compatible!=value.segments.end())check(compatible->recording==recording.part.recording,"U11 label migration regenerated compatible speech instead of reusing its WAV");
        }};
        preserve_state();preserve_slots();preserve_wavs();const auto new_hash=verify_feedback_recording(*label);
        reopen();const auto silent=f.snapshot();check(silent.key==revised.key&&silent.index==revised.index&&silent.offset==revised.offset&&!silent.playing&&!silent.allocated,
            "A second silent reopening moved the migrated U11 pause");preserve_slots();preserve_state();
        if(phase>0){f.reader->toggle();await_resumed_player([&]{return f.snapshot();},silent,"Explicit revised U11 card playback");
            wait([&]{const auto value=f.snapshot();check(value.index==new_label_index,"Explicit playback skipped the revised U11 card");return value.playing&&value.offset>80;},"the revised U11 card really plays");
            f.reader->pause();const auto actual=f.snapshot();const auto actual_history=narration::read_history(read_lesson_state(f.view));const auto earned=std::ranges::find(actual_history.entries,actual.key,&narration::Bookmark::key);
            check(earned!=actual_history.entries.end()&&earned->position.heard_prefix&&earned->position.heard_prefix->microseconds>0
                &&earned->position.heard_prefix->fingerprint!=old_slot->position.heard_prefix->fingerprint,
                "The revised U11 card did not earn its own real playback certificate");
            reopen();const auto restored=f.snapshot();check(restored.key==actual.key&&restored.index==actual.index&&restored.offset==actual.offset&&!restored.playing&&!restored.allocated,
                "The newly earned U11 card pause failed to reopen exactly");
            f.reader->toggle();await_resumed_player([&]{return f.snapshot();},restored,"Newly earned U11 domain pause resumes");f.reader->pause();
        }
        preserve_state();preserve_slots();preserve_wavs();check(state::sha256(state::read_bytes(label->recording))==new_hash&&state::read_bytes(asset)==asset_bytes&&!IsWindowVisible(f.host),
            "The U11 reader changed current PCM, curricular content or the user's desktop");
        std::cout<<"PASS authentic U11 domain "<<phase_name<<": real old/new PCM, UTF-16 timing, source-bound pause/migration and same-slot reopen, pairs/attempts/draft/other two real slots and WAVs retained\n";
    }
    std::cout<<"PASS U11 domain reader revision: "<<(immediate_only?"immediate-reader/deferred-math restoration":"three genuine old pauses before/on/after the changed label")
        <<", new speech earns its own certificate; exactly two synthetic compiler prerequisites, no MSVC execution, XML import, visual/full-course acceptance or human listening claimed\n";
}
