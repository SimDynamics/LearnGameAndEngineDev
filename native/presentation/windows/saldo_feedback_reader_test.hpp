#pragma once
// The target activity and its heading are copied from the active curriculum.
// Playback, answers and bookmarks belong only to a new hidden QA view.
namespace saldo_feedback_reader_test {
const std::filesystem::path published_asset=std::filesystem::path(build::source_root)/
    "app/src/main/assets/content/es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json";
void published_page(const std::shared_ptr<windows::NarrationWorker>& backend,double rate) {
    const auto original=state::read_bytes(published_asset);
    const auto root=std::filesystem::path(L"C:/LGAED-QA")/(L"saldo-published-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content";
    const auto copy=root/L"content"/published_asset.lexically_relative(content);std::filesystem::create_directories(copy.parent_path());
    {std::ofstream stream(copy,std::ios::binary);stream.write(reinterpret_cast<const char*>(original.data()),static_cast<std::streamsize>(original.size()));
        check(static_cast<bool>(stream),"Cannot write the literal isolated curriculum copy");}
    // Course-edition loading authenticates the physical language/course/unit
    // route and its active indices. Copy those exact descriptors alongside the
    // exact lesson; neither relax the loader nor strip the publication metadata.
    const auto course=published_asset.parent_path().parent_path()/L"course_cpp_fundamentals.json";
    const auto unit=published_asset.parent_path()/(L"unit_"+published_asset.parent_path().filename().native()+L".json");
    for(const auto& index:{course,unit}){
        const auto destination=root/L"content"/index.lexically_relative(content);
        std::filesystem::create_directories(destination.parent_path());std::filesystem::copy_file(index,destination);
    }
    const auto lesson=windows::load_lesson(copy);const auto& page=lesson.pages.at(1);
    const auto goal=std::ranges::find_if(page.blocks,[](const auto& block){return block.type==curriculum::BlockType::fill_code&&block.answers==std::vector<std::string>{"saldo"};});
    check(goal!=page.blocks.end()&&goal+1<page.blocks.end()&&(goal+1)->text=="Usar el valor después del bloque","Published saldo source changed");
    const auto saldo=static_cast<std::size_t>(goal-page.blocks.begin());
    // This prefix only admits page two in the isolated profile. Current-page
    // answers and every listening certificate are earned below by real actions.
    const auto prefix=[&](state::StateData& data){
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        for(const auto& block:lesson.pages.front().blocks){if(block.type!=curriculum::BlockType::multiple_choice)continue;
            check(block.correct_index&&curriculum::evaluate_choice(block,*block.correct_index).correct,"Invalid preceding-page choice");
            state::Response response;response.selected=static_cast<std::int64_t>(*block.correct_index);response.answer=block.options[*block.correct_index];response.correct=true;
            state::mark_response_reviewed(response,block);state::put_response(data,lesson,block,response);}
        check(state::mark_page_reached(data,lesson,0),"Cannot visit the isolated preceding page");state::save_current_page(data,lesson,1);
    };
    Fixture f(backend,false,true,true,"es",lesson,true,0,true,false,[&](HWND view){
        update_lesson_preferences(view,[&](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=rate;});
    },prefix,root/L"audio",true);
    f.reader->set_qa_playback_muted(true);
    wait([&]{const auto value=f.snapshot();return lesson_page_state(f.view).index==1&&value.planned&&!value.page_audio_revision.empty();},"complete authentic page plan",180000);
    wait([&]{return f.snapshot().page_audio_complete;},"fully generated authentic page before its real answers",900000);
    std::size_t answered=0;bool math_answered=false;
    const auto submit=[&](std::size_t index){const auto& block=page.blocks[index];
        if(block.type==curriculum::BlockType::fill_code||block.type==curriculum::BlockType::output_prediction){
            const auto edit=child(f.view,1002+static_cast<int>(index)*100);const auto text=wide(block.answers.at(0));CHARRANGE all{0,-1};
            SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
        }else if(block.type==curriculum::BlockType::multiple_choice){
            check(block.correct_index.has_value(),"Published choice lost its answer");const auto toggle=child(f.view,1020+static_cast<int>(index)*100+static_cast<int>(*block.correct_index)*2);
            SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(toggle),BN_CLICKED),reinterpret_cast<LPARAM>(toggle));
        }else{
            check(block.type==curriculum::BlockType::math_puzzle,"Unexpected activity before saldo");
            using namespace winrt::Windows::Data::Json;const auto specification=JsonObject::Parse(wide(block.raw_json));
            const auto response=specification.GetNamedArray(L"responses").GetObjectAt(0);const auto options=response.GetNamedArray(L"options");
            const auto expected=response.GetNamedObject(L"validator").GetNamedArray(L"expected").GetStringAt(0);std::size_t option=options.Size();
            for(unsigned n=0;n<options.Size();++n)if(options.GetObjectAt(n).GetNamedString(L"id")==expected){option=n;break;}
            std::vector<HWND> radios;EnumChildWindows(child(f.view,1005+static_cast<int>(index)*100),[](HWND item,LPARAM data)->BOOL{
                wchar_t name[32]{};GetClassNameW(item,name,32);if(std::wstring_view(name)==L"Button"&&(GetWindowLongPtrW(item,GWL_STYLE)&BS_TYPEMASK)==BS_RADIOBUTTON)
                    reinterpret_cast<std::vector<HWND>*>(data)->push_back(item);return TRUE;},reinterpret_cast<LPARAM>(&radios));
            std::ranges::sort(radios,{},[](HWND item){return GetDlgCtrlID(item);});check(radios.size()==options.Size()&&option<radios.size(),"Cannot identify the real math choice radios");
            const auto toggle=radios[option];SendMessageW(GetParent(toggle),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(toggle),BN_CLICKED),reinterpret_cast<LPARAM>(toggle));math_answered=true;
        }
        const auto action=child(f.view,1003+static_cast<int>(index)*100);SendMessageW(action,BM_CLICK,0,0);
    };
    for(std::size_t index=0;index<=saldo;++index){const auto type=page.blocks[index].type;
        if(type!=curriculum::BlockType::fill_code&&type!=curriculum::BlockType::output_prediction&&type!=curriculum::BlockType::multiple_choice&&type!=curriculum::BlockType::math_puzzle)continue;
        f.reader->pause();submit(index);
        wait([&]{const auto value=f.snapshot();return value.planned&&value.segments[value.index].block==index&&value.segments[value.index].feedback&&value.segments[value.index].correct;},"real published correct answer",180000);
        f.reader->toggle();bool actually_played=false;
        wait([&]{const auto value=f.snapshot();if(value.playing&&value.segments[value.index].block==index&&value.segments[value.index].feedback)actually_played=true;
            if(!actually_played||!value.playing||value.segments[value.index].block<=index)return false;
            for(const auto& part:value.segments)if(part.block==index&&part.feedback&&part.correct)
                check(part.feedback_fully_played,"Published feedback continued without hearing every actual PCM sentence");
            check(value.playback_rate==rate,"Published feedback continuation changed speed");return true;
        },"published correct feedback automatically enters its next actual content",240000);
        f.reader->pause();const auto value=f.snapshot();
        for(const auto& part:value.segments)if(part.block==index&&part.feedback&&part.correct)(void)verify_feedback_recording(part);
        ++answered;
        std::cout<<"SALDO_PUBLISHED_FEEDBACK rate="<<rate<<" block="<<index<<" next_block="<<value.segments[value.index].block<<" heard_through="<<value.heard_through<<'\n';
        if(index==saldo)check(value.segments[value.index].block==saldo+1&&value.segments[value.index].source=="Usar el valor después del bloque",
            "Published saldo feedback failed to play its immediate authentic heading");
    }
    check(math_answered&&answered>=6&&state::read_bytes(published_asset)==original&&state::read_bytes(copy)==original,
        "The authentic regression skipped a prerequisite/math or altered curriculum");
    auto paused=f.snapshot();const auto saved=read_lesson_state(f.view);
    for(const bool manual_pause:{false,true}){
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush authentic QA state");
        const auto before=narration::read_history(read_lesson_state(f.view));DestroyWindow(f.view);f.view=create_lesson_view(f.host,f.environment,windows::load_lesson(copy));
        check(f.view!=nullptr,"Cannot reopen the exact authentic QA publication");MoveWindow(f.view,0,0,980,720,TRUE);
        struct RestoreGate{std::mutex mutex;std::condition_variable_any changed;bool allow=false;std::atomic<bool> evaluated=false;};
        const auto gate=std::make_shared<RestoreGate>();LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        hooks.validate_math=[backend,gate](std::string_view block,std::string_view answers,std::string_view locale,std::stop_token stop){
            auto reply=backend->validate_math(block,answers,locale,stop);gate->evaluated.store(true);
            std::unique_lock lock(gate->mutex);if(!gate->changed.wait(lock,stop,[&]{return gate->allow;}))throw std::runtime_error("QA restoration cancelled");
            return reply;
        };
        set_lesson_view_hooks(f.view,std::move(hooks));f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.reader->set_qa_playback_muted(true);
        wait([&]{return gate->evaluated.load()&&f.snapshot().waiting_verification;},"real math reply held before restoration",180000);
        f.reader->toggle();const auto requested=f.snapshot();
        check(requested.waiting_verification&&!requested.playing&&!requested.allocated&&narration::read_history(read_lesson_state(f.view))==before,
            "Play during internal verification moved the authentic bookmark or started an incomplete script");
        if(manual_pause)f.reader->pause();
        {const std::lock_guard lock(gate->mutex);gate->allow=true;}gate->changed.notify_all();
        wait([&]{const auto value=f.snapshot();return !value.waiting_verification&&value.planned&&value.key==paused.key&&value.index==paused.index&&
            value.offset>=paused.offset&&value.segments[value.index].available&&(manual_pause||value.playing);},
            "deferred Play resumes the exact authentic position after real math verification",180000);
        const auto restored=f.snapshot();check(restored.heard_through==paused.heard_through,
            "Internal verification discarded earlier real feedback hearing");
        if(manual_pause)check(!restored.playing&&!restored.allocated&&restored.offset==paused.offset,
            "Internal verification overrode a voluntary pause");
        f.reader->pause();paused=f.snapshot();
        std::cout<<"SALDO_PUBLISHED_RESTORE rate="<<rate<<" play_while_verifying=true manual_pause="<<manual_pause<<" resumed="<<!manual_pause<<'\n';
    }
    for(const auto& block:page.blocks)if(const auto before=state::get_response(saved,lesson,block);before&&before->correct){
        const auto after=state::get_response(read_lesson_state(f.view),lesson,block);
        check(after&&after->correct&&after->answer==before->answer&&after->raw_json==before->raw_json&&after->selected==before->selected,
            "Reopening authentic page changed a correctly checked answer");}
    f.reader->toggle();wait([&]{const auto value=f.snapshot();check(!value.planned||value.index>=paused.index,"Reopening repeated previously heard feedback before saldo's heading");
        return value.waiting_gate&&!value.playing;},"reopened authentic heading/prose reaches its next legitimate pending exercise",180000);f.reader->pause();
    std::cout<<"PASS complete published saldo page "<<rate<<"X: exact QA source/IDs/semantics, "<<answered<<" real answers including math, non-silent Kokoro PCM, instance-muted Windows playback and automatic immediate heading; root="<<root.string()<<'\n';
}
void run(const std::shared_ptr<windows::NarrationWorker>& backend,double rate) {
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json";
    const auto original=state::read_bytes(asset);const auto published=windows::load_lesson(asset);
    const auto& source=published.pages.at(1).blocks;
    const auto goal=std::ranges::find_if(source,[](const auto& block){return block.type==curriculum::BlockType::fill_code&&block.answers==std::vector<std::string>{"saldo"};});
    check(goal!=source.end()&&goal+2<source.end()&&(goal+1)->text=="Usar el valor después del bloque",
        "The reported saldo activity or its next heading changed");
    auto lesson=model();lesson.id="qa_saldo_feedback_continuity";lesson.title="Continuidad del lector";lesson.pages.resize(1);
    auto& blocks=lesson.pages.front().blocks;blocks.front().text="La lectura comienza antes del ejercicio.";
    auto first=*goal;first.id=first.stable_id="previous";first.question="Escribe inicio.";first.code.clear();first.answers={"inicio"};
    first.explanation="Esta explicación debe terminar antes de continuar.";first.text_semantics.clear();first.address={0,1,{},{}};blocks.push_back(std::move(first));
    auto target=*goal;target.id=target.stable_id="saldo";target.address={0,2,{},{}};blocks.push_back(std::move(target));
    for(std::size_t n=1;n<=2;++n){auto following=*(goal+n);following.id=following.stable_id="after_saldo_"+std::to_string(n);
        following.address={0,blocks.size(),{},{}};blocks.push_back(std::move(following));}
    auto later=*goal;later.id=later.stable_id="pending_after_saldo";later.question="Escribe siguiente.";later.code.clear();later.answers={"siguiente"};
    later.explanation="El ejercicio posterior conserva su propio bloqueo.";later.text_semantics.clear();later.address={0,blocks.size(),{},{}};blocks.push_back(std::move(later));
    const auto cache=std::filesystem::path(L"C:/LGAED-QA")/(L"saldo-feedback-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Fixture f(backend,false,true,true,"es",lesson,true,0,true,true,[&](HWND view){
        update_lesson_preferences(view,[&](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=rate;});
    },{},cache);
    f.reader->set_qa_playback_muted(true);
    const auto submit=[&](std::size_t block,std::string_view answer){const auto edit=child(f.view,1002+static_cast<int>(block)*100);
        const auto text=wide(answer);CHARRANGE all{0,-1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
        SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));SendMessageW(child(f.view,1003+static_cast<int>(block)*100),BM_CLICK,0,0);};
    f.reader->toggle();
    wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&value.segments[value.index].block==1;},"real initial unanswered gate");
    submit(1,"inicio");
    wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&value.segments[value.index].block==2;},"correct first feedback automatically reaches saldo");
    submit(2,"saldo");bool heading_played=false;std::vector<std::size_t> feedback;
    wait([&]{const auto value=f.snapshot();if(value.planned)for(std::size_t n=0;n<value.segments.size();++n)
            if(value.segments[n].block==2&&value.segments[n].feedback&&value.segments[n].correct&&std::ranges::find(feedback,n)==feedback.end())feedback.push_back(n);
        if(value.playing&&value.segments[value.index].block==3){heading_played=true;check(value.playback_rate==rate,"The following heading changed speed");}
        return value.waiting_gate&&!value.playing&&value.segments[value.index].block==5;
    },"actual saldo feedback, next heading/prose and later pending prompt",120000);
    const auto stopped=f.snapshot();check(heading_played&&!feedback.empty()&&!stopped.finished&&!stopped.allocated,
        "Correct saldo feedback stopped before its heading or bypassed the later gate");
    for(const auto n:feedback){check(stopped.segments[n].feedback_fully_played,"Unheard saldo feedback released the heading");(void)verify_feedback_recording(stopped.segments[n]);}
    const auto saved=state::get_response(read_lesson_state(f.view),lesson,blocks[2]);check(saved&&saved->correct&&saved->answer=="saldo","Saldo answer was not preserved");
    f.reader->pause();submit(5,"siguiente");f.ready();
    check(!f.snapshot().playing&&!f.snapshot().allocated&&f.snapshot().segments[f.snapshot().index].feedback,"A correct answer overrode the voluntary pause");
    const auto paused=f.snapshot();f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.reader->set_qa_playback_muted(true);f.ready();const auto reopened=f.snapshot();
    check(reopened.index==paused.index&&reopened.offset==paused.offset&&reopened.key==paused.key&&!reopened.playing&&!reopened.allocated,
        "Reopening moved or started the voluntary pause");
    f.reader->toggle();wait([&]{return f.snapshot().finished;},"explicit resume finishes the final activity");
    check(state::read_bytes(asset)==original,"The regression changed actual curricular content");
    std::cout<<"PASS saldo feedback "<<rate<<"X: real non-silent Kokoro PCM with instance-muted MediaPlayer, automatic next heading/prose, pending gate, voluntary pause and exact reopen; hidden QA cache="<<cache.string()<<'\n';
}
}
