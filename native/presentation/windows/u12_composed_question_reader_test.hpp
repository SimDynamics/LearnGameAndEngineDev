#pragma once

// Complete 15->17 QA documents, with real prerequisite answers and one real
// compiler run. Only p5 receives a reader; no saved listening point is seeded.
void u12_composed_question_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,unsigned phase) {
    check(phase<2,"Invalid composed-output question reader phase");
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="ef77e1f7d47ab04a43690723c0bf18a97d52bb8ab7323c0df48f34997b807d87"
        &&state::sha256(after_bytes)=="72b6f46499f7b902559a51e4a5ee998710489774c49dd42a836e783ad85cd2f1",
        "The composed-output question fixtures differ from complete authenticated 15->17 documents");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    constexpr std::size_t page=4,prediction=1,compiler_page=2,compiler=3;
    const auto& old=before.pages.at(page).blocks.at(prediction);
    const auto& revised=after.pages.at(page).blocks.at(prediction);
    const auto& goal=before.pages.at(compiler_page).blocks.at(compiler);
    const auto& new_goal=after.pages.at(compiler_page).blocks.at(compiler);
    const auto reference=before.pages.at(3).blocks.at(5).code;
    const std::string expected="Validos=2;Rechazados=1\nValidos=0;Rechazados=0\nValidos=5;Rechazados=3\n";
    const std::string expected_raw="Validos=2;Rechazados=1\r\nValidos=0;Rechazados=0\r\nValidos=5;Rechazados=3\r\n";
    check(before.id=="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"
        &&after.id==before.id&&before.pages.size()==5&&after.pages.size()==5
        &&before.state_layout.lineage==after.state_layout.lineage
        &&before.compiler_puzzles_required&&after.compiler_puzzles_required
        &&old.stable_id=="block_0004_0001"&&revised.stable_id==old.stable_id
        &&old.id=="destino_archivo_no_es_stdout"&&revised.id==old.id
        &&old.type==curriculum::BlockType::output_prediction&&revised.type==old.type
        &&old.question=="Supón que se completan apertura y escritura. ¿Qué texto aparece en la salida estándar de este programa?"
        &&revised.question=="Supón que la apertura, la escritura y el cierre tienen éxito. ¿Qué texto aparece en la salida estándar de este programa?"
        &&old.answers==std::vector<std::string>{"Guardado"}&&old.answers==revised.answers
        &&old.code==revised.code&&old.explanation==revised.explanation&&old.review_feedback==revised.review_feedback
        &&goal.stable_id=="block_0002_0003"&&new_goal.stable_id==goal.stable_id
        &&goal.initial_code==new_goal.initial_code&&reference==after.pages.at(3).blocks.at(5).code
        &&goal.output_check&&goal.output_check->cases.size()==1
        &&goal.output_check->cases[0].standard_input.empty()&&goal.output_check->cases[0].outputs==std::vector<std::string>{expected},
        "The complete composed-output question pair changed its owner, real prerequisites, source or accepted output");
    const auto archive=std::ranges::find_if(after.state_layout.reading_revisions,[](const auto& revision){return !revision.positional_page_identity;});
    check(after.state_layout.reading_revisions.size()==2&&archive!=after.state_layout.reading_revisions.end()
        &&archive->editorial_prefix==15&&archive->previous_blocks.size()==2
        &&archive->previous_blocks.contains(goal.stable_id)&&archive->previous_blocks.contains(old.stable_id),
        "The revised composed-output question lacks its exact published two-block reading provenance");
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u12-composed-question-reader"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64())+'-'+std::to_string(phase));
    Fixture f(backend,false,true,false,"es",before,true,0,true,false,{},[](state::StateData& data){
        data.preferences["u12_composed_question_qa"]["opaque"]=std::string("\t conservar\r\n保存");
    },cache);
    struct FailureShutdown{std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}} shutdown{backend};
    f.reader.reset();
    const auto control=[&](std::size_t block,int part){return child(f.view,1000+static_cast<int>(block)*100+part);};
    const auto saved=[&]{check(flush_lesson_view(f.view),"Cannot persist isolated composed-output question QA");return read_lesson_state(f.view);};
    const auto opaque=saved().preferences.at("u12_composed_question_qa");
    const auto runs=f.environment.paths.projects/"cpp-runs";
    const auto directories=[&]{std::vector<std::filesystem::path> result;
        if(std::filesystem::exists(runs))for(const auto& item:std::filesystem::directory_iterator(runs))if(item.is_directory())result.push_back(item.path());
        std::ranges::sort(result);return result;};
    const auto read=[&](const std::filesystem::path& path){const auto bytes=state::read_bytes(path);
        check(std::filesystem::is_regular_file(path),"A real composed-output run artifact is missing");
        return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
    check(!IsWindowVisible(f.host)&&directories().empty()&&narration::read_history(saved()).entries.empty(),
        "The fresh composed-output question profile inherited execution/listening or exposed its window");
    for(const auto& p:before.pages)for(const auto& block:p.blocks)
        check(!state::get_response(saved(),before,block),"A composed-output prerequisite answer was seeded");
    check(!windows::compiler_puzzle_completed(saved(),before,goal)&&windows::compiler_puzzle_case_count(saved(),before,goal)==0,
        "The fresh composed-output profile inherited a compiler achievement");
    std::map<std::string,state::Response> responses;
    const auto remember=[&](std::size_t block,bool correct){const auto& current=before.pages.at(lesson_page_state(f.view).index).blocks.at(block);
        const auto response=state::get_response(saved(),before,current);
        check(response&&response->correct==correct&&(correct||state::response_reviewed(*response,current)),
            "A native composed-output submission lost its actual evaluated answer");responses[current.stable_id]=*response;};
    const auto click=[&](std::size_t block,int part){const auto target=control(block,part);
        check(IsWindowEnabled(target),"A real composed-output prerequisite control is disabled");SendMessageW(target,BM_CLICK,0,0);pump();};
    const auto choose=[&](std::size_t block,int option){click(block,20+option*2);click(block,3);remember(block,true);
        check(responses.at(before.pages.at(lesson_page_state(f.view).index).blocks.at(block).stable_id).selected==option,
            "The composed-output prerequisite retained another native choice");};
    const auto type_answer=[&](std::size_t block,const std::string& answer,bool correct){const auto value=wide(answer);
        SendMessageW(control(block,2),EM_SETSEL,0,-1);SendMessageW(control(block,2),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));
        click(block,3);remember(block,correct);
        check(edit_text(control(block,2))==answer,"A composed-output prediction changed its exact typed characters");};
    const auto advance=[&](std::size_t destination){check(!f.reader&&navigate_lesson_view(f.view,destination),
        "A genuinely completed composed-output prerequisite did not unlock its next page");pump();
        check(lesson_page_state(f.view).index==destination,"Composed-output preparation opened another page");};
    choose(4,2);advance(1);
    type_answer(1,"Turno 2|Turno 2 confirmado",true);type_answer(3,"4:47",true);choose(4,0);choose(5,1);
    check(responses.size()==5,"The composed-output preparation did not earn exactly its five native prior answers");advance(compiler_page);
    const auto source=wide(reference);SendMessageW(control(compiler,10),EM_SETSEL,0,-1);
    SendMessageW(control(compiler,10),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(source.c_str()));pump();
    check(edit_text(control(compiler,10))==reference,"The actual composed-output compiler did not retain the complete published reference");
    click(compiler,11);wait([&]{return IsWindowEnabled(control(compiler,11))
        &&edit_text(control(compiler,34))=="Ejecución terminada (código 0).";},"one real composed-output prerequisite compilation",120000);
    const auto actual_runs=directories();check(actual_runs.size()==1&&actual_runs.front().parent_path()==runs,
        "The prerequisite did not create exactly one private native execution directory");
    const auto run=actual_runs.front();const auto stdout_raw=read(run/"execution-stdout.bin");
    check(read(run/"main.cpp")==reference&&read(run/"stdin.bin").empty()&&stdout_raw==expected_raw
        &&read(run/"execution-stderr.bin").empty()&&std::filesystem::is_regular_file(run/"program.exe")
        &&edit_text(control(compiler,4))=="✓ Correcto"
        &&windows::compiler_puzzle_completed(saved(),before,goal)&&windows::compiler_puzzle_case_count(saved(),before,goal)==1,
        "The real prerequisite lost source, literal three-line output, empty process channels or its independently earned achievement");
    const auto tools=read(run/"toolchain.txt");check(tools.find("MSVC=")!=tools.npos&&tools.find("SDK=")!=tools.npos,
        "The actual composed-output prerequisite has no selected native toolchain evidence");
    std::cout<<"RUN U12 composed-question phase="<<phase<<" directory="<<run.generic_string()
        <<" sourceSha256="<<state::sha256(reference)<<" stdoutSha256="<<state::sha256(stdout_raw)
        <<" stdinBytes=0 stderrBytes=0 exit=0 correct=1\n";
    struct Artifact{std::filesystem::path path;std::string hash;std::uintmax_t size;std::filesystem::file_time_type stamp;};
    std::vector<Artifact> artifacts;for(const auto& item:std::filesystem::directory_iterator(run))if(item.is_regular_file())
        artifacts.push_back({item.path(),state::sha256(state::read_bytes(item.path())),item.file_size(),item.last_write_time()});
    const auto draft_key=curriculum::draft_key("programming",before,goal);
    const auto draft=state::get_draft(saved(),draft_key,goal.initial_code);
    check(draft.has_saved&&draft.source==reference&&draft.input.empty(),"The actually executed prerequisite lost its saved draft");
    const auto prerequisite_data=saved();state::Preferences proofs;for(const auto& [key,value]:prerequisite_data.preferences.at("native_windows"))
        if(key.starts_with("compiler-puzzle/v1/")||key.starts_with("reference-proof/v1/"))proofs.emplace(key,value);
    check(!proofs.empty(),"The actual prerequisite has no sealed execution evidence");
    advance(3);check(edit_text(control(5,1))==reference,"The unlocked p4 lost its complete read-only reference");advance(page);
    check(narration::read_history(saved()).entries.empty(),"Preparation manufactured listening on a prerequisite page");
    update_lesson_preferences(f.view,[](state::StateData& data){auto& settings=data.preferences["lesson_narration_settings"];
        settings["rate"]=3.;settings["kokoro_advance_generation"]=true;});
    f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});
    const auto ready=[&]{wait([&]{const auto value=f.snapshot();return value.planned&&!value.segments.empty()
        &&value.index<value.segments.size()&&std::ranges::all_of(value.segments,[](const auto& part){return part.available;});
    },"complete focal composed-output p5 question PCM and word alignment",120000);
        check(f.snapshot().rate==3.,"The composed-output question lost its selected 3x rate");};
    ready();f.reader->pause();
    const auto answer=phase==0?std::string("Validos=2;Rechazados=1"):std::string("Guardado");
    type_answer(prediction,answer,phase==1);f.reader->pause();ready();
    const auto visible_feedback=edit_text(control(prediction,4));
    const auto expected_feedback=phase==0?std::string("✗ Incorrecto\n\nRevisa tu respuesta e inténtalo de nuevo."):
        "✓ Correcto\n\n"+old.explanation;
    check(visible_feedback==expected_feedback
        &&(phase==1||visible_feedback.find(old.explanation)==visible_feedback.npos),
        "The submitted p5 prediction exposed a hidden success explanation or another verdict");
    struct Recording{std::filesystem::path path;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Recording> recordings;
    const auto retain=[&](const LessonReaderSnapshot& value){for(const auto& part:value.segments)if(part.available&&!part.recording.empty())
        recordings.push_back({part.recording,state::sha256(state::read_bytes(part.recording)),std::filesystem::last_write_time(part.recording)});};
    const auto privacy=[&]{for(const auto& region:lesson_reading_regions(f.view))
        check(region.target!=control(prediction,1)&&region.target!=control(prediction,2),
            "The p5 reader exposed a passive program body or the typed prediction answer");};
    const auto rate=[&](const LessonReaderSnapshot& value){if(value.playing)check(value.playback_rate==3.,"Actual composed-output question audio did not play at 3x");};
    retain(f.snapshot());privacy();const auto original=f.snapshot();
    const auto question=std::ranges::find_if(original.segments,[&](const auto& part){return part.block==prediction&&!part.feedback&&part.target==control(prediction,0);});
    check(question!=original.segments.end(),"The authentic old p5 question is missing from the reader");
    const auto wanted=static_cast<std::size_t>(question-original.segments.begin());
    f.reader->restart();f.reader->pause();double preceding=0;
    for(std::size_t index=0;index<wanted;++index)preceding+=original.segments[index].duration_ms;
    // Preview positions the player; only the explicit Play below earns a real
    // certificate. No bookmark or offset is written by the test.
    f.reader->seek_seconds(static_cast<int>(std::floor(preceding/1000.)));f.reader->toggle();
    wait([&]{const auto value=f.snapshot();rate(value);check(value.index<=wanted,"The old p5 reader skipped its selected authentic question pause");
        return value.playing&&value.index==wanted&&value.offset>150&&value.offset+150<value.segments[value.index].duration_ms;
    },"actual playback inside the published old p5 question");
    f.reader->pause();const auto paused=f.snapshot();(void)verify_feedback_recording(paused.segments[paused.index]);
    const auto history=narration::read_history(saved());const auto old_slot=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
    check(history.entries.size()==1&&old_slot!=history.entries.end()&&old_slot->position.milliseconds==paused.offset
        &&old_slot->position.heard_prefix&&old_slot->position.heard_prefix->microseconds>0,
        "The old p5 question pause has no real source-bound hearing certificate");
    const auto preserve=[&]{const auto data=saved();
        for(const auto& p:after.pages)for(const auto& block:p.blocks){const auto response=state::get_response(data,after,block);
            const auto found=responses.find(block.stable_id);
            if(found==responses.end())check(!response,"P5 migration fabricated an independent activity answer");
            else check(response&&response->raw_json==found->second.raw_json&&response->answer==found->second.answer
                &&response->selected==found->second.selected&&response->correct==found->second.correct
                &&response->reviewed_answer_identity==found->second.reviewed_answer_identity,
                "P5 migration changed a real previous answer, reviewed response or exact payload");
        }
        check(responses.size()==6&&edit_text(control(prediction,2))==answer
            &&edit_text(control(prediction,4))==visible_feedback
            &&windows::compiler_puzzle_completed(data,after,new_goal)&&windows::compiler_puzzle_case_count(data,after,new_goal)==1,
            "P5 question migration lost its typed answer, unchanged feedback or real compiler prerequisite");
        for(const auto& [key,value]:proofs){const auto actual=state::find(data,"native_windows",key);
            check(actual&&*actual==value,"The question-only migration changed its earlier real execution receipt");}
        const auto retained=state::get_draft(data,draft_key,new_goal.initial_code);
        check(retained.has_saved==draft.has_saved&&retained.source==draft.source&&retained.input==draft.input
            &&retained.indentation_json==draft.indentation_json&&retained.selection_start==draft.selection_start
            &&retained.selection_end==draft.selection_end&&data.preferences.at("u12_composed_question_qa")==opaque,
            "P5 question migration changed the prerequisite's exact draft/selection or unrelated preference");
        for(const auto& file:artifacts)check(std::filesystem::file_size(file.path)==file.size
            &&state::sha256(state::read_bytes(file.path))==file.hash&&std::filesystem::last_write_time(file.path)==file.stamp,
            "Question migration changed an actual previous program or raw process stream");
        for(const auto& recording:recordings)check(state::sha256(state::read_bytes(recording.path))==recording.hash
            &&std::filesystem::last_write_time(recording.path)==recording.stamp,"Question migration changed an existing authentic WAV");
        check(directories()==actual_runs&&state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes
            &&lesson_page_state(f.view).index==page&&!IsWindowVisible(f.host),
            "P5 question migration reran a program, changed immutable fixtures, navigated away or exposed its QA window");privacy();
    };
    const auto reopen=[&]{f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush the actual old p5 question pause");
        const auto durable=narration::read_history(saved());DestroyWindow(f.view);f.view=nullptr;LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,after,std::move(hooks));check(f.view!=nullptr,"Cannot mount the complete revised p5 lesson");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});ready();return durable;};
    reopen();const auto mapped=f.snapshot();const auto regions=lesson_reading_regions(f.view);
    const auto region=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==prediction&&!item.feedback&&item.target==control(prediction,0);});
    const auto feedback_region=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==prediction&&item.feedback;});
    check(region!=regions.end()&&region->source_text==revised.question&&edit_text(control(prediction,0))==revised.question,
        "The revised p5 question is not the complete displayed and spoken source");
    check(feedback_region!=regions.end()&&feedback_region->source_text==expected_feedback&&feedback_region->correct==(phase==1),
        "The retained p5 response lost its complete current guide or correct explanation");
    const auto plan=backend->plan(revised.question,"es",{},region->reading_context,region->math_mode);
    const auto feedback_plan=backend->plan(expected_feedback,"es",{},feedback_region->reading_context,feedback_region->math_mode);
    const auto feedback_text=wide(expected_feedback);std::vector<bool> feedback_covered(feedback_text.size());
    const auto text=wide(revised.question);std::vector<bool> covered(text.size());std::vector<std::size_t> parts,feedback_parts;
    for(std::size_t index=0;index<mapped.segments.size();++index){const auto& part=mapped.segments[index];
        if(part.region==feedback_region->key){const auto number=feedback_parts.size();
            check(number<feedback_plan.size()&&part.feedback&&part.correct==(phase==1)&&part.target==control(prediction,4)
                &&part.source==feedback_plan[number].text&&part.spoken==feedback_plan[number].spoken
                &&part.start==feedback_plan[number].start&&part.end==feedback_plan[number].end&&part.start<part.end
                &&part.end<=feedback_text.size()&&part.source==utf8(feedback_text.substr(part.start,part.end-part.start)),
                "The retained p5 feedback lost its complete contextual plan or UTF-16 ownership");
            for(auto offset=part.start;offset<part.end;++offset)feedback_covered[offset]=true;
            feedback_parts.push_back(index);(void)verify_feedback_recording(part);
        }
        if(part.region!=region->key)continue;
        const auto number=parts.size();check(number<plan.size()&&!part.feedback&&part.target==control(prediction,0)
            &&part.source==plan[number].text&&part.spoken==plan[number].spoken&&part.start==plan[number].start&&part.end==plan[number].end
            &&part.start<part.end&&part.end<=text.size()&&part.source==utf8(text.substr(part.start,part.end-part.start)),
            "The revised p5 question lost its complete contextual plan or exact UTF-16 ownership");
        for(auto offset=part.start;offset<part.end;++offset)covered[offset]=true;parts.push_back(index);
        std::cout<<"AUDIO U12 composed-question phase="<<phase<<" part="<<number+1<<" sha256="<<verify_feedback_recording(part)
            <<" path="<<part.recording.generic_string()<<'\n';
    }
    for(std::size_t offset=0;offset<text.size();++offset)check(iswspace(text[offset])||covered[offset],"The revised p5 question truncated its published source");
    for(std::size_t offset=0;offset<feedback_text.size();++offset)check(iswspace(feedback_text[offset])||feedback_covered[offset],"The retained p5 feedback truncated its complete source");
    check(!parts.empty()&&parts.size()==plan.size()&&!feedback_parts.empty()&&feedback_parts.size()==feedback_plan.size()
        &&mapped.key!=paused.key&&mapped.index==parts.front()
        &&mapped.offset==0&&!mapped.finished&&!mapped.playing&&!mapped.allocated&&mapped.heard_through<=static_cast<std::int64_t>(mapped.index),
        "The changed p5 question inherited old milliseconds/listening credit, selected a later passage or autostarted");
    const auto migrated=narration::read_history(saved());const auto slot=std::ranges::find(migrated.entries,mapped.key,&narration::Bookmark::key);
    check(migrated.entries.size()==1&&slot!=migrated.entries.end()&&slot->page==old_slot->page
        &&migrated.active_key==mapped.key&&migrated.active_owner==history.active_owner
        &&(!slot->position.heard_prefix||slot->position.heard_prefix->microseconds==0)
        &&std::ranges::none_of(migrated.entries,[&](const auto& item){return item.key==paused.key;}),
        "P5 migration duplicated its actual bookmark or borrowed an old recording certificate");
    retain(mapped);preserve();const auto durable=reopen();const auto silent=f.snapshot();
    check(silent.key==mapped.key&&silent.index==mapped.index&&silent.offset==mapped.offset&&silent.heard_through==mapped.heard_through
        &&!silent.finished&&!silent.playing&&!silent.allocated&&narration::read_history(saved())==durable,
        "The second revised p5 reopening changed its exact silent checkpoint");
    const auto until=GetTickCount64()+200;wait([&]{const auto value=f.snapshot();check(!value.playing&&!value.allocated&&value.offset==0,
        "The revised p5 question started without explicit Play");return GetTickCount64()>=until;},"the corrected p5 question remains silent",2000);preserve();
    std::set<std::size_t> heard;f.reader->toggle();
    wait([&]{const auto value=f.snapshot();rate(value);if(value.playing)heard.insert(value.index);
        if(phase==1)return value.finished&&!value.playing&&!value.allocated&&!value.preparing;
        return !value.playing&&!value.allocated&&!value.preparing&&!value.finished
            &&value.segments[value.index].block==prediction&&value.segments[value.index].feedback&&!value.segments[value.index].correct
            &&value.offset+1>=value.segments[value.index].duration_ms;
    },phase==0?"complete new p5 question and actual rejected-feedback retry stop":"complete new p5 question, correct explanation and natural page end",120000);
    const auto finished=f.snapshot();
    check(std::ranges::all_of(parts,[&](auto index){return heard.contains(index);})
        &&std::ranges::all_of(feedback_parts,[&](auto index){return heard.contains(index);})
        &&std::ranges::all_of(feedback_parts,[&](auto index){return finished.segments[index].feedback_fully_played;}),
        "Explicit Play skipped part of the corrected p5 question or its complete submitted feedback");preserve();
    std::cout<<"PASS U12 composed-question phase="<<phase<<": complete 15->17 documents; five native prerequisite answers and one real three-line compilation; actual "
        <<(phase==0?"incorrect":"correct")<<" p5 answer retained; real old question pause maps to full new question at zero without certificate/autoplay; second silent reopening, complete contextual PCM/UTF-16 plan and explicit 3x playback through "
        <<(phase==0?"the retry stop":"the correct explanation and natural final page end")
        <<"; unchanged earlier answers/receipt/draft/artifacts/WAV and no additional execution. No Finalizar, subsequent lesson, student construction or human listening acceptance claimed.\n";
}
