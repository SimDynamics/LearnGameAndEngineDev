#pragma once
// Real p4/p5 prerequisites are performed first with the reader detached. The
// focal p6/p8 reader then earns its own audio evidence; nothing is seeded.
void current_u12_file_repairs_reader(const std::shared_ptr<windows::NarrationWorker>& backend) {
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_cerrar_reemplazar_y_anadir_datos.json";
    const auto asset_bytes=state::read_bytes(asset);
    std::string lf(reinterpret_cast<const char*>(asset_bytes.data()),asset_bytes.size());std::erase(lf,'\r');
    check(state::sha256(lf)=="c74bb6b67eb7fa3f9d08b376324fd82f4a2ba6ef9e0e6e02d4c89d6b27e6de21",
        "The file-repairs reader asset differs from its audited complete current LF document");
    const auto lesson=windows::load_lesson(asset);
    const auto& preparation=lesson.pages.at(3).blocks.at(3);
    const auto& mode=lesson.pages.at(5).blocks.at(3);
    const auto& separator=lesson.pages.at(7).blocks.at(3);
    const auto& reference=lesson.pages.at(4).blocks.at(1).code;
    const auto& mode_choice=lesson.pages.at(5).blocks.at(1);
    const auto& separator_choice=lesson.pages.at(7).blocks.at(1);
    auto repaired_mode=mode.initial_code;
    const std::string opening="std::ofstream salida{\"serie_lgp.txt\"};";
    const auto first=repaired_mode.find(opening),second=first==repaired_mode.npos?repaired_mode.npos:repaired_mode.find(opening,first+opening.size());
    check(first!=repaired_mode.npos&&second!=repaired_mode.npos&&repaired_mode.find(opening,second+opening.size())==repaired_mode.npos,
        "The focal mode repair no longer has exactly its two original openings");
    repaired_mode.replace(second,opening.size(),"std::ofstream salida{\"serie_lgp.txt\", std::ios::app};");
    auto repaired_separator=separator.initial_code;
    const std::string missing_separator="salida << \"5\";";
    const auto insertion=repaired_separator.find(missing_separator);
    check(insertion!=repaired_separator.npos&&repaired_separator.find(missing_separator,insertion+missing_separator.size())==repaired_separator.npos,
        "The focal separator repair lost its one second insertion");
    repaired_separator.replace(insertion,missing_separator.size(),"salida << \"\\n5\";");
    check(lesson.id=="cpp_fundamentals__archivos_y_programa_completo__cerrar_reemplazar_y_anadir_datos"
        &&lesson.pages.size()==9&&lesson.compiler_puzzles_required&&lesson.reference_after.size()==3&&lesson.activity_after.size()==2
        &&preparation.stable_id=="block_0003_0003"&&preparation.initial_code.size()==130&&reference.size()==1049
        &&mode.stable_id=="block_0005_0003"&&mode.initial_code.size()==1034&&repaired_mode==reference
        &&separator.stable_id=="block_0007_0003"&&separator.initial_code.size()==1048&&repaired_separator.size()==1050
        &&mode_choice.correct_index==0&&separator_choice.correct_index==1,
        "The current file-repairs reader identities, exact sources or choice oracles changed");
    for(const auto* goal:{&preparation,&mode,&separator})check(goal->output_check&&goal->output_check->cases.size()==1
        &&goal->output_check->cases.front().standard_input.empty()
        &&goal->output_check->cases.front().outputs==std::vector<std::string>{"Total=12","Total=12\n"}
        &&goal->output_check->cases.front().explanation.empty()&&goal->output_check->cases.front().review_feedback.empty(),
        "A current file repair lost its exact independent output contract or bare verdict");
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u12-file-repairs-reader"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    Fixture f(backend,false,true,false,"es",lesson,true,3,true,false,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[](state::StateData& data){data.preferences["u12_file_repairs_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");},cache);
    f.reader.reset(); // Actual preparation below supplies no listening credit.
    struct FailureShutdown{std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}} shutdown{backend};
    const auto saved=[&]{return read_lesson_state(f.view);};
    const auto control=[&](std::size_t block,int part){return child(f.view,1000+static_cast<int>(block)*100+part);};
    const auto opaque=saved().preferences.at("u12_file_repairs_reader_qa");
    const auto root=f.environment.paths.projects/"cpp-runs";
    const auto directories=[&]{std::set<std::filesystem::path> result;if(std::filesystem::exists(root))
        for(const auto& item:std::filesystem::directory_iterator(root))if(item.is_directory())result.insert(item.path());return result;};
    const auto bytes=[](const std::filesystem::path& path){const auto raw=state::read_bytes(path);
        return std::string(reinterpret_cast<const char*>(raw.data()),raw.size());};
    struct File{std::filesystem::path path;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<File> files;
    const auto retain=[&](const std::filesystem::path& path){files.push_back({path,state::sha256(state::read_bytes(path)),std::filesystem::last_write_time(path)});};
    std::map<std::string,state::Response> answers;
    std::map<std::string,state::Draft> drafts;
    std::set<std::string> receipts;
    std::optional<narration::Bookmark> retained_mode_bookmark;
    const auto remember_answer=[&](std::size_t block){const auto page=lesson_page_state(f.view).index;const auto& goal=lesson.pages.at(page).blocks.at(block);
        const auto response=state::get_response(saved(),lesson,goal);check(response.has_value(),"A real file-repair action lost its answer");answers[goal.stable_id]=*response;};
    const auto preserve=[&]{const auto data=saved();
        check(state::read_bytes(asset)==asset_bytes&&!IsWindowVisible(f.host)&&data.preferences.at("u12_file_repairs_reader_qa")==opaque,
            "File-repair QA changed the curriculum, unrelated data or desktop visibility");
        for(const auto& file:files)check(state::sha256(state::read_bytes(file.path))==file.hash&&std::filesystem::last_write_time(file.path)==file.stamp,
            "A later file repair or reopening changed earlier WAV or execution evidence");
        for(const auto& page:lesson.pages)for(const auto& goal:page.blocks){const auto found=answers.find(goal.stable_id);const auto answer=state::get_response(data,lesson,goal);
            check(found==answers.end()?!answer:answer&&answer->raw_json==found->second.raw_json&&answer->correct==found->second.correct
                &&answer->selected==found->second.selected&&answer->reviewed_answer_identity==found->second.reviewed_answer_identity,
                "File-repair QA changed another answer or fabricated curricular work");
            if(curriculum::has_compiler_objective(goal)){const bool credited=receipts.contains(goal.stable_id);
                check(windows::compiler_puzzle_completed(data,lesson,goal)==credited
                    &&windows::compiler_puzzle_case_count(data,lesson,goal)==(credited?1u:0u),"A file repair changed an independent real execution receipt");}}
        for(const auto& [key,expected]:drafts){const auto actual=state::get_draft(data,key,expected.source,expected.input);
            check(actual.has_saved==expected.has_saved&&actual.source==expected.source&&actual.input==expected.input
                &&actual.indentation_json==expected.indentation_json&&actual.selection_start==expected.selection_start&&actual.selection_end==expected.selection_end,
                "A file repair changed an earlier editor/input draft");}
        if(retained_mode_bookmark){const auto history=narration::read_history(data);const auto at=std::ranges::find(history.entries,retained_mode_bookmark->key,&narration::Bookmark::key);
            check(at!=history.entries.end()&&*at==*retained_mode_bookmark,"The separator repair changed the genuinely played mode-page bookmark");}
    };
    const auto ready=[&]{check(f.reader!=nullptr,"Focal repair narration is not mounted");
        wait([&]{const auto value=f.snapshot();return value.planned&&!value.segments.empty()&&value.index<value.segments.size()
            &&std::ranges::all_of(value.segments,[](const auto& part){return part.available;});},"complete focal file-repair synthesis and word alignment",120000);
        check(f.snapshot().rate==3.,"File-repair narration lost the selected 3x rate");};
    const auto navigate=[&](std::size_t page,bool prepare){if(f.reader)f.reader->pause();
        check(navigate_lesson_view(f.view,page),"A real file-repair prerequisite did not allow its next page");
        if(f.reader){f.reader->content_changed();if(prepare)ready();}};
    const auto choose=[&](std::size_t block,int option,bool prepare){if(f.reader)f.reader->pause();
        check(IsWindowEnabled(control(block,20+option*2))!=FALSE,"A dependent repair choice is disabled");
        SendMessageW(control(block,20+option*2),BM_CLICK,0,0);check(edit_text(control(block,4)).empty(),"An unchecked repair choice retained feedback");
        check(IsWindowEnabled(control(block,3))!=FALSE,"A real selected repair choice cannot be checked");
        SendMessageW(control(block,3),BM_CLICK,0,0);if(f.reader)f.reader->pause();if(prepare)ready();remember_answer(block);
        const auto& goal=lesson.pages.at(lesson_page_state(f.view).index).blocks.at(block);const auto response=state::get_response(saved(),lesson,goal);
        check(response&&response->selected==option&&response->correct==(option==goal.correct_index)
            &&(response->correct||state::response_reviewed(*response,goal)),"A real repair choice lost its reviewed answer identity");};
    const auto compile=[&](std::size_t block,const std::string& source,int exit,const std::string& output,const std::string& data,bool correct,bool prepare){
        const auto page=lesson_page_state(f.view).index;const auto& goal=lesson.pages.at(page).blocks.at(block);
        if(f.reader)f.reader->pause();if(edit_text(control(block,10))!=source){const auto text=wide(source);CHARRANGE all{0,-1};
            SendMessageW(control(block,10),EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));SendMessageW(control(block,10),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));}
        const auto old=directories();SendMessageW(control(block,11),BM_CLICK,0,0);
        wait([&]{return IsWindowEnabled(control(block,11))!=FALSE;},"actual file-repair compilation and execution");if(f.reader)f.reader->pause();if(prepare)ready();
        const auto current=directories();std::vector<std::filesystem::path> added;for(const auto& path:current)if(!old.contains(path))added.push_back(path);
        check(current.size()==old.size()+1&&added.size()==1&&added.front().parent_path()==root,"One repair action did not create exactly one new private execution");
        const auto run=added.front();const auto status=exit==0?"Ejecución terminada (código 0).":"La ejecución falló (código 1).";
        check(bytes(run/"main.cpp")==source&&bytes(run/"stdin.bin").empty()&&bytes(run/"execution-stdout.bin")==output
            &&bytes(run/"execution-stderr.bin").empty()&&bytes(run/"serie_lgp.txt")==data
            &&std::filesystem::is_regular_file(run/"program.exe")&&edit_text(control(block,34))==status,
            "A real repair run differs from its exact captured source, exit, streams or data file");
        check(windows::compiler_puzzle_completed(saved(),lesson,goal)==correct,"A repair run verdict disagrees with its actual execution");
        if(correct)receipts.insert(goal.stable_id);
        check(flush_lesson_view(f.view),"Cannot flush a real repair draft");
        const auto key=curriculum::draft_key("programming",lesson,goal);drafts[key]=state::get_draft(saved(),key,goal.initial_code,goal.standard_input.value_or(""));
        for(const auto& item:std::filesystem::directory_iterator(run))if(item.is_regular_file())retain(item.path());
        std::cout<<"RUN U12 repairs p"<<page+1<<" directory="<<run.generic_string()<<" sourceSha="<<state::sha256(source)
            <<" stdoutSha="<<state::sha256(output)<<" fileSha="<<state::sha256(data)<<" exit="<<exit<<" correct="<<correct<<'\n';preserve();
    };
    check(directories().empty()&&narration::read_history(saved()).entries.empty(),"A fresh repair profile inherited executions or hearing");preserve();
    std::cout<<"PHASE U12 repairs real p4/p5 prerequisites with reader detached; no listening claimed\n";
    choose(2,2,false);compile(3,reference,0,"Total=12","3 4 5",true,false);
    navigate(4,false);choose(3,0,false);navigate(5,false);
    check(narration::read_history(saved()).entries.empty(),"Detached prerequisite work fabricated a listening bookmark");
    update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["kokoro_advance_generation"]=true;});
    f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});ready();f.reader->pause();preserve();
    const auto privacy=[&]{const auto page=lesson_page_state(f.view).index;const auto& goal=lesson.pages.at(page).blocks.at(3);
        const auto regions=lesson_reading_regions(f.view);
        for(const auto& region:regions)check(region.target!=control(3,10)&&region.target!=control(3,14),
            "The repair reader exposed editor or console contents as spoken curriculum");
        if(!windows::compiler_puzzle_completed(saved(),lesson,goal)){
            check(std::ranges::any_of(regions,[](const auto& region){return region.block_index==3&&region.required_compiler
                &&region.is_exercise&&!region.solved;}),"The actual pending repair editor has no required reader boundary");
            const auto options=lesson_options_snapshot(f.view);
            check(!lesson_page_state(f.view).can_next&&std::ranges::find(options.available_pages,page+1)==options.available_pages.end()
                &&!navigate_lesson_view(f.view,page+1),"A pending repair opened its posterior reference");
            if(page==5)check(std::ranges::find(options.available_pages,7)==options.available_pages.end()&&!navigate_lesson_view(f.view,7),
                "An incomplete mode repair opened its dependent separator choice");
        }
    };
    const auto feedback_parts=[&](std::size_t block,bool correct,const std::string& expected){
        const auto regions=lesson_reading_regions(f.view);const auto region=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==block&&item.feedback;});
        auto visible=expected;std::erase(visible,'`');
        check(region!=regions.end()&&region->source_text==expected&&region->target==control(block,4)
            &&region->correct==correct&&region->solved==correct&&edit_text(region->target)==visible,
            "The actual repair feedback lost or mixed its complete authored source/verdict");
        const auto plan=backend->plan(expected,"es",{},region->reading_context,region->math_mode);const auto source=wide(expected);
        const auto value=f.snapshot();std::vector<std::size_t> indices;std::vector<bool> covered(source.size());
        for(std::size_t index=0;index<value.segments.size();++index){const auto& part=value.segments[index];if(part.region!=region->key)continue;
            const auto number=indices.size();check(number<plan.size()&&part.feedback&&part.correct==correct&&part.target==region->target
                &&part.source==plan[number].text&&part.spoken==plan[number].spoken&&part.start==plan[number].start&&part.end==plan[number].end
                &&part.start<part.end&&part.end<=source.size()&&part.source==utf8(source.substr(part.start,part.end-part.start)),
                "The repair reader omitted or remapped a full UTF-16 feedback passage");
            for(auto at=part.start;at<part.end;++at)covered[at]=true;
            const auto hash=verify_feedback_recording(part);retain(part.recording);indices.push_back(index);
            std::cout<<"AUDIO U12 repairs p"<<lesson_page_state(f.view).index+1<<" block="<<block<<" correct="<<correct
                <<" part="<<number+1<<" sha256="<<hash<<" path="<<part.recording.generic_string()<<'\n';}
        for(std::size_t at=0;at<source.size();++at)check(iswspace(source[at])||covered[at],"The complete repair feedback was truncated");
        check(!indices.empty()&&indices.size()==plan.size()&&value.index==indices.front()&&value.offset==0&&!value.playing&&!value.allocated,
            "Fresh repair feedback lost its zero start or overrode manual pause");return indices;
    };
    const auto reopen=[&](const LessonReaderSnapshot& paused){const auto page=lesson_page_state(f.view).index;
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot checkpoint the isolated repairs page");
        const auto data=saved();const auto history=narration::read_history(data);
        const auto& goal=lesson.pages.at(page).blocks.at(3);const auto key=curriculum::draft_key("programming",lesson,goal);
        const auto draft=state::get_draft(data,key,goal.initial_code,goal.standard_input.value_or(""));
        DestroyWindow(f.view);f.view=nullptr;LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        f.view=create_lesson_view(f.host,f.environment,lesson,std::move(hooks));check(f.view!=nullptr,"Cannot reopen the complete current repairs lesson");
        MoveWindow(f.view,0,0,980,720,TRUE);f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});ready();
        const auto restored=f.snapshot();const auto restored_draft=state::get_draft(saved(),key,goal.initial_code,goal.standard_input.value_or(""));
        std::cout<<"RESTORE U12 repairs p"<<page+1<<" keyEqual="<<(restored.key==paused.key)<<" index="<<paused.index<<'/'<<restored.index
            <<" offset="<<paused.offset<<'/'<<restored.offset<<" heard="<<paused.heard_through<<'/'<<restored.heard_through<<'\n';
        check(lesson_page_state(f.view).index==page&&restored.key==paused.key&&restored.index==paused.index&&restored.offset==paused.offset
            &&restored.heard_through==paused.heard_through&&restored.finished==paused.finished&&!restored.playing&&!restored.allocated
            &&narration::read_history(saved())==history,"A repairs reopen changed the exact real pause, history or silent state");
        check(restored_draft.has_saved==draft.has_saved&&restored_draft.source==draft.source&&restored_draft.input==draft.input
            &&restored_draft.indentation_json==draft.indentation_json&&restored_draft.selection_start==draft.selection_start
            &&restored_draft.selection_end==draft.selection_end&&edit_text(control(3,10))==draft.source,
            "A repairs reopen changed its current editor/input draft");
        const auto until=GetTickCount64()+200;wait([&]{const auto value=f.snapshot();check(!value.playing&&!value.allocated&&value.offset==paused.offset,
            "A restored repair reader started without explicit Play");return GetTickCount64()>=until;},"a restored repair pause remains silent",2000);
        privacy();preserve();
    };
    const auto pending_editor=[&]{f.reader->pause();f.reader->seek_seconds(600);const auto preview=f.snapshot();
        check(preview.segments[preview.index].block==3&&!preview.finished&&!preview.allocated,
            "Forward preview escaped a required repair editor");f.reader->toggle();
        wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==3;},
            "the current repair editor waits for a real passing execution");privacy();};
    const auto play_feedback=[&](const std::vector<std::size_t>& indices,bool correct,bool whole_page){std::set<std::size_t> heard;
        const auto observe=[&](const LessonReaderSnapshot& value){if(value.playing){
            check(value.playback_rate==3.,"Real file-repair feedback playback did not use the selected 3x rate");
            if(std::ranges::find(indices,value.index)!=indices.end())heard.insert(value.index);}};
        f.reader->toggle();
        if(!correct){wait([&]{const auto value=f.snapshot();observe(value);return value.playing&&value.index==indices.front()
                &&value.offset>100&&value.offset+150<value.segments[value.index].duration_ms;},"real authored repair guide partial playback");
            f.reader->pause();const auto paused=f.snapshot();const auto history=narration::read_history(saved());
            const auto slot=std::ranges::find(history.entries,paused.key,&narration::Bookmark::key);
            check(slot!=history.entries.end()&&slot->position.milliseconds==paused.offset&&slot->position.heard_prefix
                &&slot->position.heard_prefix->microseconds>0,"The authored repair guide pause lacks a real source-bound certificate");
            reopen(paused);f.reader->toggle();await_resumed_player([&]{return f.snapshot();},paused,"Explicit repair-guide resume");}
        wait([&]{const auto value=f.snapshot();observe(value);
            if(!correct)return !value.playing&&!value.allocated&&!value.preparing&&value.index==indices.back()
                &&value.offset+1>=value.segments[value.index].duration_ms;
            if(whole_page)return value.finished&&!value.playing&&!value.allocated&&!value.preparing;
            return value.playing&&value.segments[value.index].block==2&&value.offset>100;
        },!correct?"the complete authored repair guide stops for retry":whole_page?"complete bare repair Correcto playback":"complete repair choice explanation before task prose",120000);
        f.reader->pause();const auto ended=f.snapshot();
        check(std::ranges::all_of(indices,[&](auto index){return heard.contains(index)&&ended.segments[index].feedback_fully_played;}),
            "Repair playback skipped part of its complete source-bound feedback");
        if(!correct){check(!ended.finished&&!lesson_page_state(f.view).can_next,"Rejected repair feedback completed the page");
            f.reader->seek_seconds(600);const auto sought=f.snapshot();check(sought.segments[sought.index].feedback
                &&sought.segments[sought.index].block==1&&!sought.finished&&sought.heard_through==ended.heard_through,
                "Seeking escaped the unresolved repair guide or credited unheard content");}
        else if(whole_page)check(ended.finished&&lesson_page_state(f.view).can_next,"The actual fully played Correcto did not finish its repair page");
        preserve();
    };
    struct Repair{std::size_t page;int wrong,correct;const char* guide;const char* explanation;const std::string* initial;const std::string* fixed;const char* initial_data;const char* final_data;};
    const std::array<Repair,2> repairs{{
        {5,1,0,"La extracción de un entero omite los espacios iniciales. Comprueba cuántos valores numéricos conserva el archivo.",
            "La segunda apertura de salida predeterminada reemplaza la muestra. El espacio es válido; lo que falta son los dos valores previos.",
            &mode.initial_code,&repaired_mode," 5","3 4 5"},
        {7,0,1,"Localiza qué carácter sigue inmediatamente al último carácter ya escrito. El modo de apertura no añade caracteres que no se hayan escrito.",
            "Las inserciones concatenan caracteres; app decide dónde escribir, sin añadir separadores. Sólo se obtienen dos enteros.",
            &separator.initial_code,&repaired_separator,"3 45","3 4\r\n5"}
    }};
    for(const auto& repair:repairs){
        if(lesson_page_state(f.view).index!=repair.page)navigate(repair.page,true);
        const auto& choice=lesson.pages.at(repair.page).blocks.at(1);const auto& goal=lesson.pages.at(repair.page).blocks.at(3);
        check(choice.review_feedback_options.at(static_cast<std::size_t>(repair.wrong))==repair.guide&&choice.explanation==repair.explanation
            &&IsWindowEnabled(control(1,20))!=FALSE,"A current dependent repair choice changed its authored guide or remained locked");
        privacy();f.reader->seek_seconds(600);const auto unreviewed=f.snapshot();
        check(unreviewed.segments[unreviewed.index].block==1&&!unreviewed.finished,"A reader preview bypassed an untouched actual repair choice");
        f.reader->toggle();wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==1;},
            "the actual repair reader waits for its prerequisite choice");
        std::cout<<"PHASE U12 repairs p"<<repair.page+1<<" authored guide, real pause/reopen and correct explanation\n";
        choose(1,repair.wrong,true);const auto wrong=feedback_parts(1,false,"✗ Incorrecto\n\n"+std::string(repair.guide));
        check(edit_text(control(1,4)).find(repair.explanation)==std::string::npos,"An incorrect repair choice exposed its success explanation");
        play_feedback(wrong,false,false);choose(1,repair.correct,true);
        const auto correct_choice=feedback_parts(1,true,"✓ Correcto\n\n"+std::string(repair.explanation));play_feedback(correct_choice,true,false);
        // Only feedback above is listened to in full. Preview the remaining
        // long task prose to its real pending-editor boundary without credit.
        pending_editor();
        std::cout<<"PHASE U12 repairs p"<<repair.page+1<<" literal initial program: real exit1, unavailable objective\n";
        compile(3,*repair.initial,1,"Lectura incompleta",repair.initial_data,false,true);
        const auto unavailable=f.snapshot();const auto regions=lesson_reading_regions(f.view);
        check(edit_text(control(3,4)).empty()&&!unavailable.finished&&!unavailable.playing&&!unavailable.allocated
            &&std::ranges::none_of(regions,[](const auto& region){return region.block_index==3&&region.feedback;})
            &&std::ranges::none_of(unavailable.segments,[](const auto& part){return part.block==3&&part.feedback;}),
            "A nonzero initial repair run invented a spoken verdict or successful objective");pending_editor();
        std::cout<<"PHASE U12 repairs p"<<repair.page+1<<" one exact repair and real bare Correcto\n";
        compile(3,*repair.fixed,0,"Total=12",repair.final_data,true,true);
        const auto correct=feedback_parts(3,true,"✓ Correcto");
        check(std::ranges::none_of(correct,[&](auto index){return f.snapshot().segments[index].feedback_fully_played;}),
            "A fresh correct repair inherited older playback credit");
        play_feedback(correct,true,true);const auto completed=f.snapshot();reopen(completed);
        const auto options=lesson_options_snapshot(f.view);
        check(std::ranges::find(options.available_pages,repair.page+1)!=options.available_pages.end(),
            "The actual accepted repair failed to unlock its posterior reference");
        if(repair.page==5){const auto history=narration::read_history(saved());const auto entry=std::ranges::find(history.entries,completed.key,&narration::Bookmark::key);
            check(entry!=history.entries.end()&&entry->position.finished,"The fully played mode repair has no actual finished bookmark");retained_mode_bookmark=*entry;
            check(std::ranges::find(options.available_pages,7)!=options.available_pages.end(),"The real mode receipt did not enable the dependent separator page");}
        check(windows::compiler_puzzle_completed(saved(),lesson,goal),"Reopening lost a real repair achievement");preserve();
    }
    check(lesson_page_state(f.view).index==7&&lesson_page_state(f.view).lesson_id==lesson.id&&directories().size()==5
        &&receipts.size()==3&&answers.size()==4,"The focal repairs changed lesson, fabricated work or ran more than five real programs");
    preserve();std::cout<<"PASS current U12L3 p6/p8 repair reader: actual p4/p5 prerequisites without listening credit; both authored wrong guides fully played, real paused silent reopening, correct explanations, required-editor/seek/reference gates, two literal exit1 runs without invented verdict/credit and two exact exit0 repairs, source-bound bare Correcto playback before navigation, five actual private runs total, preserved prior answers/receipts/drafts/WAV/files and real p6 bookmark. No Finalizar, U12L4 navigation, full lesson traversal, student authorship or human listening acceptance claimed.\n";
}
