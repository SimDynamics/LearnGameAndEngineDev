#pragma once

// Included after Fixture. The complete authored lesson is isolated at p4;
// every answer and compiler result below comes from its actual controls.
void current_u12_file_append_view(){
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_cerrar_reemplazar_y_anadir_datos.json";
    const auto original=state::read_bytes(asset);
    std::string normalized(reinterpret_cast<const char*>(original.data()),original.size());std::erase(normalized,'\r');
    require(state::sha256(normalized)=="9fa23a593751256a5936eabfe1ae7dc8a2388a2dd80a146af6338557c625a55b",
        "The current U12L3 append asset differs from its independently audited LF source");
    const auto lesson=windows::load_lesson(asset);
    constexpr std::size_t page=3,compiler=3,choice=2,note=4,reference_page=4;
    const auto& goal=lesson.pages.at(page).blocks.at(compiler);
    const auto& question=lesson.pages.at(page).blocks.at(choice);
    const auto& dependent=lesson.pages.at(reference_page).blocks.at(3);
    const auto reference=lesson.pages.at(reference_page).blocks.at(1).code;
    const auto& mode_repair=lesson.pages.at(5).blocks.at(3);
    const auto& separator_repair=lesson.pages.at(7).blocks.at(3);
    const auto metadata=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(lesson.state_layout.raw_json));
    const std::vector<std::string> references{"block_0003_0004","block_0004_0000","block_0004_0001","block_0004_0002"};
    const curriculum::StatePosition owner{page,compiler};
    require(lesson.id=="cpp_fundamentals__archivos_y_programa_completo__cerrar_reemplazar_y_anadir_datos"
        &&lesson.pages.size()==9&&lesson.state_layout.signatures.size()==33&&metadata.GetNamedArray(L"editorial").Size()==12
        &&!lesson.compiler_puzzles_required&&goal.stable_id=="block_0003_0003"
        &&goal.type==BlockType::cpp_compiler&&curriculum::compiler_is_puzzle_prerequisite(lesson,goal)
        &&goal.initial_code.size()==130&&state::sha256(goal.initial_code)=="6cd72c317fbe6d076adfee4158cc8b63d811b9a0ebc1fe132617f6394b980729"
        &&goal.output_check&&goal.output_check->cases.size()==1&&goal.output_check->cases[0].standard_input.empty()
        &&goal.output_check->cases[0].outputs==std::vector<std::string>{"Total=12","Total=12\n"}
        &&question.stable_id=="block_0003_0002"&&question.correct_index==2
        &&reference.size()==1049&&state::sha256(reference)=="81fbfb2ed55859f13b7ff47300f6e896d2f90af6bdd2fa38db144db5f515a318"
        &&lesson.reference_after.size()==1&&lesson.reference_after[0].activity==goal.stable_id
        &&lesson.reference_after[0].blocks==references&&lesson.activity_after.size()==1
        &&lesson.activity_after[0].activity==dependent.stable_id
        &&lesson.activity_after[0].prerequisites==std::vector<std::string>{goal.stable_id}
        &&dependent.stable_id=="block_0004_0003"&&dependent.type==BlockType::multiple_choice&&dependent.correct_index==0
        &&curriculum::activity_prerequisites(lesson,dependent)==std::vector<curriculum::StatePosition>{owner}
        &&mode_repair.stable_id=="block_0005_0003"&&separator_repair.stable_id=="block_0007_0003"
        &&!curriculum::compiler_is_puzzle_prerequisite(lesson,mode_repair)
        &&!curriculum::compiler_is_puzzle_prerequisite(lesson,separator_repair),
        "The current append construction, exact output or selective reference/puzzle gates changed");
    for(const auto& identity:references){const auto at=curriculum::stable_block_position(lesson,identity);
        require(at&&curriculum::reference_activity(lesson,lesson.pages[at->page].blocks[at->block])
            ==std::optional<curriculum::StatePosition>{owner},"An actual append reference lost its stable first-editor owner");}
    const auto variant=[&](std::string_view from,std::string_view to){
        auto result=reference;const auto at=result.find(from);
        require(at!=result.npos&&result.find(from,at+from.size())==result.npos,"A QA append variation is not one exact independent source edit");
        result.replace(at,from.size(),to);return result;
    };
    const auto no_app=variant(", std::ios::app","");
    const auto no_separator=variant("salida << \" 5\";","salida << \"5\";");
    require(state::sha256(no_app)=="831e7045c50cb84278c3c61d94dcf01b31478c73a7f825f8cedcf65d7166427c"
        &&state::sha256(no_separator)=="b36ce5bc74eb1deb4b63465054413ec15f023c58aa29f3df0b524689eef0647e"
        &&no_app==mode_repair.initial_code&&no_separator==separator_repair.initial_code,
        "The two isolated QA variations no longer match their independently conserved source programs");
    Fixture f([&](state::StateData& data,const curriculum::Lesson& current){
        state::prepare_lesson_state(data,current);state::save_current_page(data,current,page);
        data.preferences["u12_file_append_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
    },[&](const curriculum::Lesson&){return lesson;},true);
    require(f.page()==page&&!IsWindowVisible(f.host),"Append QA did not start in its complete hidden isolated page");
    const auto unrelated=f.saved().preferences.at("u12_file_append_qa");
    const auto root=f.environment.paths.projects/L"cpp-runs";
    const auto read=[](const std::filesystem::path& path){
        std::ifstream stream(path,std::ios::binary);require(static_cast<bool>(stream),"Missing actual append artifact: "+path.generic_string());
        return std::string(std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
    };
    const auto directories=[&]{std::vector<std::filesystem::path> result;
        if(std::filesystem::exists(root))for(const auto& item:std::filesystem::directory_iterator(root))
            if(item.is_directory())result.push_back(item.path());
        std::ranges::sort(result);return result;
    };
    const auto request=[](const std::string& source){windows::CppRunRequest value;value.source_utf8=source;return value;};
    require(directories().empty(),"A fresh append profile inherited another execution");
    for(const auto& current_page:lesson.pages)for(const auto& block:current_page.blocks)
        require(!state::get_response(f.saved(),lesson,block),"A fresh append profile inherited an answer");
    f.solve(choice);const auto answered=state::get_response(f.saved(),lesson,question);
    require(answered&&answered->correct&&answered->selected==2,"The preceding append choice was not answered by its native control");
    const auto pending_gate=[&](std::string_view stage){
        const auto options=lesson_options_snapshot(f.view);
        require(f.page()==page&&!lesson_page_state(f.view).can_next
            &&std::ranges::find(options.available_pages,reference_page)==options.available_pages.end()
            &&!navigate_lesson_view(f.view,reference_page)&&f.page()==page
            &&!descendant(f.view,id(note,0))&&!descendant(f.view,id(note,19))
            &&!windows::compiler_puzzle_completed(f.saved(),lesson,goal)&&windows::compiler_puzzle_case_count(f.saved(),lesson,goal)==0,
            "A pending append construction opened its note, dependent puzzle or references: "+std::string(stage));
        for(const auto& region:lesson_reading_regions(f.view)){
            require(region.block_index!=note,"The pending append editor exposed its post-editor note to narration");
            for(const auto& identity:{"block_0004_0000","block_0004_0001","block_0004_0002","block_0004_0003"})
                require(region.key.find('/'+std::string(identity)+'/')==std::string::npos
                    &&region.anchor.find('/'+std::string(identity)+'/')==std::string::npos,
                    "A pending append construction leaked its posterior reference/dependent-puzzle reader source");
        }
    };
    pending_gate("correct preceding choice; untouched first editor");
    struct File{std::filesystem::path path;std::string bytes,hash;std::filesystem::file_time_type stamp;};
    std::vector<File> retained;
    const auto retain=[&](const std::filesystem::path& run){
        for(const auto& item:std::filesystem::directory_iterator(run))if(item.is_regular_file()){
            const auto bytes=read(item.path());retained.push_back({item.path(),bytes,state::sha256(bytes),item.last_write_time()});
        }
    };
    const auto preserve=[&]{
        const auto saved=f.saved();const auto response=state::get_response(saved,lesson,question);
        require(state::read_bytes(asset)==original&&!IsWindowVisible(f.host)
            &&saved.preferences.at("u12_file_append_qa")==unrelated&&response&&response->correct&&response->raw_json==answered->raw_json,
            "Append QA changed curriculum, unrelated data, the preceding answer or desktop visibility");
        for(const auto& file:retained)require(read(file.path)==file.bytes&&state::sha256(read(file.path))==file.hash
            &&std::filesystem::last_write_time(file.path)==file.stamp,"A later append run changed an earlier file, program or stream");
        for(const auto& current_page:lesson.pages)for(const auto& block:current_page.blocks)
            if(block.stable_id!=question.stable_id)require(!state::get_response(saved,lesson,block),
                "Testing the append editor fabricated an independent answer");
        for(const auto* repair:{&mode_repair,&separator_repair})require(!windows::compiler_puzzle_completed(saved,lesson,*repair)
            &&windows::compiler_puzzle_case_count(saved,lesson,*repair)==0
            &&!state::get_draft(saved,curriculum::draft_key("programming",lesson,*repair),repair->initial_code).has_saved,
            "The isolated first-editor test activated a separate curricular repair or changed its draft");
        for(const auto& item:std::filesystem::recursive_directory_iterator(f.environment.paths.root))if(item.is_regular_file())
            require(item.path().extension()!=L".wav"&&item.path().extension()!=L".opus","An append view test unexpectedly generated audio");
    };
    const auto execute=[&](const std::string& source,const std::string& raw_output,const std::optional<std::string>& file_data,
            int exit_code,bool correct,bool evaluated){
        if(f.text(compiler,10)!=source)f.replace(compiler,10,source);
        const auto before=directories();f.run(compiler);const auto after=directories();
        require(after.size()==before.size()+1,"One actual append action did not create exactly one new private execution directory");
        std::vector<std::filesystem::path> added;
        for(const auto& item:after)if(std::ranges::find(before,item)==before.end())added.push_back(item);
        require(added.size()==1&&added.front().parent_path()==root,"Cannot identify the actual fresh append run directory");
        const auto run=added.front();
        require(read(run/L"main.cpp")==source&&read(run/L"stdin.bin").empty()
            &&read(run/L"execution-stdout.bin")==raw_output&&read(run/L"execution-stderr.bin").empty()
            &&std::filesystem::is_regular_file(run/L"program.exe"),"An append run changed source/stdin or its independently expected raw output");
        const auto expected_status=(exit_code==0?"Ejecución terminada (código ":"La ejecución falló (código ")+std::to_string(exit_code)+").";
        require(f.text(compiler,34)==expected_status,"The append action exposed another real exit status: "+f.text(compiler,34));
        std::string console=raw_output,channel="Salida del programa (stdout)";
        if(exit_code!=0){
            // The raw program wrote its diagnostic to stdout. The existing UI
            // additionally presents a failed-execution summary on its error
            // channel; that summary is not captured process stderr.
            channel="Errores del programa (stderr)";console="La ejecución no terminó correctamente.\n"+raw_output;
        }else if(raw_output.empty()){
            const auto diagnostics=read(run/L"compilation-stdout.bin")+read(run/L"compilation-stderr.bin");(void)wide(diagnostics);
            if(!diagnostics.empty()){
                channel="Diagnósticos de compilación y enlace";console.clear();
                for(std::size_t at=0;at<diagnostics.size();++at)if(diagnostics[at]=='\r'){
                    if(at+1<diagnostics.size()&&diagnostics[at+1]=='\n')++at;console+='\n';
                }else console+=diagnostics[at];
            }
        }
        require(f.text(compiler,30)==channel&&f.text(compiler,14)==console,
            "The actual append console changed its selected process/diagnostic channel or complete source");
        require(!std::filesystem::exists(run/L"old-run-only.txt"),"A fresh append run inherited a completed run's private sentinel");
        const auto data=run/L"serie_lgp.txt";
        require(file_data?std::filesystem::is_regular_file(data)&&read(data)==*file_data:!std::filesystem::exists(data),
            "The append run's real file does not contain the independently expected character sequence");
        require((evaluated?f.text(compiler,4)==(correct?"✓ Correcto":"✗ Incorrecto"):f.text(compiler,4).empty())
            &&windows::compiler_puzzle_completed(f.saved(),lesson,goal)==correct,
            "A rejected output, failed execution or successful append program acquired the wrong curricular verdict");
        require(windows::compiler_puzzle_review(f.saved(),lesson,goal,request(source))==(evaluated&&!correct?"Incorrecto":""),
            "A nonzero append execution retained an obsolete educational rejection or review");
        const auto toolchain=read(run/L"toolchain.txt");
        require(toolchain.find("MSVC=")!=toolchain.npos&&toolchain.find("SDK=")!=toolchain.npos,
            "The actual append run lost its selected Windows toolchain evidence");
        std::cout<<"RUN U12L3 append directory="<<run.generic_string()<<" sourceSha256="<<state::sha256(source)
            <<" stdoutSha256="<<state::sha256(raw_output)<<" stdinBytes=0 stderrBytes=0 exit="<<exit_code
            <<" evaluated="<<evaluated<<" correct="<<correct;
        if(file_data)std::cout<<" dataSha256="<<state::sha256(*file_data)<<" dataBytes="<<file_data->size();
        std::cout<<'\n';preserve();return run;
    };
    const auto initial=execute(goal.initial_code,"",std::nullopt,0,false,true);pending_gate("executed unchanged template");
    const auto initial_review=f.text(compiler,4);f.reopen();
    require(f.page()==page&&f.text(compiler,10)==goal.initial_code&&f.text(compiler,4)==initial_review&&directories().size()==1,
        "Reopening lost the actual initial rejection or ran the first editor automatically");pending_gate("reopened initial rejection");
    // Only this completed, test-owned run receives a sentinel. No external or
    // personal path is edited, and later runs must leave these bytes intact.
    for(const auto name:{L"serie_lgp.txt",L"old-run-only.txt"}){
        std::ofstream stream(initial/name,std::ios::binary|std::ios::trunc);require(static_cast<bool>(stream),"Cannot write the completed QA run sentinel");
        stream<<"QA: conservar esta muestra anterior\r\n";require(static_cast<bool>(stream),"Cannot retain the completed QA run sentinel");
    }
    retain(initial);
    const auto replaced=execute(no_app,"Lectura incompleta",std::string(" 5"),1,false,false);retain(replaced);
    pending_gate("default second opening replaced the prior two integers");
    const auto joined=execute(no_separator,"Lectura incompleta",std::string("3 45"),1,false,false);retain(joined);
    pending_gate("app retained the characters but the missing space joined the last two integers");
    const auto accepted_run=execute(reference,"Total=12",std::string("3 4 5"),0,true,true);retain(accepted_run);
    const auto completed_gate=[&]{
        const auto regions=lesson_reading_regions(f.view);const auto options=lesson_options_snapshot(f.view);
        require(f.page()==page&&lesson_page_state(f.view).can_next
            &&f.text(note,0)==lesson.pages[page].blocks[note].text
            &&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==note
                &&region.source_text==lesson.pages[page].blocks[note].text&&region.target==f.control(note,0);})
            &&std::ranges::find(options.available_pages,reference_page)!=options.available_pages.end()
            &&std::ranges::find(options.available_pages,5)==options.available_pages.end()
            &&windows::compiler_puzzle_completed(f.saved(),lesson,goal)&&windows::compiler_puzzle_case_count(f.saved(),lesson,goal)==1,
            "A successful append construction lost its note/reference access or bypassed the independently unanswered later choice");
    };
    completed_gate();
    const auto draft_key=curriculum::draft_key("programming",lesson,goal);
    const auto before_reference=f.saved();const auto draft_before=state::get_draft(before_reference,draft_key,goal.initial_code);
    state::Preferences navigation_proofs;
    for(const auto& [key,value]:before_reference.preferences.at("native_windows"))
        if(key.starts_with("compiler-puzzle/v1/")||key.starts_with("reference-proof/v1/"))navigation_proofs.emplace(key,value);
    f.select_page(reference_page);const auto regions=lesson_reading_regions(f.view);
    for(std::size_t index=0;index<3;++index){const auto& block=lesson.pages[reference_page].blocks[index];
        if(block.type==BlockType::code){
            require(f.text(index,1)==reference&&(GetWindowLongPtrW(f.control(index,1),GWL_STYLE)&ES_READONLY)
                &&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==index
                    &&region.target==f.control(index,16)&&region.passive_label&&region.source_text=="Ejemplo de código C más más.";}),
                "The unlocked append reference lost its exact read-only source or code label");
        }else{
            auto visible=block.text;std::erase(visible,'`');
            require(f.text(index,0)==visible&&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==index
                &&region.target==f.control(index,0)&&region.source_text==block.text;}),
                "The unlocked append reference lost its complete current prose or narration source");
        }
    }
    auto visible_question=dependent.question;std::erase(visible_question,'`');
    require(f.text(3,0)==visible_question&&windows::compiler_puzzle_completed(f.saved(),lesson,goal),
        "The accepted first editor did not mount the complete dependent question with its required owner completed");
    // A freshly mounted multiple choice has no selected option. Its radios
    // are editable, while the ordinary Check action stays disabled until an
    // option is chosen; opening the reference must not submit that choice.
    require(!state::get_response(f.saved(),lesson,dependent)&&!IsWindowEnabled(f.control(3,3))
        &&window_text(f.control(3,3))=="Comprobar",
        "Visiting the append reference selected/submitted its dependent choice or enabled an unprepared Check action");
    require(!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,5)&&f.page()==reference_page,
        "The independently unanswered append choice allowed entry into a later repair page");
    require(std::ranges::any_of(regions,[&](const auto& region){return region.block_index==3&&region.is_exercise&&!region.solved
        &&region.target==f.control(3,0)&&region.source_text==dependent.question;}),"The unlocked dependent append puzzle has no real pending reader region");
    for(std::size_t option=0;option<dependent.options.size();++option){
        auto visible=dependent.options[option];std::erase(visible,'`');
        require(f.text(3,21+static_cast<int>(option)*2)==visible
            &&IsWindowEnabled(f.control(3,20+static_cast<int>(option)*2))
            &&SendMessageW(f.control(3,20+static_cast<int>(option)*2),BM_GETCHECK,0,0)==BST_UNCHECKED,
            "An unlocked append option lost its full original text/editable radio or acquired an unsubmitted selection");
    }
    require(directories().size()==4,"Visiting the append reference automatically executed a program");preserve();
    f.select_page(page);completed_gate();const auto returned=f.saved();const auto returned_draft=state::get_draft(returned,draft_key,goal.initial_code);
    require(f.text(compiler,10)==reference&&returned_draft.source==draft_before.source&&returned_draft.input==draft_before.input
        &&returned_draft.indentation_json==draft_before.indentation_json&&returned_draft.selection_start==draft_before.selection_start
        &&returned_draft.selection_end==draft_before.selection_end&&directories().size()==4,
        "Reference navigation changed the accepted append draft/selection or executed it automatically");
    for(const auto& [key,value]:navigation_proofs){const auto retained_proof=state::find(returned,"native_windows",key);
        require(retained_proof&&*retained_proof==value,"Reference navigation changed an accepted append execution proof");}
    const auto repeated=execute(reference,"Total=12",std::string("3 4 5"),0,true,true);retain(repeated);
    const auto accepted=f.saved();state::Preferences proofs;
    for(const auto& [key,value]:accepted.preferences.at("native_windows"))if(key.starts_with("compiler-puzzle/v1/"))proofs.emplace(key,value);
    require(!proofs.empty(),"The real append construction has no sealed successful execution proof");
    const auto modified=reference+"// QA: borrador de anadir conservado\n";
    const auto editor=f.control(compiler,10);f.replace(compiler,10,modified);
    require(f.text(compiler,14).find("El programa cambió.")!=std::string::npos&&directories().size()==5,
        "Editing retained stale live append output or ran automatically");
    completed_gate();
    require(SendMessageW(editor,WM_UNDO,0,0),"The append editor lost its actual Undo transaction");pump(f.host);
    require(f.text(compiler,10)==reference,"Undo did not restore the exact accepted append source");
    require(SendMessageW(editor,EM_REDO,0,0),"The append editor lost its actual Redo transaction");pump(f.host);
    require(f.text(compiler,10)==modified,"Redo did not restore the exact edited append draft");
    const CHARRANGE selection{5,17};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump(f.host);
    const auto before=state::get_draft(f.saved(),draft_key,goal.initial_code);
    require(before.has_saved&&before.source==modified&&before.selection_start==selection.cpMin&&before.selection_end==selection.cpMax,
        "Saving the append editor lost its exact draft or selection");
    f.reopen();const auto reopened=f.saved();const auto after=state::get_draft(reopened,draft_key,goal.initial_code);
    CHARRANGE restored{};SendMessageW(f.control(compiler,10),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&restored));
    require(f.page()==page&&f.text(compiler,10)==modified&&after.source==before.source&&after.input==before.input
        &&after.indentation_json==before.indentation_json&&restored.cpMin==selection.cpMin&&restored.cpMax==selection.cpMax
        &&directories().size()==5&&windows::compiler_puzzle_completed(reopened,lesson,goal)
        &&windows::compiler_puzzle_case_count(reopened,lesson,goal)==1,
        "Whole-view append reopen lost draft/selection/sticky success or ran automatically");
    for(const auto& [key,value]:proofs){const auto retained_proof=state::find(reopened,"native_windows",key);
        require(retained_proof&&*retained_proof==value,"Editing/reopening changed the append construction's captured successful receipt");}
    completed_gate();preserve();
    std::cout<<"PASS current U12L3 append view: complete authored lesson isolated at p4; real preceding choice; empty initial Incorrecto and restored rejection; exact no-app/missing-space files and exit1 diagnostics remain unavailable without educational verdict or achievement; author reference/repeat produce Total=12 and file 3 4 5 in five fresh private directories; current note/references/dependent choice gates, retained prior artifacts, draft/selection/Undo/Redo and silent sticky-success reopen. Other two curricular editors, their answers and drafts unchanged; no audio, student authorship, full lesson traversal or listening acceptance claimed.\n";
}
