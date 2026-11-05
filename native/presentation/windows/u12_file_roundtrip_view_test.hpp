#pragma once

// Included after the existing hidden Fixture. Every compilation below starts
// at the actual lesson action; no response, execution or receipt is seeded.
void current_u12_file_roundtrip_view(){
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_abrir_escribir_y_leer_una_muestra.json";
    const auto original=state::read_bytes(asset);
    std::string normalized(reinterpret_cast<const char*>(original.data()),original.size());
    std::erase(normalized,'\r');
    require(state::sha256(normalized)=="54d635d601cc9c8f3a8b7dbdfa31c194f160d6a4452f9e18980db3e84d87a058",
        "The current U12L2 round-trip asset differs from its independently audited LF source");
    const auto lesson=windows::load_lesson(asset);
    constexpr std::size_t page=3,compiler=3,choice=2;
    const auto& goal=lesson.pages.at(page).blocks.at(compiler);
    const auto& question=lesson.pages.at(page).blocks.at(choice);
    const auto single=lesson.pages.at(4).blocks.at(1).code;
    const auto battery=lesson.pages.at(5).blocks.at(1).code;
    require(lesson.id=="cpp_fundamentals__archivos_y_programa_completo__abrir_escribir_y_leer_una_muestra"
        &&lesson.pages.size()==6&&goal.stable_id=="block_0003_0003"
        &&goal.type==BlockType::cpp_compiler&&goal.initial_code.size()==141
        &&goal.output_check&&goal.output_check->cases.size()==1
        &&goal.output_check->cases[0].standard_input.empty()
        &&goal.output_check->cases[0].outputs==std::vector<std::string>{"Total=11\nTotal=6\n"}
        &&question.correct_index==1&&single.size()==671&&battery.size()==805
        &&single.find("salida << \"4 7\";")!=single.npos
        &&battery.find("if (probar(4, 7) != 0) return 1;")!=battery.npos
        &&battery.find("if (probar(0, 6) != 0) return 1;")!=battery.npos,
        "The actual U12 file round-trip owner or its independent aggregate oracle changed");
    const std::vector<std::string> references{"block_0004_0000","block_0004_0001","block_0004_0002",
        "block_0004_0003","block_0004_0004","block_0005_0000","block_0005_0001"};
    require(lesson.compiler_puzzles_required&&lesson.reference_after.size()==1&&lesson.activity_after.empty()
        &&lesson.reference_after[0].activity==goal.stable_id&&lesson.reference_after[0].blocks==references,
        "The integrated U12L2 battery or its seven stable posterior references changed");
    for(const auto& identity:references){const auto at=curriculum::stable_block_position(lesson,identity);
        require(at&&(at->page==4||at->page==5)
            &&curriculum::reference_activity(lesson,lesson.pages[at->page].blocks[at->block])
                ==std::optional<curriculum::StatePosition>{curriculum::StatePosition{page,compiler}},
            "An actual posterior reference lost its stable compiler owner: "+identity);
    }
    Fixture f([&](state::StateData& data,const curriculum::Lesson& current){
        state::prepare_lesson_state(data,current);
        state::save_current_page(data,current,page);
        data.preferences["u12_file_roundtrip_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
    },[&](const curriculum::Lesson&){return lesson;},true);
    require(f.page()==page&&!IsWindowVisible(f.host),"File round-trip QA must start in its complete hidden isolated page");
    const auto unrelated=f.saved().preferences.at("u12_file_roundtrip_qa");
    const auto root=f.environment.paths.projects/L"cpp-runs";
    const auto read=[](const std::filesystem::path& path){
        std::ifstream stream(path,std::ios::binary);require(static_cast<bool>(stream),"Missing actual run artifact: "+path.generic_string());
        return std::string(std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
    };
    const auto directories=[&]{std::vector<std::filesystem::path> result;
        if(std::filesystem::exists(root))for(const auto& item:std::filesystem::directory_iterator(root))
            if(item.is_directory())result.push_back(item.path());
        std::ranges::sort(result);return result;
    };
    require(directories().empty(),"A fresh U12 profile inherited another run directory");
    for(const auto& current_page:lesson.pages)for(const auto& block:current_page.blocks)
        require(!state::get_response(f.saved(),lesson,block),"A fresh file round-trip profile inherited an answer");
    f.solve(choice);
    const auto answered=state::get_response(f.saved(),lesson,question);
    require(answered&&answered->correct&&answered->selected==1,"The preceding current choice was not answered by its real native control");
    const auto pending_gate=[&](std::string_view stage){
        const auto options=lesson_options_snapshot(f.view);
        require(f.page()==page&&!lesson_page_state(f.view).can_next
            &&std::ranges::find(options.available_pages,4)==options.available_pages.end()
            &&std::ranges::find(options.available_pages,5)==options.available_pages.end()
            &&!navigate_lesson_view(f.view,4)&&!navigate_lesson_view(f.view,5)&&f.page()==page,
            "A pending U12L2 compiler allowed its posterior reference pages: "+std::string(stage));
        require(!descendant(f.view,id(4,0))&&!descendant(f.view,id(4,19))
            &&std::ranges::none_of(lesson_reading_regions(f.view),[](const auto& region){return region.block_index==4;}),
            "The pending U12L2 compiler exposed/narrated its post-editor note: "+std::string(stage));
        for(const auto& region:lesson_reading_regions(f.view))for(const auto& identity:references)
            require(region.key.find('/'+identity+'/')==std::string::npos
                &&region.anchor.find('/'+identity+'/')==std::string::npos,
                "A locked posterior U12L2 reference leaked into the reader: "+identity);
        require(!windows::compiler_puzzle_completed(f.saved(),lesson,goal)
            &&windows::compiler_puzzle_case_count(f.saved(),lesson,goal)==0,
            "A pending or rejected U12L2 program acquired a successful execution proof: "+std::string(stage));
    };
    pending_gate("correct preceding choice; untouched editor");
    struct File{std::filesystem::path path;std::string bytes,hash;std::filesystem::file_time_type modified;};
    std::vector<File> retained;
    const auto retain=[&](const std::filesystem::path& run){
        for(const auto& item:std::filesystem::directory_iterator(run))if(item.is_regular_file()){
            const auto bytes=read(item.path());retained.push_back({item.path(),bytes,state::sha256(bytes),item.last_write_time()});
        }
    };
    const auto preserve=[&]{
        require(state::read_bytes(asset)==original&&!IsWindowVisible(f.host)
            &&f.saved().preferences.at("u12_file_roundtrip_qa")==unrelated,
            "A file round-trip changed curriculum, unrelated data or desktop visibility");
        const auto response=state::get_response(f.saved(),lesson,question);
        require(response&&response->raw_json==answered->raw_json&&response->correct,
            "File IO or source editing changed the independent preceding answer");
        for(const auto& file:retained)require(read(file.path)==file.bytes&&state::sha256(read(file.path))==file.hash
            &&std::filesystem::last_write_time(file.path)==file.modified,
            "A later run edited, deleted or replaced an earlier source, data file or captured stream");
        for(std::size_t p=0;p<lesson.pages.size();++p)if(p!=page)for(const auto& block:lesson.pages[p].blocks)
            require(!state::get_response(f.saved(),lesson,block),"An isolated file round-trip fabricated an earlier/later-page answer");
        for(const auto& item:std::filesystem::recursive_directory_iterator(f.environment.paths.root))if(item.is_regular_file())
            require(item.path().extension()!=L".wav"&&item.path().extension()!=L".opus",
                "A file round-trip integration test unexpectedly generated audio");
    };
    const auto execute=[&](const std::string& source,const std::string& raw_output,const std::string& display_output,
            const std::optional<std::string>& file_data,bool correct){
        if(f.text(compiler,10)!=source)f.replace(compiler,10,source);
        const auto before=directories();f.run(compiler);const auto after=directories();
        require(after.size()==before.size()+1,"One actual compile action did not create exactly one new private run directory");
        std::vector<std::filesystem::path> added;
        for(const auto& item:after)if(std::ranges::find(before,item)==before.end())added.push_back(item);
        require(added.size()==1&&added.front().parent_path()==root,"Cannot identify the actual new U12 execution directory");
        const auto run=added.front();
        require(read(run/L"main.cpp")==source,"The lesson action changed the exact captured source");
        require(read(run/L"stdin.bin").empty(),"The file round-trip inherited unexpected standard input");
        const auto actual_output=read(run/L"execution-stdout.bin"),actual_error=read(run/L"execution-stderr.bin");
        require(actual_output==raw_output,"The file round-trip stdout differs: expected="+std::to_string(raw_output.size())
            +" bytes actual="+std::to_string(actual_output.size()));
        require(actual_error.empty(),"The file round-trip produced unexpected stderr: "+actual_error);
        require(std::filesystem::is_regular_file(run/L"program.exe")
            &&f.text(compiler,34)=="Ejecución terminada (código 0).",
            "The lesson action did not expose a real successful exit: "+f.text(compiler,34));
        auto expected_console=display_output;
        std::string expected_channel="Salida del programa (stdout)";
        if(actual_output.empty()&&actual_error.empty()){
            // With both program streams empty the existing console selects
            // captured compiler diagnostics, if present. That useful channel
            // fallback must not be mistaken for nonempty program stdout.
            const auto diagnostics=read(run/L"compilation-stdout.bin")+read(run/L"compilation-stderr.bin");
            (void)wide(diagnostics); // The real tool adapter promises UTF-8.
            if(!diagnostics.empty()){
                expected_channel="Diagnósticos de compilación y enlace";
                expected_console.clear();
                for(std::size_t at=0;at<diagnostics.size();++at){
                    if(diagnostics[at]=='\r'){
                        if(at+1<diagnostics.size()&&diagnostics[at+1]=='\n')++at;
                        expected_console+='\n';
                    }else expected_console+=diagnostics[at];
                }
            }
        }
        require(f.text(compiler,30)==expected_channel,
            "The lesson console selected another actual stream: "+f.text(compiler,30));
        require(f.text(compiler,14)==expected_console,
            "The lesson console changed its selected captured stream: expected="+std::to_string(expected_console.size())
            +" bytes actual="+std::to_string(f.text(compiler,14).size())+" channel="+expected_channel);
        require(!std::filesystem::exists(run/L"old-run-only.txt"),"A new file execution inherited a prior run's private sentinel");
        const auto data=run/L"datos_recarga.txt";
        require(file_data?std::filesystem::is_regular_file(data)&&read(data)==*file_data:!std::filesystem::exists(data),
            "The actual file contents differ from the independently expected round-trip result");
        require(f.text(compiler,4).starts_with(correct?"✓ Correcto":"✗ Incorrecto")
            &&windows::compiler_puzzle_completed(f.saved(),lesson,goal)==correct,
            "Current aggregate output checking differs from its two exact authored reports");
        const auto toolchain=read(run/L"toolchain.txt");
        require(toolchain.find("MSVC=")!=toolchain.npos&&toolchain.find("SDK=")!=toolchain.npos,
            "The real compiler run lost its selected toolchain evidence");
        std::cout<<"RUN U12L2 directory="<<run.generic_string()<<" sourceSha256="<<state::sha256(source)
            <<" stdoutSha256="<<state::sha256(raw_output)<<" stdinBytes=0 stderrBytes=0 exit=0 correct="<<correct;
        if(file_data)std::cout<<" dataSha256="<<state::sha256(*file_data)<<" dataBytes="<<file_data->size();
        std::cout<<'\n';preserve();return run;
    };
    const auto initial=execute(goal.initial_code,"","",std::nullopt,false);
    pending_gate("executed unchanged template; empty stdout");
    // Deliberately seed only this completed test-owned run. Fresh executions
    // must neither inherit this data nor change the prior folder's contents.
    for(const auto name:{L"datos_recarga.txt",L"old-run-only.txt"}){
        std::ofstream stream(initial/name,std::ios::binary|std::ios::trunc);
        require(static_cast<bool>(stream),"Cannot write the private completed-run sentinel");
        stream<<"QA: conservar este archivo anterior\r\n";
        require(static_cast<bool>(stream),"Cannot preserve the private completed-run sentinel");
    }
    retain(initial);
    const auto single_run=execute(single,"Total=11","Total=11",std::string("4 7"),false);retain(single_run);
    pending_gate("executed one-sample reference; missing second report");
    f.reopen();
    require(f.text(compiler,10)==single&&f.text(compiler,4)=="✗ Incorrecto"&&directories().size()==2,
        "Whole-view reopen lost the actual rejected program/verdict or executed it automatically");
    pending_gate("reopened rejected one-sample program");preserve();
    // CRT text streams persist CRLF bytes. The real evaluator/UI normalizes
    // those transport newlines to the authored LF contract without rewriting
    // either captured stream or the lesson asset.
    const auto battery_run=execute(battery,"Total=11\r\nTotal=6\r\n","Total=11\nTotal=6\n",std::string("0 6"),true);retain(battery_run);
    require(windows::compiler_puzzle_case_count(f.saved(),lesson,goal)==1,
        "The passing real battery did not create its own aggregate execution receipt");
    const auto completed_gate=[&]{
        require(f.page()==page&&lesson_page_state(f.view).can_next
            &&f.text(4,0)==lesson.pages[page].blocks[4].text
            &&std::ranges::any_of(lesson_reading_regions(f.view),[&](const auto& region){
                return region.block_index==4&&region.source_text==lesson.pages[page].blocks[4].text
                    &&region.target==f.control(4,0);}),
            "The accepted real battery did not reveal its exact post-editor note and reader region");
        const auto options=lesson_options_snapshot(f.view);
        require(std::ranges::find(options.available_pages,4)!=options.available_pages.end()
            &&std::ranges::find(options.available_pages,5)!=options.available_pages.end(),
            "The accepted real battery did not unlock both actual reference pages");
    };
    completed_gate();
    const auto draft_key=curriculum::draft_key("programming",lesson,goal);
    const auto before_references=f.saved();
    const auto battery_draft=state::get_draft(before_references,draft_key,goal.initial_code);
    state::Preferences reference_proofs;
    for(const auto& [key,value]:before_references.preferences.at("native_windows"))
        if(key.starts_with("compiler-puzzle/v1/")||key.starts_with("reference-proof/v1/"))reference_proofs.emplace(key,value);
    // Execute the native Pages action. These are the current author references,
    // not a seeded student program or an alternate stripped-down lesson.
    for(const auto target:{std::size_t{4},std::size_t{5}}){
        f.select_page(target);
        const auto regions=lesson_reading_regions(f.view);
        for(std::size_t index=0;index<lesson.pages[target].blocks.size();++index){
            const auto& reference=lesson.pages[target].blocks[index];
            require(std::ranges::find(references,reference.stable_id)!=references.end(),
                "A newly mounted reference has no independently audited relationship");
            if(reference.type==BlockType::code){
                require(f.text(index,1)==reference.code
                    &&(GetWindowLongPtrW(f.control(index,1),GWL_STYLE)&ES_READONLY)
                    &&std::ranges::any_of(regions,[&](const auto& region){return region.block_index==index
                        &&region.target==f.control(index,16)&&region.passive_label
                        &&region.source_text=="Ejemplo de código C más más.";}),
                    "An unlocked U12L2 reference lost its exact read-only source or native code caption");
            }else{
                require(reference.type==BlockType::heading||reference.type==BlockType::paragraph||reference.type==BlockType::note,
                    "A posterior U12L2 prose reference changed its supported type");
                auto visible=reference.text;std::erase(visible,'`');
                require(f.text(index,0)==visible&&std::ranges::any_of(regions,[&](const auto& region){
                    return region.block_index==index&&region.target==f.control(index,0)&&region.source_text==reference.text;}),
                    "An unlocked U12L2 reference lost its exact current prose or narration source: "+reference.stable_id);
            }
        }
        require(directories().size()==3,"Visiting an authored U12L2 reference executed a program automatically");preserve();
    }
    f.select_page(page);completed_gate();const auto returned=f.saved();
    const auto returned_draft=state::get_draft(returned,draft_key,goal.initial_code);
    require(f.text(compiler,10)==battery&&returned_draft.source==battery_draft.source
        &&returned_draft.input==battery_draft.input&&returned_draft.indentation_json==battery_draft.indentation_json
        &&returned_draft.selection_start==battery_draft.selection_start&&returned_draft.selection_end==battery_draft.selection_end
        &&windows::compiler_puzzle_case_count(returned,lesson,goal)==1&&directories().size()==3,
        "Returning from the two actual references lost the accepted battery draft or ran it automatically");
    for(const auto& [key,value]:reference_proofs){const auto retained_proof=state::find(returned,"native_windows",key);
        require(retained_proof&&*retained_proof==value,"Reference navigation rewrote the real accepted execution proof");}
    const auto repeated=execute(battery,"Total=11\r\nTotal=6\r\n","Total=11\nTotal=6\n",std::string("0 6"),true);retain(repeated);
    // Only later source edits/reopens must retain the last real completed
    // proof. A genuine repeated run is permitted to refresh its metadata.
    const auto accepted=f.saved();state::Preferences proofs;
    for(const auto& [key,value]:accepted.preferences.at("native_windows"))
        if(key.starts_with("compiler-puzzle/v1/")||key.starts_with("reference-proof/v1/"))proofs.emplace(key,value);
    require(!proofs.empty()&&windows::compiler_puzzle_case_count(accepted,lesson,goal)==1,
        "The repeated real battery lost its aggregate execution proof");
    const auto modified=battery+"// QA: borrador conservado\n";
    const auto editor=f.control(compiler,10);f.replace(compiler,10,modified);
    require(f.text(compiler,14).find("El programa cambió.")!=std::string::npos&&directories().size()==4,
        "Editing the source retained a stale live result or executed automatically");
    require(SendMessageW(editor,WM_UNDO,0,0),"The actual file editor lost its Undo transaction");pump(f.host);
    require(f.text(compiler,10)==battery,"Undo did not restore the exact authored reference source");
    require(SendMessageW(editor,EM_REDO,0,0),"The actual file editor lost its Redo transaction");pump(f.host);
    require(f.text(compiler,10)==modified,"Redo did not restore the exact edited draft");
    const CHARRANGE selection{5,17};SendMessageW(editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump(f.host);
    const auto saved=f.saved();
    const auto before=state::get_draft(saved,draft_key,goal.initial_code);
    require(before.has_saved&&before.source==modified&&before.selection_start==selection.cpMin&&before.selection_end==selection.cpMax,
        "Saving lost the actual edited file draft or selection");
    f.reopen();const auto reopened=f.saved();const auto after=state::get_draft(reopened,draft_key,goal.initial_code);
    CHARRANGE restored{};SendMessageW(f.control(compiler,10),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&restored));
    require(f.page()==page&&f.text(compiler,10)==modified&&after.source==before.source&&after.input==before.input
        &&after.indentation_json==before.indentation_json&&restored.cpMin==selection.cpMin&&restored.cpMax==selection.cpMax
        &&directories().size()==4&&windows::compiler_puzzle_completed(reopened,lesson,goal),
        "Whole-view reopen lost the edited draft, selection or prior sealed success, or ran the program automatically");
    for(const auto& [key,value]:proofs){const auto retained_proof=state::find(reopened,"native_windows",key);
        require(retained_proof&&*retained_proof==value,"Editing/reopening rewrote the previously earned real execution proof");}
    completed_gate();preserve();
    std::cout<<"PASS current U12L2 real file round-trip view: current mandatory editor and seven authored posterior references; correct preceding choice and initial/single-reference rejection keep both reference pages and the post-editor note gated, including exact rejected-source/verdict reopen without automatic execution; exact two-report battery reveals current p5/p6 sources and prose via actual navigation; repeat in four fresh directories, real file/stream bytes, no previous-run inheritance, unchanged earlier artifacts/proofs, source Undo/Redo and silent draft/selection reopen; no generated audio, author reference is QA evidence rather than student construction or listening acceptance\n";
}
