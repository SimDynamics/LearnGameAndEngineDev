#pragma once
// Complete proposed lesson; prerequisite answers and compiler results use real
// native controls. The author's reference is explicit QA input, not student work.
void u12_composed_feedback_reader(const std::shared_ptr<windows::NarrationWorker>& backend,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path) {
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="ef77e1f7d47ab04a43690723c0bf18a97d52bb8ab7323c0df48f34997b807d87"
        &&state::sha256(after_bytes)=="72b6f46499f7b902559a51e4a5ee998710489774c49dd42a836e783ad85cd2f1",
        "The composed-feedback reader lost its exact complete published/proposed documents");
    const auto lesson=windows::load_lesson(after_path);const auto& goal=lesson.pages.at(2).blocks.at(3);
    const auto& expected=goal.output_check->cases.at(0);
    const std::string complete="Validos=2;Rechazados=1\nValidos=0;Rechazados=0\nValidos=5;Rechazados=3\n";
    const std::string partial="Validos=2;Rechazados=1\nValidos=0;Rechazados=0";
    check(goal.stable_id=="block_0002_0003"&&expected.outputs==std::vector<std::string>{complete}
        &&expected.standard_input.empty()&&expected.review_feedback.size()==2&&!expected.explanation.empty()
        &&expected.review_feedback[0].outputs==std::vector<std::string>{""}
        &&expected.review_feedback[1].outputs==std::vector<std::string>{partial}
        &&lesson.compiler_puzzles_required&&lesson.reference_after.size()==1&&lesson.activity_after.size()==1,
        "The proposed composition changed its literal output or selective prerequisite contract");
    const auto cache=std::filesystem::path(build::source_root)/"out/qa/u12-composed-feedback-reader"/
        ("pcm-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64()));
    Fixture f(backend,false,true,false,"es",lesson,true,0,true,false,[](HWND view){
        update_lesson_preferences(view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=3.;});
    },[](state::StateData& data){data.preferences["u12_composed_feedback_reader_qa"]["opaque"]=std::string("\t conservar\r\n保存");},cache);
    f.reader.reset();
    struct FailureShutdown{std::shared_ptr<windows::NarrationWorker> worker;
        ~FailureShutdown(){if(std::uncaught_exceptions())worker->shutdown();}} shutdown{backend};
    const auto saved=[&]{return read_lesson_state(f.view);};
    const auto control=[&](std::size_t block,int part){return child(f.view,1000+static_cast<int>(block)*100+part);};
    const auto opaque=saved().preferences.at("u12_composed_feedback_reader_qa");
    std::map<std::string,state::Response> answers;
    const auto remember=[&](std::size_t block){const auto& current=lesson.pages.at(lesson_page_state(f.view).index).blocks.at(block);
        const auto answer=state::get_response(saved(),lesson,current);check(answer&&answer->correct,"A real composition prerequisite was not accepted");
        answers[current.stable_id]=*answer;};
    const auto choose=[&](std::size_t block,int option){SendMessageW(control(block,20+option*2),BM_CLICK,0,0);
        SendMessageW(control(block,3),BM_CLICK,0,0);remember(block);};
    const auto predict=[&](std::size_t block,const char* answer){const auto text=wide(answer);SendMessageW(control(block,2),EM_SETSEL,0,-1);
        SendMessageW(control(block,2),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));SendMessageW(control(block,3),BM_CLICK,0,0);remember(block);};
    choose(4,2);check(navigate_lesson_view(f.view,1),"The first actual composition choice did not allow p2");
    predict(1,"Turno 2|Turno 2 confirmado");predict(3,"4:47");choose(4,0);choose(5,1);
    check(answers.size()==5&&navigate_lesson_view(f.view,2),"Five real prerequisites did not permit the composition editor");
    check(narration::read_history(saved()).entries.empty(),"Detached prerequisite preparation invented a listening bookmark");
    const auto root=f.environment.paths.projects/"cpp-runs";
    const auto directories=[&]{std::set<std::filesystem::path> result;if(std::filesystem::exists(root))
        for(const auto& item:std::filesystem::directory_iterator(root))if(item.is_directory())result.insert(item.path());return result;};
    const auto bytes=[](const std::filesystem::path& path){const auto raw=state::read_bytes(path);
        return std::string(reinterpret_cast<const char*>(raw.data()),raw.size());};
    check(directories().empty(),"The fresh composition reader inherited a program execution");
    struct Retained{std::filesystem::path path;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Retained> retained;
    const auto retain=[&](const std::filesystem::path& path){retained.push_back({path,state::sha256(state::read_bytes(path)),std::filesystem::last_write_time(path)});};
    const auto preserve=[&]{const auto data=saved();check(data.preferences.at("u12_composed_feedback_reader_qa")==opaque
            &&state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host),
            "Composition feedback changed unrelated data, the source documents or desktop visibility");
        for(const auto& page:lesson.pages)for(const auto& current:page.blocks){const auto response=state::get_response(data,lesson,current);
            const auto found=answers.find(current.stable_id);check(found==answers.end()?!response:response&&response->raw_json==found->second.raw_json,
                "Composition feedback changed a prerequisite answer or invented an independent response");}
        for(const auto& file:retained)check(state::sha256(state::read_bytes(file.path))==file.hash&&std::filesystem::last_write_time(file.path)==file.stamp,
            "A later compilation or reopening changed an earlier source, stream or recording");};
    const auto ready=[&]{wait([&]{const auto value=f.snapshot();return value.planned&&!value.segments.empty()
        &&std::ranges::all_of(value.segments,[](const auto& part){return part.available;});},"full proposed composition synthesis/alignment",120000);
        check(f.snapshot().rate==3.,"Composition feedback lost the selected 3x rate");};
    const auto hooks=[&]{LessonViewHooks value;value.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};
        value.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        value.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};return value;};
    const auto mount=[&]{update_lesson_preferences(f.view,[](state::StateData& data){data.preferences["lesson_narration_settings"]["kokoro_advance_generation"]=true;});
        set_lesson_view_hooks(f.view,hooks());f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});ready();};
    mount();f.reader->pause();
    const auto pending=[&]{const auto regions=lesson_reading_regions(f.view);const auto snapshot=f.snapshot();
        check(!windows::compiler_puzzle_completed(saved(),lesson,goal)&&windows::compiler_puzzle_case_count(saved(),lesson,goal)==0
            &&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,3)&&!navigate_lesson_view(f.view,4),
            "An uncompleted composition opened its reference or dependent prediction");
        check(std::ranges::any_of(regions,[](const auto& region){return region.block_index==3&&region.required_compiler&&!region.solved;})
            &&std::ranges::none_of(regions,[](const auto& region){return region.block_index!=std::numeric_limits<std::size_t>::max()&&region.block_index>3;})
            &&std::ranges::none_of(snapshot.segments,[](const auto& part){return part.block!=std::numeric_limits<std::size_t>::max()&&part.block>3;}),
            "Pending composition narration exposed its following note");
        for(const auto& region:regions)check(region.target!=control(3,10)&&region.target!=control(3,14),"The reader exposed compiler source or stdout");};
    check(edit_text(control(3,4)).empty(),"An untouched composition editor exposed feedback");pending();
    const auto reopen=[&]{f.reader.reset();check(flush_lesson_view(f.view),"Cannot persist the real composition feedback");
        const auto durable=narration::read_history(saved());DestroyWindow(f.view);f.view=nullptr;
        f.view=create_lesson_view(f.host,f.environment,lesson,hooks());check(f.view!=nullptr,"Cannot reopen the complete proposed composition");
        MoveWindow(f.view,0,0,980,720,TRUE);mount();return durable;};
    const auto execute=[&](const std::string& source,const std::string& output,const std::string& feedback,bool correct){
        // MSVC's unmodified stdout text mode writes CRLF. Assert those raw
        // bytes separately from the authored LF-only educational objective.
        std::string native_output;for(const auto character:output){if(character=='\n')native_output+='\r';native_output+=character;}
        f.reader->pause();if(edit_text(control(3,10))!=source){const auto value=wide(source);SendMessageW(control(3,10),EM_SETSEL,0,-1);
            SendMessageW(control(3,10),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));}
        const auto previous=directories();SendMessageW(control(3,11),BM_CLICK,0,0);
        wait([&]{return IsWindowEnabled(control(3,11))!=FALSE;},"actual fresh composition compile/execute/check",120000);
        f.reader->pause();ready();const auto current=directories();std::vector<std::filesystem::path> added;
        for(const auto& item:current)if(!previous.contains(item))added.push_back(item);
        check(current.size()==previous.size()+1&&added.size()==1,"One composition action did not create exactly one fresh execution");
        const auto run=added.front();check(bytes(run/"main.cpp")==source&&bytes(run/"stdin.bin").empty()
            &&bytes(run/"execution-stdout.bin")==native_output&&bytes(run/"execution-stderr.bin").empty()
            &&std::filesystem::is_regular_file(run/"program.exe")&&edit_text(control(3,34))=="Ejecución terminada (código 0).",
            "A composition execution changed its real source, stdin, raw channels or exit status");
        for(const auto& item:std::filesystem::directory_iterator(run))if(item.is_regular_file())retain(item.path());
        check(edit_text(control(3,4))==feedback&&windows::compiler_puzzle_completed(saved(),lesson,goal)==correct
            &&windows::compiler_puzzle_case_count(saved(),lesson,goal)==(correct?1u:0u),"The actual composition verdict/guide or receipt is wrong");
        if(!correct)pending();
        const auto regions=lesson_reading_regions(f.view);const auto region=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==3&&item.feedback;});
        check(region!=regions.end()&&region->source_text==feedback&&region->correct==correct,"The proposed guide/explanation is not the complete spoken source");
        const auto plan=backend->plan(feedback,"es",{},region->reading_context,region->math_mode);const auto source_text=wide(feedback);
        std::vector<std::size_t> indices;std::vector<bool> covered(source_text.size());const auto prepared=f.snapshot();
        for(std::size_t index=0;index<prepared.segments.size();++index){const auto& part=prepared.segments[index];if(part.region!=region->key)continue;
            const auto number=indices.size();check(number<plan.size()&&part.source==plan[number].text&&part.spoken==plan[number].spoken
                &&part.start==plan[number].start&&part.end==plan[number].end&&part.start<part.end&&part.end<=source_text.size()
                &&part.source==utf8(source_text.substr(part.start,part.end-part.start)),"Composition feedback lost full contextual UTF-16 source ownership");
            for(auto at=part.start;at<part.end;++at)covered[at]=true;indices.push_back(index);retain(part.recording);
            std::cout<<"AUDIO U12 composition correct="<<correct<<" part="<<number+1<<" sha256="<<verify_feedback_recording(part)<<" path="<<part.recording.generic_string()<<'\n';}
        check(!indices.empty()&&indices.size()==plan.size()&&prepared.index==indices.front()&&prepared.offset==0&&!prepared.playing&&!prepared.allocated,
            "Fresh composition feedback did not stay completely prepared at an explicit paused zero start");
        for(std::size_t at=0;at<source_text.size();++at)check(iswspace(source_text[at])||covered[at],"A composition guide/explanation was truncated");
        std::set<std::size_t> heard;f.reader->toggle();wait([&]{const auto value=f.snapshot();if(value.playing){check(value.playback_rate==3.,"Composition feedback did not play at actual 3x");
                if(std::ranges::find(indices,value.index)!=indices.end())heard.insert(value.index);}
            return !value.playing&&!value.allocated&&!value.preparing&&(correct?value.finished:value.index==indices.back()
                &&value.offset+1>=value.segments[value.index].duration_ms);
        },correct?"complete success explanation and following composition note":"complete composition rejection and retry boundary",120000);
        const auto ended=f.snapshot();check(std::ranges::all_of(indices,[&](auto index){return heard.contains(index)&&ended.segments[index].feedback_fully_played;})
            &&ended.finished==correct,"Playback skipped part of the proposed compiler feedback or completed an unresolved objective");
        preserve();const auto durable=reopen();const auto silent=f.snapshot();check(silent.key==ended.key&&silent.index==ended.index&&silent.offset==ended.offset
            &&silent.heard_through==ended.heard_through&&silent.finished==ended.finished&&!silent.playing&&!silent.allocated
            &&narration::read_history(saved())==durable&&edit_text(control(3,4))==feedback&&directories()==current,
            "Reopening changed actual composition feedback, listening position or execution evidence");
        if(!correct)pending();preserve();
        std::cout<<"RUN U12 composition correct="<<correct<<" sourceSha256="<<state::sha256(source)<<" stdoutSha256="<<state::sha256(native_output)<<" path="<<run.generic_string()<<'\n';
    };
    execute(goal.initial_code,"","✗ Incorrecto\n\n"+expected.review_feedback[0].text,false);
    execute(lesson.pages[3].blocks[1].code,partial,"✗ Incorrecto\n\n"+expected.review_feedback[1].text,false);
    execute(lesson.pages[3].blocks[5].code,complete,"✓ Correcto\n\n"+expected.explanation,true);
    check(directories().size()==3&&lesson_page_state(f.view).can_next,"The three actual composition runs did not retain the correct advancement boundary");
    preserve();std::cout<<"PASS U12L4 proposed compiler feedback: five actual prior answers, three fresh literal executions, complete new guides/explanation with PCM/alignment/UTF-16 and explicit 3x playback, retry/success gates and exact silent reopening, immutable originals and no human listening/student authorship/whole-course completion claim\n";
}
