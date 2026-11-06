#pragma once

// Five real prerequisite answers and three actual runs in one fresh hidden QA
// profile. The final run uses the authentic old feedback contract, then adopts
// the proposed explanation without rerunning or rewriting its success receipt.
void u12_composed_feedback_view(const std::filesystem::path& before_path,const std::filesystem::path& after_path){
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    require(state::sha256(before_bytes)=="ef77e1f7d47ab04a43690723c0bf18a97d52bb8ab7323c0df48f34997b807d87"
        &&state::sha256(after_bytes)=="72b6f46499f7b902559a51e4a5ee998710489774c49dd42a836e783ad85cd2f1",
        "The composed-feedback fixtures differ from the complete published fifteen-edge source or closed seventeen-edge proposal");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    constexpr std::size_t page=2,compiler=3,note=4;
    const auto& goal=after.pages.at(page).blocks.at(compiler);const auto& old_goal=before.pages.at(page).blocks.at(compiler);
    const auto& partial=after.pages.at(3).blocks.at(1).code;const auto& complete=after.pages.at(3).blocks.at(5).code;
    const std::string expected="Validos=2;Rechazados=1\nValidos=0;Rechazados=0\nValidos=5;Rechazados=3\n";
    const std::string partial_output="Validos=2;Rechazados=1\nValidos=0;Rechazados=0";
    const std::string explanation="La salida coincide con las tres líneas pedidas, en su orden y con un salto después de cada una. Comprueba también que tu función forma los campos desde sus parámetros, devuelve la cadena y deja la presentación a main; la coincidencia de la salida no certifica esa organización.";
    const std::array<std::string,2> guides{
        "La ejecución terminó sin mostrar texto. Completa la composición y las tres llamadas, guarda sus resultados y envíalos a la salida desde main.",
        "Esta salida contiene sólo dos líneas y no tiene salto final. Revisa la tercera llamada y los saltos solicitados para la batería completa."};
    require(after.id=="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"&&before.id==after.id
        &&after.pages.size()==5&&before.pages.size()==5&&goal.stable_id=="block_0002_0003"&&old_goal.stable_id==goal.stable_id
        &&goal.type==BlockType::cpp_compiler&&goal.initial_code==old_goal.initial_code&&goal.initial_code.size()==156
        &&goal.output_check&&goal.output_check->cases.size()==1&&goal.output_check->cases[0].standard_input.empty()
        &&goal.output_check->cases[0].outputs==std::vector<std::string>{expected}
        &&goal.output_check->cases[0].explanation==explanation&&goal.output_check->cases[0].review_feedback.size()==2
        &&old_goal.output_check&&old_goal.output_check->cases[0].explanation.empty()&&old_goal.output_check->cases[0].review_feedback.empty()
        &&after.compiler_puzzles_required&&after.reference_after.size()==1&&after.reference_after[0].activity==goal.stable_id
        &&after.reference_after[0].blocks==std::vector<std::string>{"block_0003_0000","block_0003_0001","block_0003_0002",
            "block_0003_0003","block_0003_0004","block_0003_0005"}
        &&after.activity_after.size()==1&&after.activity_after[0].activity=="block_0004_0001"
        &&after.activity_after[0].prerequisites==std::vector<std::string>{goal.stable_id}
        &&after.pages[4].blocks[1].question=="Supón que la apertura, la escritura y el cierre tienen éxito. ¿Qué texto aparece en la salida estándar de este programa?",
        "The complete feedback proposal changed its authored objective, independent references or exact hypotheses");
    for(std::size_t index=0;index<guides.size();++index){const auto& rule=goal.output_check->cases[0].review_feedback[index];
        require(rule.outputs==std::vector<std::string>{index==0?"":partial_output}&&rule.text==guides[index],
            "The proposed compiler guides no longer match their independently expected exact outputs");}
    Fixture f([&](state::StateData& data,const curriculum::Lesson& current){state::prepare_lesson_state(data,current);
        data.preferences["u12_composed_feedback_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
        FILETIME time{};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER ticks{};ticks.LowPart=time.dwLowDateTime;ticks.HighPart=time.dwHighDateTime;
        auto& finite=data.preferences["attempt_reserve_seconds_test"];finite["reserve_programming"]=3.;
        finite["recovery_interval_programming"]=std::int64_t{3600000};
        finite["updated_at_programming"]=static_cast<std::int64_t>(ticks.QuadPart/10000-11644473600000ULL);
    },[&](const curriculum::Lesson&){return after;},true);
    require(f.page()==0&&!IsWindowVisible(f.host),"The composed-feedback QA did not start at its complete hidden first page");
    const auto initial=f.saved();const auto opaque=initial.preferences.at("u12_composed_feedback_qa");
    const auto hearts=initial.preferences.at("attempt_reserve_seconds_test");
    const auto root=f.environment.paths.projects/L"cpp-runs";
    const auto bytes=[](const std::filesystem::path& path){const auto raw=state::read_bytes(path);
        return std::string(reinterpret_cast<const char*>(raw.data()),raw.size());};
    const auto directories=[&]{std::set<std::filesystem::path> result;if(std::filesystem::exists(root))
        for(const auto& item:std::filesystem::directory_iterator(root))if(item.is_directory())result.insert(item.path());return result;};
    struct Artifact{std::filesystem::path path;std::string hash;std::filesystem::file_time_type stamp;};
    std::vector<Artifact> artifacts;std::map<std::string,state::Response> answers;
    const auto preserve=[&]{const auto data=f.saved();
        require(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes&&!IsWindowVisible(f.host)
            &&data.preferences.at("u12_composed_feedback_qa")==opaque&&data.preferences.at("attempt_reserve_seconds_test")==hearts,
            "Compiler feedback changed a published fixture, unrelated preference, finite hearts or desktop visibility");
        for(const auto& p:after.pages)for(const auto& block:p.blocks){const auto response=state::get_response(data,after,block);const auto known=answers.find(block.stable_id);
            if(known==answers.end())require(!response,"Compiler feedback fabricated an independent curricular answer");
            else require(response&&response->raw_json==known->second.raw_json&&response->correct==known->second.correct
                &&response->answer==known->second.answer&&response->selected==known->second.selected,
                "Compiler feedback changed a prerequisite answer earned through its real control");}
        require(narration::read_history(data).entries.empty(),"A view-only compiler check fabricated listening history");
        for(const auto& file:artifacts)require(state::sha256(state::read_bytes(file.path))==file.hash
            &&std::filesystem::last_write_time(file.path)==file.stamp,"A later view action changed a previous actual run artifact");
        for(const auto& file:std::filesystem::recursive_directory_iterator(f.environment.paths.root))if(file.is_regular_file())
            require(file.path().extension()!=L".wav"&&file.path().extension()!=L".opus","The silent feedback view generated audio");
    };
    require(directories().empty(),"The fresh composed-feedback QA inherited a previous execution");preserve();
    const auto answer=[&](std::size_t block,std::optional<int> option,std::string_view text){const auto& current=f.lesson.pages[f.page()].blocks[block];
        require(f.text(block,4).empty(),"A pristine prerequisite exposed feedback before its actual submission");
        if(option){f.click(block,20+*option*2);require(f.text(block,4).empty(),"Selecting a prerequisite exposed feedback before Check");}
        else f.replace(block,2,text);f.click(block,3);
        const auto response=state::get_response(f.saved(),f.lesson,current);
        require(response&&response->correct&&f.text(block,4).starts_with("✓ Correcto"),"An authored prerequisite was not accepted through its actual controls");
        if(option)require(response->selected==*option,"A real prerequisite changed its selected option");
        else require(response->answer==text,"A real prerequisite changed its exact typed answer");
        answers[current.stable_id]=*response;preserve();
    };
    answer(4,2,{});f.next();answer(1,{},"Turno 2|Turno 2 confirmado");answer(3,{},"4:47");answer(4,0,{});answer(5,1,{});f.next();
    require(f.page()==page&&answers.size()==5,"Five actual prerequisite answers did not reach the composed-output construction");
    const auto draft_key=curriculum::draft_key("programming",after,goal);
    const auto request=[](const std::string& source){windows::CppRunRequest value;value.source_utf8=source;return value;};
    const auto gates=[&](bool complete_goal){const auto data=f.saved();const auto options=lesson_options_snapshot(f.view);const auto regions=lesson_reading_regions(f.view);
        require(f.page()==page&&windows::compiler_puzzle_completed(data,f.lesson,f.lesson.pages[page].blocks[compiler])==complete_goal
            &&windows::compiler_puzzle_case_count(data,f.lesson,f.lesson.pages[page].blocks[compiler])==(complete_goal?1u:0u)
            &&lesson_page_state(f.view).can_next==complete_goal,"The real composed-output objective acquired an incorrect navigation/receipt state");
        for(const auto destination:{3u,4u})require((std::ranges::find(options.available_pages,destination)!=options.available_pages.end())==complete_goal,
            "The composed-output objective exposed or failed to enable a posterior reference/dependent page");
        if(!complete_goal){require(!navigate_lesson_view(f.view,3)&&!navigate_lesson_view(f.view,4)&&f.page()==page
                &&!descendant(f.view,id(note,0))&&!descendant(f.view,id(note,19)),"An unresolved compiler exposed its note or posterior implementation");
            require(std::ranges::none_of(regions,[](const auto& region){return region.block_index==note;}),"An unresolved compiler narrated its post-editor note");}
        else{auto visible=f.lesson.pages[page].blocks[note].text;std::erase(visible,'`');
            require(f.text(note,0)==visible&&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==note
                &&region.target==f.control(note,0)&&region.source_text==f.lesson.pages[page].blocks[note].text;}),"The earned objective lost its complete note");}
        for(const auto& region:regions)require(region.target!=f.control(compiler,10)&&region.target!=f.control(compiler,14),
            "The feedback view exposed source or process console bytes as narrated curriculum");
    };
    const auto feedback=[&](bool correct,const std::string& body){const auto expected=std::string(correct?"✓ Correcto":"✗ Incorrecto")+(body.empty()?"":"\n\n"+body);
        auto visible=expected;std::erase(visible,'`');require(f.text(compiler,4)==visible,"The actual compiler feedback was incomplete or mixed another verdict/guide");
        const auto regions=lesson_reading_regions(f.view);const auto region=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==compiler&&item.feedback;});
        require(region!=regions.end()&&region->target==f.control(compiler,4)&&region->source_text==expected
            &&region->correct==correct&&region->solved==correct,"The compiler feedback has a different full reader source or state");
        if(!correct)require(visible.find(explanation)==std::string::npos,"A rejected output exposed the success explanation");
    };
    const auto execute=[&](const std::string& source,const std::string& raw_stdout,bool correct,const std::string& body){
        if(f.text(compiler,10)!=source){f.replace(compiler,10,source);require(f.text(compiler,4).empty(),
            "Editing a compiler source retained another attempt's feedback before a real run");}
        const auto old=directories();f.run(compiler);const auto current=directories();std::vector<std::filesystem::path> added;
        for(const auto& run:current)if(!old.contains(run))added.push_back(run);
        require(current.size()==old.size()+1&&added.size()==1&&added[0].parent_path()==root,
            "One composed-output action did not create exactly one private fresh execution");
        const auto run=added[0];require(bytes(run/L"main.cpp")==source&&bytes(run/L"stdin.bin").empty()
            &&bytes(run/L"execution-stdout.bin")==raw_stdout&&bytes(run/L"execution-stderr.bin").empty()
            &&std::filesystem::is_regular_file(run/L"program.exe")&&f.text(compiler,34)=="Ejecución terminada (código 0).",
            "The real composition run differs from its independently expected source, streams or exit zero");
        require(!std::filesystem::exists(run/L"resumen_lgp.txt"),"A pure composition run unexpectedly inherited or created the later file example");
        const auto tools=bytes(run/L"toolchain.txt");require(tools.find("MSVC=")!=tools.npos&&tools.find("SDK=")!=tools.npos,
            "The actual composed-output run lost its selected Windows toolchain evidence");
        if(!raw_stdout.empty()){auto visible=raw_stdout;std::erase(visible,'\r');
            require(f.text(compiler,30)=="Salida del programa (stdout)"&&f.text(compiler,14)==visible,
                "The composition console lost its complete process stdout or significant final newline");}
        feedback(correct,body);gates(correct);
        const auto& current_goal=f.lesson.pages[page].blocks[compiler];
        require(windows::compiler_puzzle_review(f.saved(),f.lesson,current_goal,request(source))==(correct?"":"Incorrecto\n"+body),
            "The exact real rejection review does not match its captured program and complete current guide");
        for(const auto& item:std::filesystem::directory_iterator(run))if(item.is_regular_file())
            artifacts.push_back({item.path(),state::sha256(state::read_bytes(item.path())),item.last_write_time()});
        std::cout<<"RUN U12L4 composed feedback directory="<<run.generic_string()<<" sourceSha256="<<state::sha256(source)
            <<" stdoutSha256="<<state::sha256(raw_stdout)<<" stdoutBytes="<<raw_stdout.size()
            <<" stdinBytes=0 stderrBytes=0 exit=0 correct="<<correct<<'\n';preserve();
    };
    const auto reopen=[&](bool correct,const std::string& body){const auto data=f.saved();const auto draft=state::get_draft(data,draft_key,goal.initial_code);
        const auto count=directories().size();state::Preferences retained;
        for(const auto& [key,value]:data.preferences.at("native_windows"))if(key.starts_with("compiler-puzzle/v1/")||key.starts_with("reference-proof/v1/"))retained.emplace(key,value);
        f.reopen();const auto restored=f.saved();const auto next=state::get_draft(restored,draft_key,goal.initial_code);
        require(f.page()==page&&f.text(compiler,10)==draft.source&&next.has_saved==draft.has_saved&&next.source==draft.source&&next.input==draft.input
            &&next.indentation_json==draft.indentation_json&&next.selection_start==draft.selection_start&&next.selection_end==draft.selection_end
            &&f.text(compiler,14).empty()&&directories().size()==count,"Reopening lost an actual attempt's editor/input/selection or reran a transient console");
        for(const auto& [key,value]:retained){const auto actual=state::find(restored,"native_windows",key);
            require(actual&&*actual==value,"Reopening changed a captured program, case, review seal or successful receipt");}
        feedback(correct,body);gates(correct);preserve();
    };
    gates(false);require(f.text(compiler,4).empty(),"A pristine compiler exposed a proposed guide before execution");
    execute(goal.initial_code,"",false,guides[0]);reopen(false,guides[0]);
    execute(partial,"Validos=2;Rechazados=1\r\nValidos=0;Rechazados=0",false,guides[1]);reopen(false,guides[1]);
    // Use the complete older publication for this one genuine success. Its
    // contract has the same byte outputs and source, without new feedback.
    // Remounting it is explicit QA; no student data or receipts are inserted.
    const auto two_runs=directories();f.close();f.lesson=before;f.open();
    require(f.page()==page&&f.text(compiler,10)==partial&&directories()==two_runs,
        "Mounting the authentic old feedback publication changed the current draft or ran a program");
    gates(false);preserve();
    execute(complete,"Validos=2;Rechazados=1\r\nValidos=0;Rechazados=0\r\nValidos=5;Rechazados=3\r\n",true,"");
    reopen(true,"");const auto historical=f.saved();const auto historical_draft=state::get_draft(historical,draft_key,goal.initial_code);
    state::Preferences proof;
    for(const auto& [key,value]:historical.preferences.at("native_windows"))if(key.starts_with("compiler-puzzle/v1/")||key.starts_with("reference-proof/v1/"))proof.emplace(key,value);
    const auto receipt_prefix="compiler-puzzle/v1/"+before.locale+'/'+before.id+'/'+curriculum::exercise_key(before,old_goal)+'/'
        +state::sha256(curriculum::reference_objective_frame(before,old_goal))+'/';
    for(const auto field:{"program","cases","completed","feedback"})require(proof.contains(receipt_prefix+field),
        "The actual historical success lacks its complete native captured-program/case/seal/feedback receipt");
    const auto stored_feedback=std::get_if<std::string>(&proof.at(receipt_prefix+"feedback"));
    require(stored_feedback&&*stored_feedback=="Correcto"&&directories().size()==3,
        "The authentic old success already contained a new explanation or did not use exactly three real executions");
    f.close();f.lesson=after;f.open();const auto adopted=f.saved();const auto adopted_draft=state::get_draft(adopted,draft_key,goal.initial_code);
    require(f.page()==page&&f.text(compiler,10)==complete&&directories().size()==3&&adopted_draft.has_saved==historical_draft.has_saved
        &&adopted_draft.source==historical_draft.source&&adopted_draft.input==historical_draft.input
        &&adopted_draft.indentation_json==historical_draft.indentation_json&&adopted_draft.selection_start==historical_draft.selection_start
        &&adopted_draft.selection_end==historical_draft.selection_end,
        "Adopting the new explanation changed the real successful draft or executed another program");
    const auto immutable_receipt=[&]{const auto data=f.saved();for(const auto& [key,value]:proof){const auto actual=state::find(data,"native_windows",key);
        require(actual&&*actual==value,"Feedback adoption rewrote the original real execution receipt or reference seal");}};
    immutable_receipt();feedback(true,explanation);gates(true);preserve();reopen(true,explanation);immutable_receipt();
    f.select_page(3);const auto references=lesson_reading_regions(f.view);
    for(std::size_t block=0;block<after.pages[3].blocks.size();++block){const auto& source=after.pages[3].blocks[block];
        if(source.type==BlockType::code)require(f.text(block,1)==source.code&&(GetWindowLongPtrW(f.control(block,1),GWL_STYLE)&ES_READONLY)
            &&std::ranges::any_of(references,[&](const auto& region){return region.block_index==block&&region.passive_label
                &&region.target==f.control(block,16)&&region.source_text=="Ejemplo de código C más más.";}),
            "An earned composition reference lost its exact read-only complete program");
        else{auto visible=source.text;std::erase(visible,'`');require(f.text(block,0)==visible
            &&std::ranges::any_of(references,[&](const auto& region){return region.block_index==block
                &&region.target==f.control(block,0)&&region.source_text==source.text;}),"An earned composition reference lost its exact complete prose");}
    }
    immutable_receipt();preserve();f.select_page(4);
    const auto& prediction=after.pages[4].blocks[1];const auto regions=lesson_reading_regions(f.view);
    require(f.text(1,0)==prediction.question&&IsWindowEnabled(f.control(1,2))&&!IsWindowEnabled(f.control(1,3))
        &&f.text(1,4).empty()&&!state::get_response(f.saved(),after,prediction)
        &&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==1&&region.is_exercise&&!region.solved
            &&region.target==f.control(1,0)&&region.source_text==prediction.question;}),
        "The earned objective failed to enable the complete new dependent hypothesis without answering it");
    immutable_receipt();preserve();f.select_page(page);feedback(true,explanation);gates(true);reopen(true,explanation);immutable_receipt();
    require(directories().size()==3&&answers.size()==5,"The focal feedback view ran extra programs or fabricated curricular work");preserve();
    std::cout<<"PASS U12L4 compiler feedback view: five prerequisites answered through actual native controls; initial empty and two-line "
        <<"real exit-zero rejections show only their complete current guides, retain source/review seal through reopening, and keep note/references/dependent question gated; "
        <<"third real three-line success earned under authentic before15, then after17 displays its full current explanation without another run or receipt rewrite; "
        <<"six complete reference blocks and new unanswered dependent hypothesis available only after success; five answers, exact draft/selection, finite hearts, "
        <<"all run artifacts and immutable sources preserved; no audio/hearing, Finish, independent student authorship or course completion claimed\n";
}
