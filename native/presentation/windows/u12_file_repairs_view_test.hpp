#pragma once

// Included after Fixture. This starts at an explicit QA position, not at a
// claimed completed student journey. Every answer and receipt is earned through
// the native controls; the first editor is a real prerequisite for both repairs.
void current_u12_file_repairs_view(){
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_cerrar_reemplazar_y_anadir_datos.json";
    const auto original=state::read_bytes(asset);
    std::string normalized(reinterpret_cast<const char*>(original.data()),original.size());std::erase(normalized,'\r');
    require(state::sha256(normalized)=="c74bb6b67eb7fa3f9d08b376324fd82f4a2ba6ef9e0e6e02d4c89d6b27e6de21",
        "The current U12L3 repair asset differs from its audited LF source");
    const auto lesson=windows::load_lesson(asset);
    constexpr std::size_t first_page=3,mode_page=5,separator_page=7,editor_index=3;
    const auto& first=lesson.pages.at(first_page).blocks.at(editor_index);
    const auto& mode=lesson.pages.at(mode_page).blocks.at(editor_index);
    const auto& separator=lesson.pages.at(separator_page).blocks.at(editor_index);
    const auto& first_choice=lesson.pages.at(first_page).blocks.at(2);
    const auto& reference_choice=lesson.pages.at(4).blocks.at(3);
    const auto& mode_choice=lesson.pages.at(mode_page).blocks.at(1);
    const auto& separator_choice=lesson.pages.at(separator_page).blocks.at(1);
    const auto reference=lesson.pages.at(4).blocks.at(1).code;
    const auto layout=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(lesson.state_layout.raw_json));
    require(lesson.id=="cpp_fundamentals__archivos_y_programa_completo__cerrar_reemplazar_y_anadir_datos"
        &&lesson.pages.size()==9&&lesson.state_layout.signatures.size()==33&&layout.GetNamedArray(L"editorial").Size()==16
        &&lesson.compiler_puzzles_required&&lesson.reference_after.size()==3&&lesson.activity_after.size()==2
        &&first.stable_id=="block_0003_0003"&&mode.stable_id=="block_0005_0003"&&separator.stable_id=="block_0007_0003"
        &&first.type==BlockType::cpp_compiler&&mode.type==BlockType::cpp_compiler&&separator.type==BlockType::cpp_compiler
        &&first_choice.correct_index==2&&reference_choice.correct_index==0&&mode_choice.correct_index==0&&separator_choice.correct_index==1
        &&state::sha256(first.initial_code)=="6cd72c317fbe6d076adfee4158cc8b63d811b9a0ebc1fe132617f6394b980729"
        &&state::sha256(mode.initial_code)=="831e7045c50cb84278c3c61d94dcf01b31478c73a7f825f8cedcf65d7166427c"
        &&state::sha256(separator.initial_code)=="b36ce5bc74eb1deb4b63465054413ec15f023c58aa29f3df0b524689eef0647e"
        &&reference.size()==1049&&state::sha256(reference)=="81fbfb2ed55859f13b7ff47300f6e896d2f90af6bdd2fa38db144db5f515a318"
        &&lesson.pages[6].blocks[1].code==reference&&lesson.pages[8].blocks[1].code==reference
        &&curriculum::activity_prerequisites(lesson,reference_choice)==std::vector<curriculum::StatePosition>{{first_page,editor_index}}
        &&curriculum::activity_prerequisites(lesson,separator_choice)==std::vector<curriculum::StatePosition>{{mode_page,editor_index}},
        "The current repair sequence changed its stable owners, literal sources, answers or prerequisites");
    for(const auto* goal:{&first,&mode,&separator})require(goal->output_check&&goal->output_check->cases.size()==1
        &&goal->output_check->cases[0].standard_input.empty()
        &&goal->output_check->cases[0].outputs==std::vector<std::string>{"Total=12","Total=12\n"},
        "A repair acquired a different stdin or independent exact output objective");
    for(const auto page:{mode_page,separator_page})for(std::size_t b=0;b<2;++b)
        require(curriculum::reference_activity(lesson,lesson.pages[page+1].blocks[b])
            ==std::optional<curriculum::StatePosition>{{page,editor_index}},
            "A repair reference lost its exact compiler owner");
    const auto changed_once=[](std::string source,std::string_view from,std::string_view to){
        const auto at=source.find(from);
        require(at!=source.npos&&source.find(from,at+from.size())==source.npos,
            "A repair source variation is not one unambiguous literal edit");
        source.replace(at,from.size(),to);return source;
    };
    require(changed_once(reference,", std::ios::app","")==mode.initial_code
        &&changed_once(reference,"salida << \" 5\";","salida << \"5\";")==separator.initial_code,
        "The published repair templates differ from more than the intended mode/string");
    // This is a distinct valid repair: retain app and separate the third integer
    // with an actual newline, rather than copying the reference's leading space.
    const auto newline_repair=changed_once(separator.initial_code,"salida << \"5\";","salida << \"\\n5\";");
    require(newline_repair.size()==1050&&newline_repair!=reference,
        "The separator QA no longer exercises its independent newline alternative");
    Fixture f([&](state::StateData& data,const curriculum::Lesson& current){
        state::prepare_lesson_state(data,current);state::save_current_page(data,current,first_page);
        data.preferences["u12_file_repairs_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
    },[&](const curriculum::Lesson&){return lesson;},true);
    require(f.page()==first_page&&!IsWindowVisible(f.host),"Repair QA did not start in its explicit hidden p4");
    const auto unrelated=f.saved().preferences.at("u12_file_repairs_qa");
    const auto root=f.environment.paths.projects/L"cpp-runs";
    const auto read=[](const std::filesystem::path& path){
        std::ifstream stream(path,std::ios::binary);require(static_cast<bool>(stream),"Missing actual repair artifact: "+path.generic_string());
        return std::string(std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
    };
    const auto directories=[&]{std::vector<std::filesystem::path> result;
        if(std::filesystem::exists(root))for(const auto& item:std::filesystem::directory_iterator(root))
            if(item.is_directory())result.push_back(item.path());
        std::ranges::sort(result);return result;
    };
    const auto request=[](const std::string& source){windows::CppRunRequest result;result.source_utf8=source;return result;};
    require(directories().empty(),"The fresh repair QA inherited an execution directory");
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        require(!state::get_response(f.saved(),lesson,block),"The fresh repair QA inherited a submitted answer");
    for(const auto* goal:{&first,&mode,&separator})require(!windows::compiler_puzzle_completed(f.saved(),lesson,*goal)
        &&windows::compiler_puzzle_case_count(f.saved(),lesson,*goal)==0,
        "The fresh repair QA inherited a successful execution receipt");
    struct Artifact{std::filesystem::path path;std::string bytes,hash;std::filesystem::file_time_type stamp;};
    std::vector<Artifact> artifacts;
    const auto retain=[&](const std::filesystem::path& run){
        for(const auto& item:std::filesystem::directory_iterator(run))if(item.is_regular_file()){
            const auto bytes=read(item.path());artifacts.push_back({item.path(),bytes,state::sha256(bytes),item.last_write_time()});
        }
    };
    std::map<std::string,state::Response> responses;
    state::Preferences proofs;
    std::map<std::string,state::Draft> drafts;
    const auto reference_key=[&](const curriculum::Block& goal){
        return "reference-proof/v1/"+lesson.locale+'/'+curriculum::draft_key("programming",lesson,goal);
    };
    const auto proof_value=[](const state::Value& value){
        if(const auto text=std::get_if<std::string>(&value))return std::string("string:")+*text;
        if(const auto list=std::get_if<std::vector<std::string>>(&value)){
            std::string result="list:";for(const auto& item:*list)result+='['+item+']';return result;
        }
        if(const auto flag=std::get_if<bool>(&value))return std::string(*flag?"true":"false");
        if(const auto integer=std::get_if<std::int64_t>(&value))return std::to_string(*integer);
        return std::to_string(std::get<double>(value));
    };
    const auto preserve=[&]{
        const auto saved=f.saved();
        require(state::read_bytes(asset)==original&&!IsWindowVisible(f.host)
            &&saved.preferences.at("u12_file_repairs_qa")==unrelated,
            "Repair QA changed curriculum, unrelated preferences or desktop visibility");
        for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
            const auto found=responses.find(block.stable_id);const auto response=state::get_response(saved,lesson,block);
            if(found==responses.end())require(!response,"Repair QA fabricated a response in an independent activity");
            else require(response&&response->raw_json==found->second.raw_json&&response->answer==found->second.answer
                &&response->selected==found->second.selected&&response->correct==found->second.correct,
                "Repair QA changed an earlier correct, incorrect or reviewed answer");
        }
        for(const auto& [key,value]:proofs){const auto kept=state::find(saved,"native_windows",key);
            require(kept&&*kept==value,"A later repair edited an earlier captured execution receipt: key="+key
                +" before="+proof_value(value)+" after="+(kept?proof_value(*kept):"<missing>"));}
        for(const auto& [key,old]:drafts){const auto now=state::get_draft(saved,key,"");
            require(now.has_saved==old.has_saved&&now.source==old.source&&now.input==old.input
                &&now.indentation_json==old.indentation_json&&now.selection_start==old.selection_start&&now.selection_end==old.selection_end,
                "A later page changed an earlier exact draft, stdin, indentation or selection");}
        for(const auto& file:artifacts)require(read(file.path)==file.bytes&&state::sha256(read(file.path))==file.hash
            &&std::filesystem::last_write_time(file.path)==file.stamp,"A later repair changed an earlier program, file or process stream");
    };
    const auto remember_response=[&](const curriculum::Block& block){
        const auto response=state::get_response(f.saved(),lesson,block);
        require(response.has_value(),"A native repair submission was not persisted");responses[block.stable_id]=*response;
    };
    const auto remember_proofs=[&](const curriculum::Block& active,bool edits_active=false){
        preserve();const auto saved=f.saved();
        for(const auto* goal:{&first,&mode,&separator})if(windows::compiler_puzzle_completed(saved,lesson,*goal)){
            // Freeze the complete captured-program/cases/seal/feedback receipt
            // only after this exact owner has actually earned its objective.
            const auto prefix="compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+curriculum::exercise_key(lesson,*goal)+'/'
                +state::sha256(curriculum::reference_objective_frame(lesson,*goal))+'/';
            for(const auto field:{"program","cases","completed","feedback"}){
                const auto key=prefix+field;const auto value=state::find(saved,"native_windows",key);
                require(value!=nullptr,"A completed actual repair is missing its sealed receipt field: "+key);proofs.emplace(key,*value);
            }
            // This separate reference seal follows the editable draft. Editing
            // its own source must clear it even while the earned compiler
            // achievement stays sticky. Earlier owners' seals remain frozen.
            if(edits_active&&goal->stable_id==active.stable_id)continue;
            const auto key=reference_key(*goal);const auto value=state::find(saved,"native_windows",key);
            if(goal->stable_id==active.stable_id)require(value!=nullptr,"The accepted unedited owner has no reference seal");
            if(value)proofs.emplace(key,*value);
        }
        require(!proofs.empty(),"A successful actual repair has no sealed execution receipt");
    };
    const auto remember_draft=[&](const curriculum::Block& block){
        const auto key=curriculum::draft_key("programming",lesson,block);
        drafts[key]=state::get_draft(f.saved(),key,block.initial_code);
    };
    const auto execute=[&](const curriculum::Block& goal,const std::string& source,const std::string& output,
            const std::string& file_bytes,int exit_code,bool correct){
        require(f.text(editor_index,10)==source,"The actual repair editor differs from its independently expected source");
        const auto before=directories();f.run(editor_index);const auto after=directories();
        require(after.size()==before.size()+1,"One repair action did not create exactly one private execution");
        std::vector<std::filesystem::path> added;
        for(const auto& entry:after)if(std::ranges::find(before,entry)==before.end())added.push_back(entry);
        require(added.size()==1&&added[0].parent_path()==root,"Cannot identify the fresh actual repair run");
        const auto run=added[0];
        require(read(run/L"main.cpp")==source&&read(run/L"stdin.bin").empty()
            &&read(run/L"execution-stdout.bin")==output&&read(run/L"execution-stderr.bin").empty()
            &&read(run/L"serie_lgp.txt")==file_bytes&&std::filesystem::is_regular_file(run/L"program.exe"),
            "The repair changed exact source/stdin, raw process channels or real file characters");
        const auto status=(exit_code==0?"Ejecución terminada (código ":"La ejecución falló (código ")+std::to_string(exit_code)+").";
        require(f.text(editor_index,34)==status,"The repair displayed another actual process exit status");
        const auto channel=exit_code==0?"Salida del programa (stdout)":"Errores del programa (stderr)";
        const auto console=exit_code==0?output:"La ejecución no terminó correctamente.\n"+output;
        require(f.text(editor_index,30)==channel&&f.text(editor_index,14)==console,
            "The repair console lost its complete output or selected presentation channel");
        require(f.text(editor_index,4)==(correct?"✓ Correcto":"")
            &&windows::compiler_puzzle_completed(f.saved(),lesson,goal)==correct
            &&windows::compiler_puzzle_case_count(f.saved(),lesson,goal)==(correct?1u:0u)
            &&windows::compiler_puzzle_review(f.saved(),lesson,goal,request(source)).empty(),
            "A nonzero repair run acquired an educational rejection/achievement, or the accepted repair lost its receipt");
        const auto tools=read(run/L"toolchain.txt");require(tools.find("MSVC=")!=tools.npos&&tools.find("SDK=")!=tools.npos,
            "The repair run lost its actual selected Windows toolchain evidence");
        std::cout<<"RUN U12L3 repair page="<<f.page()+1<<" directory="<<run.generic_string()
            <<" sourceSha256="<<state::sha256(source)<<" stdoutSha256="<<state::sha256(output)
            <<" dataSha256="<<state::sha256(file_bytes)<<" dataBytes="<<file_bytes.size()
            <<" stdinBytes=0 stderrBytes=0 exit="<<exit_code<<" correct="<<correct<<'\n';
        retain(run);preserve();
    };
    const auto pending=[&](std::size_t page,const curriculum::Block& goal){
        const auto options=lesson_options_snapshot(f.view);
        require(f.page()==page&&!lesson_page_state(f.view).can_next
            &&std::ranges::find(options.available_pages,page+1)==options.available_pages.end()
            &&!navigate_lesson_view(f.view,page+1)&&f.page()==page
            &&!windows::compiler_puzzle_completed(f.saved(),lesson,goal)
            &&windows::compiler_puzzle_case_count(f.saved(),lesson,goal)==0,
            "An incomplete repair opened its protected reference or allowed page advancement");
        for(const auto& region:lesson_reading_regions(f.view))
            for(const auto b:{0u,1u}){const auto& reference_block=lesson.pages[page+1].blocks[b];
                require(region.key.find('/'+reference_block.stable_id+'/')==std::string::npos
                    &&region.anchor.find('/'+reference_block.stable_id+'/')==std::string::npos,
                    "A pending repair exposed its posterior reference to narration");}
    };
    const auto wrong_then_correct=[&](std::size_t block_index,const curriculum::Block& question,int wrong){
        const auto count=directories().size();f.click(block_index,20+wrong*2);f.click(block_index,3);remember_response(question);
        const auto feedback="✗ Incorrecto\n\n"+question.review_feedback_options.at(static_cast<std::size_t>(wrong));
        require(!responses.at(question.stable_id).correct&&responses.at(question.stable_id).selected==wrong
            &&state::response_reviewed(responses.at(question.stable_id),question)&&f.text(block_index,4)==feedback,
            "A repair distractor lost its specific complete native guide or reviewed selection");
        const auto page=f.page();f.reopen();
        require(f.page()==page&&f.text(block_index,4)==feedback&&directories().size()==count,
            "Reopening a repair distractor lost its guide or executed an editor automatically");preserve();
        f.solve(block_index);remember_response(question);
        require(responses.at(question.stable_id).correct&&f.text(block_index,4)=="✓ Correcto\n\n"+question.explanation,
            "The corrected repair choice lost its complete explanation");preserve();
    };
    const auto reopen_success=[&](const curriculum::Block& goal,const std::string& source){
        const auto count=directories().size(),page=f.page();remember_proofs(goal,true);
        const auto current_reference=reference_key(goal);
        require(!state::string_value(f.saved(),"native_windows",current_reference).empty(),
            "The successful repair did not create its own draft-bound reference seal");
        const auto editor=f.control(editor_index,10);const auto modified=source+"// QA: conservar borrador de reparacion\n";
        f.replace(editor_index,10,modified);
        require(f.text(editor_index,14).find("El programa cambió.")!=std::string::npos&&directories().size()==count,
            "Editing reused stale repair output or ran the program automatically");
        const auto edited=f.saved();
        require(!state::find(edited,"native_windows",current_reference)&&windows::compiler_puzzle_completed(edited,lesson,goal),
            "Editing retained a stale draft reference seal or revoked the independent earned compiler achievement");preserve();
        require(SendMessageW(editor,WM_UNDO,0,0),"The repair editor lost its Undo transaction");pump(f.host);
        require(f.text(editor_index,10)==source,"Repair Undo lost the accepted source");
        require(SendMessageW(editor,EM_REDO,0,0),"The repair editor lost its Redo transaction");pump(f.host);
        require(f.text(editor_index,10)==modified,"Repair Redo lost the exact edited draft");
        require(SendMessageW(editor,WM_UNDO,0,0),"The repair editor lost its final Undo transaction");pump(f.host);
        require(f.text(editor_index,10)==source,"Final repair Undo did not restore the independently accepted program");
        const CHARRANGE selection{5,17};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump(f.host);
        remember_draft(goal);const auto saved_draft=drafts.at(curriculum::draft_key("programming",lesson,goal));
        require(saved_draft.has_saved&&saved_draft.source==source&&saved_draft.input.empty()
            &&saved_draft.selection_start==selection.cpMin&&saved_draft.selection_end==selection.cpMax,
            "Repair draft capture lost source/stdin or the exact UTF-16 selection");
        f.reopen();CHARRANGE kept{};SendMessageW(f.control(editor_index,10),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&kept));
        require(f.page()==page&&f.text(editor_index,10)==source&&f.text(editor_index,4)=="✓ Correcto"
            &&kept.cpMin==selection.cpMin&&kept.cpMax==selection.cpMax&&directories().size()==count
            &&windows::compiler_puzzle_completed(f.saved(),lesson,goal),
            "Silent repair reopen lost its exact source/selection/sticky success or executed a program");preserve();
    };
    const auto inspect_reference=[&](std::size_t page){
        const auto count=directories().size();f.select_page(page);const auto regions=lesson_reading_regions(f.view);
        auto heading=lesson.pages[page].blocks[0].text;std::erase(heading,'`');
        require(f.text(0,0)==heading&&f.text(1,1)==reference&&(GetWindowLongPtrW(f.control(1,1),GWL_STYLE)&ES_READONLY)
            &&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==0
                &&region.source_text==lesson.pages[page].blocks[0].text&&region.target==f.control(0,0);})
            &&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==1&&region.passive_label
                &&region.target==f.control(1,16)&&region.source_text=="Ejemplo de código C más más.";})
            &&directories().size()==count,
            "The unlocked repair reference lost its complete readonly program/current heading or executed it automatically");preserve();
    };

    // Global mandatory compiler gates are genuine: complete the earlier owner
    // through its UI once, rather than seeding a receipt to enter the repairs.
    f.solve(2);remember_response(first_choice);f.replace(editor_index,10,reference);
    execute(first,reference,"Total=12","3 4 5",0,true);remember_proofs(first);remember_draft(first);
    f.next();require(f.page()==4,"The real first-editor prerequisite did not unlock p5");
    f.solve(3);remember_response(reference_choice);preserve();f.next();
    require(f.page()==mode_page&&f.text(editor_index,10)==mode.initial_code,
        "The actual preceding editor and dependent choice did not unlock the published p6 repair template");
    wrong_then_correct(1,mode_choice,1);pending(mode_page,mode);
    const auto mode_pending_options=lesson_options_snapshot(f.view);
    require(!navigate_lesson_view(f.view,separator_page)&&f.page()==mode_page
        &&std::ranges::find(mode_pending_options.available_pages,separator_page)==mode_pending_options.available_pages.end(),
        "The dependent p8 repair question was reachable before the p6 objective");
    execute(mode,mode.initial_code,"Lectura incompleta"," 5",1,false);pending(mode_page,mode);
    f.reopen();require(f.page()==mode_page&&f.text(editor_index,10)==mode.initial_code&&f.text(editor_index,14).empty()
        &&f.text(editor_index,4).empty()&&directories().size()==2,
        "Reopening the failed p6 run replayed a transient console or invented an educational verdict");pending(mode_page,mode);preserve();
    f.replace(editor_index,10,reference);execute(mode,reference,"Total=12","3 4 5",0,true);reopen_success(mode,reference);
    require(lesson_page_state(f.view).can_next,"The accepted p6 repair did not unlock its protected reference");
    inspect_reference(6);f.next();
    require(f.page()==separator_page&&f.text(editor_index,10)==separator.initial_code
        &&!state::get_response(f.saved(),lesson,separator_choice)&&!IsWindowEnabled(f.control(1,3)),
        "Completing p6 failed to enable the independent unsubmitted p8 question/editor");
    for(std::size_t option=0;option<separator_choice.options.size();++option)
        require(IsWindowEnabled(f.control(1,20+static_cast<int>(option)*2))
            &&SendMessageW(f.control(1,20+static_cast<int>(option)*2),BM_GETCHECK,0,0)==BST_UNCHECKED,
            "The newly available p8 question has a locked radio or a fabricated selection");
    wrong_then_correct(1,separator_choice,0);pending(separator_page,separator);
    execute(separator,separator.initial_code,"Lectura incompleta","3 45",1,false);pending(separator_page,separator);
    f.reopen();require(f.page()==separator_page&&f.text(editor_index,10)==separator.initial_code&&f.text(editor_index,14).empty()
        &&f.text(editor_index,4).empty()&&directories().size()==4,
        "Reopening the failed p8 run replayed its transient console or invented a verdict");pending(separator_page,separator);preserve();
    f.replace(editor_index,10,newline_repair);execute(separator,newline_repair,"Total=12","3 4\r\n5",0,true);
    reopen_success(separator,newline_repair);
    require(lesson_page_state(f.view).can_next,"The accepted newline repair did not unlock p9");inspect_reference(8);
    require(f.page()==8&&directories().size()==5&&responses.size()==4,
        "The five-run repair sequence left its final reference or submitted an independent activity");
    for(const auto* goal:{&first,&mode,&separator})require(windows::compiler_puzzle_completed(f.saved(),lesson,*goal)
        &&windows::compiler_puzzle_case_count(f.saved(),lesson,*goal)==1,
        "The later repairs lost an independently earned prior compiler achievement");
    for(const auto& item:std::filesystem::recursive_directory_iterator(f.environment.paths.root))if(item.is_regular_file())
        require(item.path().extension()!=L".wav"&&item.path().extension()!=L".opus","A repair view check unexpectedly generated audio");
    preserve();
    std::cout<<"PASS current U12L3 repairs view: fresh hidden QA explicitly starts at p4 with no submitted answers/receipts; real first-editor reference and p5 choice earn prerequisite; p6 wrong-space and p8 app-separator distractors retain complete guides across reopen; unchanged templates exit1 with Lectura incompleta and exact files ' 5'/'3 45', no educational verdict or achievement; only-mode and independent newline-string repairs produce Total=12 with exact files '3 4 5'/'3 4\\r\\n5'; five private runs, p7/p9 protected references, p8 dependent question, earlier answers/receipts/artifacts and source/stdin/indentation/selection/Undo/Redo preserved. Ends on p9 without Finalizar or U12L4. No audio, listening, student authorship or full lesson traversal claimed.\n";
}
