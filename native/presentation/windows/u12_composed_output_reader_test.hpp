#pragma once
// Full published documents exercise the first stable-layout adoption. The
// independent compiler draft is explicitly QA data; answers/audio are real.
void u12_composed_output_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,unsigned phase) {
    check(phase<3,"Invalid composed-output reader phase");
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="be285a02171b33a68b30900a7739ffd8b8c5ce5ccee98b6f78ec73f41ad797be"
        &&state::sha256(after_bytes)=="eb160976072ae3bb891ea5fab0bb1336a7d24bb0a83282756634d5189814a6f3",
        "The composed-output fixtures differ from complete published 4741c42/05f14ca documents");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    const std::size_t page=phase==2?1:0,body=phase==0?1:phase==1?3:2;
    const auto& old=before.pages.at(page).blocks.at(body);const auto& revised=after.pages.at(page).blocks.at(body);
    const auto& compiler=before.pages.at(2).blocks.at(3);const auto& new_compiler=after.pages.at(2).blocks.at(3);
    check(before.id=="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"
        &&after.id==before.id&&before.pages.size()==5&&after.pages.size()==5&&!before.state_layout.version&&after.state_layout.version==2
        &&before.pages.at(page).stable_id=="page_"+std::to_string(page)
        &&old.stable_id=="block_"+std::to_string(page)+'_'+std::to_string(body)
        &&revised.stable_id==(phase==0?"block_0000_0001":phase==1?"block_0000_0003":"block_0001_0002")
        &&old.type==curriculum::BlockType::paragraph&&revised.type==old.type&&old.text!=revised.text
        &&compiler.type==curriculum::BlockType::cpp_compiler&&new_compiler.initial_code==compiler.initial_code
        &&curriculum::draft_key("programming",before,compiler)==curriculum::draft_key("programming",after,new_compiler)
        &&!after.compiler_puzzles_required&&after.reference_after.empty()&&after.activity_after.empty(),
        "The composed-output adoption changed its authentic prose, independent draft owner or authored availability");
    const auto draft_key=curriculum::draft_key("programming",before,compiler);
    state::Draft draft;draft.source=compiler.initial_code+"\n// QA: conservar borrador independiente\n";
    draft.input=" entrada\t\r\n";draft.indentation_json=state::make_indentation_metadata(draft.source,{});
    draft.selection_start=3;draft.selection_end=12;
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u12-composed-output-reader"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64())+'-'+std::to_string(phase));
    Fixture f(backend,false,true,true,"es",before,true,page,true,false,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[&](state::StateData& data){state::put_draft(data,draft_key,draft);
        data.preferences["u12_composed_output_qa"]["opaque"]=std::string("\t conservar\r\n保存");},cache);
    struct FailureShutdown{std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}} shutdown{backend};
    const auto saved=[&]{return read_lesson_state(f.view);};
    const auto control=[&](std::size_t block,int part){return child(f.view,1000+static_cast<int>(block)*100+part);};
    const auto ready=[&]{wait([&]{const auto value=f.snapshot();return value.planned&&!value.segments.empty()
        &&value.index<value.segments.size()&&std::ranges::all_of(value.segments,[](const auto& part){return part.available;});
    },"complete composed-output contextual synthesis and word alignment",120000);
        check(f.snapshot().rate==3.,"Composed-output narration lost the selected 3x rate");};
    ready();f.reader->pause();
    check(!IsWindowVisible(f.host)&&narration::read_history(saved()).entries.empty(),
        "A fresh composed-output fixture inherited hearing or exposed its QA window");
    for(const auto& p:before.pages)for(const auto& block:p.blocks)
        check(!state::get_response(saved(),before,block),"A composed-output response was seeded");
    const auto opaque=saved().preferences.at("u12_composed_output_qa");
    std::map<std::size_t,state::Response> responses;
    const auto remember=[&](std::size_t block,bool correct){const auto& goal=before.pages.at(page).blocks.at(block);
        const auto response=state::get_response(saved(),before,goal);
        check(response&&response->correct==correct&&(correct||state::response_reviewed(*response,goal)),
            "A real composed-output attempt lost its reviewed response");responses[block]=*response;};
    const auto prediction=[&](std::size_t block,const std::string& answer,bool correct){f.reader->pause();const auto text=wide(answer);
        SendMessageW(control(block,2),EM_SETSEL,0,-1);SendMessageW(control(block,2),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(control(block,3),BM_CLICK,0,0);f.reader->pause();ready();remember(block,correct);
        check(responses.at(block).answer==answer,"The actual composed-output prediction changed its exact typed answer");};
    if(phase<2){const int option=phase==0?0:2;SendMessageW(control(4,20+option*2),BM_CLICK,0,0);
        SendMessageW(control(4,3),BM_CLICK,0,0);f.reader->pause();ready();remember(4,phase==1);
        check(responses.at(4).selected==option,"The actual composed-output choice lost its selected option");}
    if(phase==2){
        prediction(1,"Turno 2|Turno 2 confirmado",true);
        const auto initial=f.snapshot();std::vector<std::size_t> feedback;
        for(std::size_t index=0;index<initial.segments.size();++index){const auto& part=initial.segments[index];
            if(part.block==1&&part.feedback&&part.correct){feedback.push_back(index);(void)verify_feedback_recording(part);}}
        check(!feedback.empty(),"The first real prediction has no authentic correct explanation");
        std::set<std::size_t> heard;f.reader->toggle();
        wait([&]{const auto value=f.snapshot();if(value.playing){check(value.playback_rate==3.,"The prerequisite explanation did not play at 3x");
                if(std::ranges::find(feedback,value.index)!=feedback.end())heard.insert(value.index);}
            return value.playing&&value.segments[value.index].block==2&&value.offset>100;
        },"the actual first prediction explanation permits entering the following prose",120000);
        f.reader->pause();const auto entered=f.snapshot();
        check(std::ranges::all_of(feedback,[&](auto index){return heard.contains(index)&&entered.segments[index].feedback_fully_played;}),
            "The old paragraph was reached without hearing its real prerequisite explanation");
        prediction(3,"4:4",false);
        // Seek backward below preserves the genuinely heard prerequisite;
        // Restart would erase that evidence and correctly restore its gate.
    }
    struct Recording{std::filesystem::path path;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Recording> recordings;
    const auto retain=[&](const LessonReaderSnapshot& value){for(const auto& part:value.segments)if(part.available&&!part.recording.empty())
        recordings.push_back({part.recording,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});};
    const auto privacy=[&](const curriculum::Lesson& lesson){for(const auto& region:lesson_reading_regions(f.view)){
        if(region.block_index>=lesson.pages.at(page).blocks.size())continue;
        const auto type=lesson.pages.at(page).blocks.at(region.block_index).type;
        if(type==curriculum::BlockType::code||type==curriculum::BlockType::output_prediction)
            check(region.target!=control(region.block_index,1),"The composed-output reader exposed an example or exercise code body");
        if(type==curriculum::BlockType::output_prediction)
            check(region.target!=control(region.block_index,2),"The composed-output reader exposed a typed answer");
    }};
    const auto playback_rate=[&](const LessonReaderSnapshot& value){if(value.playing)
        check(value.playback_rate==3.,"Actual composed-output playback did not use the selected 3x rate");};
    privacy(before);retain(f.snapshot());
    const auto original=f.snapshot();
    const auto target=std::ranges::find_if(original.segments,[&](const auto& part){
        return part.block==body&&!part.feedback&&part.target==control(body,0);});
    check(target!=original.segments.end(),"The complete published old prose is absent from the actual reader");
    const auto wanted=static_cast<std::size_t>(target-original.segments.begin());
    f.reader->seek_seconds(-600);double preceding=0;
    for(std::size_t index=0;index<wanted;++index)preceding+=original.segments[index].duration_ms;
    // Preview is positioning only. The following explicit Play, never a saved
    // position or a synthetic certificate, creates the old listening evidence.
    f.reader->seek_seconds(static_cast<int>(std::floor(preceding/1000.)));
    f.reader->toggle();wait([&]{const auto value=f.snapshot();playback_rate(value);
        check(value.index<=wanted,"The old composed-output reader skipped its selected real prose pause");
        return value.playing&&value.index==wanted&&value.offset>150&&value.offset+150<value.segments[value.index].duration_ms;
    },"a real pause inside the published pre-layout composed-output prose");
    f.reader->pause();const auto paused=f.snapshot();(void)verify_feedback_recording(paused.segments[paused.index]);
    const auto original_history=narration::read_history(saved());
    const auto old_slot=std::ranges::find(original_history.entries,paused.key,&narration::Bookmark::key);
    const auto old_page="programming/"+before.id+'/'+std::to_string(page);
    check(original_history.entries.size()==1&&old_slot!=original_history.entries.end()&&old_slot->page==old_page
        &&old_slot->position.milliseconds==paused.offset&&old_slot->position.heard_prefix&&old_slot->position.heard_prefix->microseconds>0,
        "The authentic pre-layout pause lacks its original positional page or real source-bound certificate");
    const auto preserve=[&]{const auto data=saved();
        for(std::size_t p=0;p<after.pages.size();++p)for(std::size_t b=0;b<after.pages[p].blocks.size();++b){const auto& goal=after.pages[p].blocks[b];
            const auto answer=state::get_response(data,after,goal);const auto retained=p==page?responses.find(b):responses.end();
            if(retained==responses.end())check(!answer,"Composed-output migration fabricated an independent answer");
            else check(answer&&answer->raw_json==retained->second.raw_json&&answer->answer==retained->second.answer
                &&answer->correct==retained->second.correct&&answer->selected==retained->second.selected
                &&answer->reviewed_answer_identity==retained->second.reviewed_answer_identity
                &&(answer->correct||state::response_reviewed(*answer,goal)),
                "Composed-output migration changed an actual answer or its reviewed identity");
            if(curriculum::has_compiler_objective(goal))check(windows::compiler_puzzle_case_count(data,after,goal)==0
                &&!windows::compiler_puzzle_completed(data,after,goal),"Prose listening invented a compiler receipt");
        }
        if(phase==2)for(const auto& [block,response]:responses)
            check(edit_text(control(block,2))==response.answer,"Composed-output adoption lost an exact visible prediction draft");
        const auto actual=state::get_draft(data,draft_key,compiler.initial_code);
        check(actual.has_saved&&actual.source==draft.source&&actual.input==draft.input&&actual.indentation_json==draft.indentation_json
            &&actual.selection_start==draft.selection_start&&actual.selection_end==draft.selection_end
            &&data.preferences.at("u12_composed_output_qa")==opaque,"Composed-output adoption changed the independent QA compiler draft or unrelated data");
        for(const auto& recording:recordings)check(state::sha256(state::read_bytes(recording.path))==recording.hash
            &&std::filesystem::last_write_time(recording.path)==recording.stamp,"Stable-layout adoption changed an existing valid WAV");
        check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host),
            "Composed-output adoption changed a published document or exposed its QA window");
        const auto runs=f.environment.paths.projects/"cpp-runs";
        check(!std::filesystem::exists(runs)||std::filesystem::is_empty(runs),"The focal prose test unexpectedly ran a program");privacy(after);
    };
    const auto reopen=[&]{f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush the real composed-output pause");
        const auto durable=narration::read_history(saved());DestroyWindow(f.view);f.view=nullptr;LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,after,std::move(hooks));check(f.view!=nullptr,"Cannot mount the complete adopted composed-output lesson");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});ready();return durable;
    };
    reopen();const auto mapped=f.snapshot();const auto regions=lesson_reading_regions(f.view);
    const auto current=std::ranges::find_if(regions,[&](const auto& region){return region.block_index==body&&!region.feedback&&region.target==control(body,0);});
    auto visible=revised.text;std::erase(visible,'`');
    check(current!=regions.end()&&current->source_text==revised.text&&edit_text(current->target)==visible,
        "The adopted composed-output paragraph differs from its complete displayed and spoken source");
    const auto plan=backend->plan(revised.text,"es",{},current->reading_context,current->math_mode);const auto source=wide(revised.text);
    std::vector<std::size_t> parts;std::vector<bool> covered(source.size());
    for(std::size_t index=0;index<mapped.segments.size();++index){const auto& part=mapped.segments[index];if(part.region!=current->key)continue;
        const auto number=parts.size();check(number<plan.size()&&!part.feedback&&part.target==current->target
            &&part.source==plan[number].text&&part.spoken==plan[number].spoken&&part.start==plan[number].start&&part.end==plan[number].end
            &&part.start<part.end&&part.end<=source.size()&&part.source==utf8(source.substr(part.start,part.end-part.start)),
            "Adopted composed-output prose lost its full contextual plan or exact UTF-16 ownership");
        for(auto at=part.start;at<part.end;++at)covered[at]=true;parts.push_back(index);
        std::cout<<"AUDIO U12 composed phase="<<phase<<" paragraph="<<body<<" part="<<number+1
            <<" sha256="<<verify_feedback_recording(part)<<" path="<<part.recording.generic_string()<<'\n';
    }
    for(std::size_t at=0;at<source.size();++at)check(iswspace(source[at])||covered[at],"The adopted paragraph truncated part of its published source");
    std::cout<<"RESTORE U12 composed phase="<<phase<<" oldPage="<<old_page<<" newPage="<<lesson_page_state(f.view).narration_page_identity
        <<" oldIndex="<<paused.index<<" expectedIndex="<<(parts.empty()?mapped.segments.size():parts.front())
        <<" actualIndex="<<mapped.index<<" offsets="<<paused.offset<<'/'<<mapped.offset
        <<" heard="<<paused.heard_through<<'/'<<mapped.heard_through<<" keyChanged="<<(paused.key!=mapped.key)<<'\n';
    check(parts.size()==plan.size()&&!parts.empty()&&mapped.key!=paused.key&&mapped.index==parts.front()&&mapped.offset==0
        &&!mapped.finished&&!mapped.playing&&!mapped.allocated&&mapped.heard_through<=static_cast<std::int64_t>(mapped.index),
        "First stable-layout adoption lost the old pause's owner or inherited obsolete milliseconds/hearing/autoplay");
    const auto migrated=narration::read_history(saved());const auto slot=std::ranges::find(migrated.entries,mapped.key,&narration::Bookmark::key);
    check(migrated.entries.size()==1&&slot!=migrated.entries.end()&&slot->page==lesson_page_state(f.view).narration_page_identity
        &&slot->page!=old_page&&migrated.active_key==mapped.key&&migrated.active_owner==original_history.active_owner
        &&(!slot->position.heard_prefix||slot->position.heard_prefix->microseconds==0)
        &&std::ranges::none_of(migrated.entries,[&](const auto& entry){return entry.key==paused.key;}),
        "First stable-layout adoption duplicated the old bookmark or borrowed its recording certificate");
    retain(mapped);preserve();const auto durable=reopen();const auto silent=f.snapshot();
    check(silent.key==mapped.key&&silent.index==mapped.index&&silent.offset==mapped.offset&&silent.heard_through==mapped.heard_through
        &&!silent.finished&&!silent.playing&&!silent.allocated&&narration::read_history(saved())==durable,
        "A second adopted reopening changed its exact silent checkpoint or listening history");
    const auto until=GetTickCount64()+200;wait([&]{const auto value=f.snapshot();check(!value.playing&&!value.allocated&&value.offset==0,
        "The adopted composed-output prose started without explicit Play");return GetTickCount64()>=until;},"the adopted prose remains silently paused",2000);
    preserve();
    const std::size_t feedback_block=phase==2?3:4;const bool correct=phase==1;
    const auto& reviewed=after.pages.at(page).blocks.at(feedback_block);
    const auto expected_feedback=correct?"✓ Correcto\n\n"+reviewed.explanation:
        "✗ Incorrecto\n\n"+(phase==0?reviewed.review_feedback_options.at(0):reviewed.review_feedback);
    const auto final_regions=lesson_reading_regions(f.view);
    const auto feedback_region=std::ranges::find_if(final_regions,[&](const auto& region){return region.block_index==feedback_block&&region.feedback;});
    auto visible_feedback=expected_feedback;std::erase(visible_feedback,'`');
    check(feedback_region!=final_regions.end()&&feedback_region->source_text==expected_feedback
        &&feedback_region->correct==correct&&edit_text(feedback_region->target)==visible_feedback,
        "Adoption lost the current correct explanation or the guide appropriate to the actual retained error");
    const auto feedback_plan=backend->plan(expected_feedback,"es",{},feedback_region->reading_context,feedback_region->math_mode);
    const auto feedback_source=wide(expected_feedback);std::vector<std::size_t> feedback_parts;
    std::vector<bool> feedback_covered(feedback_source.size());const auto prepared=f.snapshot();
    for(std::size_t index=0;index<prepared.segments.size();++index){const auto& part=prepared.segments[index];if(part.region!=feedback_region->key)continue;
        const auto number=feedback_parts.size();check(number<feedback_plan.size()&&part.feedback&&part.correct==correct
            &&part.source==feedback_plan[number].text&&part.spoken==feedback_plan[number].spoken
            &&part.start==feedback_plan[number].start&&part.end==feedback_plan[number].end&&part.start<part.end&&part.end<=feedback_source.size()
            &&part.source==utf8(feedback_source.substr(part.start,part.end-part.start)),
            "The retained response's current guide/explanation lost its complete contextual UTF-16 source");
        for(auto at=part.start;at<part.end;++at)feedback_covered[at]=true;feedback_parts.push_back(index);
        std::cout<<"AUDIO U12 composed phase="<<phase<<" feedbackPart="<<number+1<<" sha256="<<verify_feedback_recording(part)
            <<" path="<<part.recording.generic_string()<<'\n';
    }
    check(!feedback_parts.empty()&&feedback_parts.size()==feedback_plan.size(),"The retained response's complete feedback plan is missing");
    for(std::size_t at=0;at<feedback_source.size();++at)check(iswspace(feedback_source[at])||feedback_covered[at],"The current response feedback was truncated");
    std::set<std::size_t> heard;f.reader->toggle();
    wait([&]{const auto value=f.snapshot();playback_rate(value);
        if(value.playing&&(std::ranges::find(parts,value.index)!=parts.end()||std::ranges::find(feedback_parts,value.index)!=feedback_parts.end()))heard.insert(value.index);
        if(correct)return value.finished&&!value.playing&&!value.allocated&&!value.preparing;
        return !value.playing&&!value.allocated&&!value.preparing&&value.index==feedback_parts.back()
            &&value.offset+1>=value.segments[value.index].duration_ms;
    },correct?"the complete adopted paragraph and retained success explanation":
        "the complete adopted paragraph and current wrong guide stop for retry",120000);
    const auto ended=f.snapshot();
    check(std::ranges::all_of(parts,[&](auto index){return heard.contains(index);})
        &&std::ranges::all_of(feedback_parts,[&](auto index){return heard.contains(index)&&ended.segments[index].feedback_fully_played;})
        &&ended.finished==correct,"Explicit playback skipped new prose or full appropriate feedback, or wrongly completed an unresolved page");
    if(!correct){check(!lesson_page_state(f.view).can_next,"The retained wrong response unlocked another page");
        f.reader->seek_seconds(600);const auto sought=f.snapshot();
        check(sought.segments[sought.index].feedback&&sought.segments[sought.index].block==feedback_block
            &&!sought.finished&&sought.heard_through==ended.heard_through,
            "Forward seek escaped the retained rejected response or fabricated hearing");}
    preserve();const auto final_checkpoint=f.snapshot();const auto final_history=reopen();const auto restored_final=f.snapshot();
    check(restored_final.key==final_checkpoint.key&&restored_final.index==final_checkpoint.index&&restored_final.offset==final_checkpoint.offset
        &&restored_final.heard_through==final_checkpoint.heard_through&&restored_final.finished==final_checkpoint.finished&&!restored_final.playing&&!restored_final.allocated
        &&narration::read_history(saved())==final_history,"Final adopted reopening lost actual playback or silently replayed");preserve();
    std::cout<<"PASS U12L4 composed-output adoption phase "<<phase<<": full published no-map 4741c42 to v2 05f14ca; actual "
        <<(phase==0?"rejected choice":phase==1?"correct choice":"first prediction correctly heard and second prediction rejected")
        <<" retained; genuine old prose pause moves to the same revised paragraph at zero without inherited audio credit/autoplay; "
        <<"complete new contextual PCM/timings/UTF-16 and explicit 3x playback, appropriate complete feedback and natural retry/finish boundary; "
        <<"two silent adopted reopens, original WAV/source and independent explicit QA draft preserved. No compiler run, synthetic hearing, "
        <<"color acceptance, human listening acceptance or whole-course completion claimed.\n";
}
