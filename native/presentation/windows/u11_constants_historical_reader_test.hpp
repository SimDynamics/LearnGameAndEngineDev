#pragma once

// Included after Harness and its actual-control/PCM helpers. These are native
// Windows runs of authentic old content, never imported Android verdicts or
// manually constructed listening/compilation receipts.
namespace constants_historical {
constexpr std::string_view before_sha="65b1eb97508bfb7f6dbbc695f25c8cfa27994333892878c44c297b7ccea463cc";
constexpr std::string_view after_sha="d444222f6ce6f69436279bee30e734f6fb9823ce7c6df592f6085a07734b04db";
constexpr std::string_view lesson_id="cpp_fundamentals__texto_interpretacion_y_flujos__constantes_del_lenguaje_y_nombres_predefinidos";
constexpr std::size_t page_index=5,old_compiler_index=2;
struct OrdinaryProof {curriculum::StatePosition position;std::string key,raw;};

const curriculum::Block& original_owner(const curriculum::Lesson& lesson,curriculum::StatePosition at){
    const curriculum::Block* found=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.address.page==at.page&&block.address.block==at.block&&block.address.addition_namespace.empty()){
            require(!found,"The historical constants owner has duplicate original coordinates");found=&block;}
    require(found!=nullptr,"The historical constants owner lost its original coordinates");return *found;
}

std::vector<OrdinaryProof> prepare_original_entry(state::StateData& data,const curriculum::Lesson& lesson){
    require(lesson.id==lesson_id&&lesson.locale=="es"&&lesson.state_layout.version==0&&lesson.pages.size()==8,
        "Historical constants entry requires its exact positional Spanish source");
    constexpr std::array<curriculum::StatePosition,6> expected{{{0,3},{1,3},{2,2},{4,1},{4,2},{4,3}}};
    std::vector<OrdinaryProof> prepared;
    for(std::size_t page=0;page<=page_index;++page){
        require(state::can_reach_page(data,lesson,page),"The positional QA entry skipped an unvisited page");
        state::save_current_page(data,lesson,page);
        if(page==page_index)break;
        for(std::size_t index=0;index<lesson.pages[page].blocks.size();++index){const auto& block=lesson.pages[page].blocks[index];
            require(block.type!=curriculum::BlockType::cpp_compiler,"A historical prerequisite compiler needs an actual run");
            if(block.type==curriculum::BlockType::heading||block.type==curriculum::BlockType::paragraph||block.type==curriculum::BlockType::code)continue;
            require(prepared.size()<expected.size()&&expected[prepared.size()].page==page&&expected[prepared.size()].block==index
                &&test::compiler_reader_ordinary(block.type)&&!state::get_response(data,lesson,block),
                "The historical constants ordinary prerequisite set changed");
            state::Response response;curriculum::AnswerEvaluation evaluated;
            if(block.type==curriculum::BlockType::multiple_choice){
                require(block.correct_index&&*block.correct_index<block.options.size(),"Missing historical correct choice");
                response.selected=static_cast<std::int64_t>(*block.correct_index);response.answer=block.options[*block.correct_index];
                evaluated=curriculum::evaluate_choice(block,*block.correct_index);
            }else if(block.type==curriculum::BlockType::order_code){
                response.order.assign(block.correct_order.begin(),block.correct_order.end());
                evaluated=curriculum::evaluate_order(block,block.correct_order);
            }else{
                require(!block.answers.empty(),"Missing historical ordinary answer");response.answer=block.answers.front();
                evaluated=curriculum::evaluate_text_answer(block,response.answer);
            }
            require(evaluated.correct,"A declared synthetic ordinary prerequisite failed its actual evaluator");
            response.correct=true;state::mark_response_reviewed(response,block);state::put_response(data,lesson,block,response);
            const auto key=state::response_storage_key(lesson,block);
            prepared.push_back({{page,index},key,state::string_value(data,"learning_progress",key)});
        }
    }
    require(prepared.size()==expected.size()&&narration::read_history(data).entries.empty()
        &&!data.preferences.contains("native_windows"),"The old entry fabricated execution, listening or incomplete prerequisites");
    std::vector<std::string> disclosed;for(const auto& item:prepared)disclosed.push_back(item.key);
    data.preferences["u11_constants_historical_synthetic_prerequisites_qa"]["ordinary_only"]=std::move(disclosed);
    state::validate_lesson_state(data,lesson);return prepared;
}

bool same_draft(const state::Draft& a,const state::Draft& b){
    return a.has_saved==b.has_saved&&a.source==b.source&&a.input==b.input&&a.indentation_json==b.indentation_json
        &&a.selection_start==b.selection_start&&a.selection_end==b.selection_end;
}

void exercise(const std::shared_ptr<windows::NarrationWorker>& backend,const std::filesystem::path& before_path,
        const std::filesystem::path& after_path,unsigned phase){
    require(phase<2,"Historical constants reading has two audited phases");const bool correct=phase==1;
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    require(state::sha256(before_bytes)==before_sha&&state::sha256(after_bytes)==after_sha,
        "Historical constants reading needs the exact 617f034 and 5d115a0 Git documents");
    const auto before=windows::load_lesson(before_path,"es"),after=windows::load_lesson(after_path,"es");
    require(before.id==lesson_id&&after.id==before.id&&before.state_layout.version==0&&after.state_layout.version==2
        &&before.pages.size()==8&&after.pages.size()==8&&!before.compiler_puzzles_required&&after.compiler_puzzles_required,
        "Historical constants reading lost its positional-to-v2 publication boundary");
    const auto& old_compiler=before.pages[page_index].blocks[old_compiler_index];
    const auto compiler_at=position(after,"block_0005_0002"),criterion_at=position(after,"block_0005_0001");
    const auto& compiler=after.pages[compiler_at.page].blocks[compiler_at.block];
    const auto& reference=before.pages[7].blocks[1];
    require(before.pages[page_index].blocks.size()==5&&after.pages[page_index].blocks.size()==5
        &&old_compiler.stable_id=="block_5_2"&&compiler_at.page==page_index&&compiler_at.block==4
        &&criterion_at.page==page_index&&criterion_at.block==1
        &&old_compiler.type==curriculum::BlockType::cpp_compiler&&compiler.type==old_compiler.type
        &&old_compiler.initial_code==compiler.initial_code&&old_compiler.initial_code.size()==constants.initial_bytes
        &&old_compiler.output_check&&compiler.output_check&&old_compiler.output_check->cases.size()==1&&compiler.output_check->cases.size()==1
        &&old_compiler.output_check->cases.front().standard_input.empty()&&compiler.output_check->cases.front().standard_input.empty()
        &&old_compiler.output_check->cases.front().outputs==std::vector<std::string>{std::string(constants_output)}
        &&compiler.output_check->cases.front().outputs==old_compiler.output_check->cases.front().outputs
        &&old_compiler.output_check->cases.front().explanation.empty()&&compiler.output_check->cases.front().explanation.empty()
        &&old_compiler.output_check->cases.front().review_feedback.empty()&&compiler.output_check->cases.front().review_feedback.empty()
        &&reference.type==curriculum::BlockType::code&&reference.code.size()==constants.reference_bytes
        &&reference.code==after.pages[7].blocks[1].code,
        "The historical constants controls, program, generic verdict or independent execution oracle changed");
    constexpr std::array<std::size_t,5> order{{0,1,3,4,2}};
    for(std::size_t index=0;index<order.size();++index){const auto& block=after.pages[page_index].blocks[index];
        require(block.address.page==page_index&&block.address.block==order[index]&&block.address.addition_namespace.empty(),
            "The constants compiler page no longer has its authentic original-owner reordering");}
    require(before.state_layout.positional_reading_pages.empty()&&after.state_layout.positional_reading_pages.size()==1,
        "The exact constants publication did not receive its one authenticated positional page archive");
    const auto& archived=after.state_layout.positional_reading_pages.front();
    const auto& original_page=before.pages[page_index];
    require(archived.page==page_index&&archived.current_owners==std::vector<std::string>{"block_0005_0000","block_0005_0001",
            "block_0005_0002","block_0005_0003","block_0005_0004"}
        &&archived.original.stable_id==original_page.stable_id&&archived.original.blocks.size()==original_page.blocks.size()
        &&archived.original.narration_language==original_page.narration_language,
        "The constants archive changed the authentic positional page, language or original-to-current ownership");
    const auto fingerprint=[](const std::string& raw){return test::compile_check_test_fingerprint(
        winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw)));};
    for(std::size_t index=0;index<original_page.blocks.size();++index){const auto& original=original_page.blocks[index];
        const auto& rebuilt=archived.original.blocks[index];
        require(rebuilt.stable_id==original.stable_id&&rebuilt.id==original.id&&rebuilt.type==original.type
            &&rebuilt.address.page==original.address.page&&rebuilt.address.block==original.address.block
            &&rebuilt.address.addition_namespace==original.address.addition_namespace&&rebuilt.address.progress_page==original.address.progress_page
            &&rebuilt.text==original.text&&rebuilt.code==original.code&&rebuilt.initial_code==original.initial_code
            &&fingerprint(rebuilt.raw_json)==fingerprint(original.raw_json)
            &&(!curriculum::has_compiler_objective(original)
                ||curriculum::reference_objective_frame(before,rebuilt)==curriculum::reference_objective_frame(before,original)),
            "A reconstructed constants block differs from the complete authentic positional source or native identity");
        if(original.type==curriculum::BlockType::cpp_compiler)
            require(rebuilt.output_check&&rebuilt.output_check->cases.size()==original.output_check->cases.size()
                &&rebuilt.output_check->input_comparison==original.output_check->input_comparison
                &&rebuilt.output_check->cases.front().standard_input==original.output_check->cases.front().standard_input
                &&rebuilt.output_check->cases.front().outputs==original.output_check->cases.front().outputs,
                "The positional constants archive changed its actual compiler input/output objective");
    }
    require(before.pages[page_index].blocks[0].text==after.pages[page_index].blocks[0].text
        &&before.pages[page_index].blocks[1].text!=after.pages[page_index].blocks[1].text
        &&curriculum::exercise_key(before,old_compiler)==curriculum::exercise_key(after,compiler)
        &&curriculum::reference_objective_frame(before,old_compiler)!=curriculum::reference_objective_frame(after,compiler),
        "The constants test no longer distinguishes compatible title, edited instructions and versioned objective identities");

    auto spec=constants;spec.token="constants-historical-reader";spec.folder="u11-constants-historical-reader";
    spec.preference="u11_constants_historical_reader_qa";
    std::vector<OrdinaryProof> prerequisites;
    Harness f(backend,spec,before_path,before_sha,[&](state::StateData& data,const curriculum::Lesson& lesson){
        prerequisites=prepare_original_entry(data,lesson);
    });
    f.ready();f.privacy();
    const auto program=correct?reference.code:old_compiler.initial_code;
    if(correct)f.edit(old_compiler_index,10,program);
    const auto old_run=f.compile(old_compiler_index,program);
    const auto expected_feedback=correct?std::string("✓ Correcto"):std::string("✗ Incorrecto");
    (void)feedback_parts(f,old_compiler_index,expected_feedback);
    windows::CppRunRequest capture;capture.source_utf8=program;
    const auto recorded=f.saved();
    require(windows::compiler_puzzle_completed(recorded,before,old_compiler)==correct
        &&windows::compiler_puzzle_case_count(recorded,before,old_compiler)==(correct?1u:0u)
        &&(correct?windows::compiler_puzzle_review(recorded,before,old_compiler,capture).empty()
            :!windows::compiler_puzzle_review(recorded,before,old_compiler,capture).empty()),
        "The historical verdict was not earned by its actual educational execution");
    std::string crt_output;for(const char value:constants_output){if(value=='\n')crt_output+='\r';crt_output+=value;}
    const auto verify_run=[&](const std::filesystem::path& run){const auto output=bytes(run/"execution-stdout.bin");
        require(bytes(run/"main.cpp")==program&&bytes(run/"stdin.bin").empty()&&bytes(run/"execution-stderr.bin").empty()
            &&(correct?(output==constants_output||output==crt_output):output.empty()),
            "A constants compiler receipt disagrees with its literal captured source/stdin/stdout/stderr");};
    verify_run(old_run);
    std::map<std::filesystem::path,Harness::Recording> run_files;
    const auto retain_run=[&](const std::filesystem::path& run){for(const auto name:{"main.cpp","stdin.bin","execution-stdout.bin","execution-stderr.bin"}){
        const auto path=run/name;run_files.emplace(path,Harness::Recording{state::sha256(state::read_bytes(path)),std::filesystem::last_write_time(path)});}};
    retain_run(old_run);
    const auto draft_key=curriculum::draft_key("programming",before,old_compiler);
    require(draft_key==curriculum::draft_key("programming",after,compiler),"The positional draft key changed at adoption");
    const auto saved_draft=state::get_draft(recorded,draft_key,old_compiler.initial_code);
    require(saved_draft.has_saved&&saved_draft.source==program&&saved_draft.input.empty(),"The real old editor did not retain its own draft");
    const auto old_review=windows::compiler_puzzle_review(recorded,before,old_compiler,capture);
    const auto old_receipt_base="compiler-puzzle/v1/"+before.locale+'/'+before.id+'/'+curriculum::exercise_key(before,old_compiler)+'/'
        +state::sha256(curriculum::reference_objective_frame(before,old_compiler))+'/';
    state::Preferences old_receipts;
    for(const auto& [key,value]:recorded.preferences.at("native_windows"))if(key.starts_with(old_receipt_base))old_receipts.emplace(key,value);
    require(!old_receipts.empty(),"The historical execution has no independently persisted native evidence");

    f.reader->restart();f.reader->pause();const auto planned=f.snapshot();
    const auto source_regions=lesson_reading_regions(f.view);
    for(const auto& part:planned.segments){const auto region=std::ranges::find(source_regions,part.region,&LessonReadingRegion::key);
        require(region!=source_regions.end(),"A historical constants recording has no actual original reading region");
        f.verify_audio(part,!region->passive_label);}
    const std::size_t old_passage=correct?1u:0u;const auto target=f.control(old_passage,0);
    std::vector<std::size_t> candidates;
    for(std::size_t index=0;index<planned.segments.size();++index){const auto& part=planned.segments[index];
        if(part.block==old_passage&&part.target==target&&!part.feedback)candidates.push_back(index);}
    require(candidates.size()>(correct?1u:0u),"The authentic old passage lacks its selected spoken sentence");
    const auto wanted=candidates[correct?1u:0u];double preceding=0;
    for(std::size_t index=0;index<wanted;++index)preceding+=planned.segments[index].duration_ms;
    f.reader->seek_seconds(static_cast<int>(std::floor(preceding/1000.)));f.reader->toggle();
    wait([&]{const auto value=f.snapshot();require(value.index<=wanted,"Historical playback skipped the selected source sentence");
        return value.playing&&value.index==wanted&&value.offset>150&&value.offset+200<value.segments[value.index].duration_ms;
    },"actual old constants partial PCM playback");
    f.reader->pause();const auto paused=f.snapshot();f.verify_audio(paused.segments[paused.index]);
    const auto old_history=narration::read_history(f.saved());
    require(old_history.entries.size()==1&&old_history.entries.front().key==paused.key
        &&old_history.entries.front().page=="programming/"+before.id+"/5"
        &&old_history.entries.front().position.heard_prefix&&old_history.entries.front().position.heard_prefix->microseconds>0
        &&old_history.entries.front().position.milliseconds==paused.offset&&!paused.playing&&!paused.allocated&&!paused.finished,
        "The old constants pause is not one released positional bookmark earned from actual PCM");

    f.close();f.asset=after_path;f.original_asset=after_bytes;f.lesson=after;f.open(page_index);f.ready();f.privacy();
    const auto mapped=f.snapshot();const auto current_target=f.control(old_passage,0);
    const auto first=std::ranges::find_if(mapped.segments,[&](const auto& part){return part.block==old_passage&&part.target==current_target&&!part.feedback;});
    require(first!=mapped.segments.end()&&(!correct||first!=mapped.segments.begin()),"The revised constants passage is missing or aliases the title");
    const auto boundary=static_cast<std::size_t>(first-mapped.segments.begin());
    require(mapped.key!=paused.key&&mapped.index==boundary&&mapped.offset==0&&mapped.heard_through==0
        &&!mapped.playing&&!mapped.allocated&&!mapped.finished,
        "The positional constants pause did not return to its first current passage without inherited playback");
    const auto migrated=narration::read_history(f.saved());
    require(migrated.entries.size()==1&&migrated.entries.front().key==mapped.key
        &&migrated.entries.front().page==lesson_page_state(f.view).narration_page_identity
        &&migrated.entries.front().page!=old_history.entries.front().page
        &&migrated.active_key==mapped.key&&migrated.active_owner==old_history.active_owner
        &&(!migrated.entries.front().position.heard_prefix||migrated.entries.front().position.heard_prefix->microseconds==0),
        "The positional constants history fell back instead of rekeying its existing slot without a certificate");
    // Authenticate the whole old dynamic script, including the old verdict and
    // the two trailing paragraphs present only after the successful old run.
    bool authenticated=false;
    for(const auto& archive:editorial_lesson_reading_archives(f.view)){
        if(!archive.positional_page_identity||archive.page_identity!=old_history.entries.front().page
            ||archive.regions.size()!=source_regions.size())continue;
        bool sources=true;std::vector<windows::NarrationSegment> script;
        for(std::size_t index=0;index<archive.regions.size();++index){const auto& region=archive.regions[index];const auto& original=source_regions[index];
            sources=sources&&region.source_text==original.source_text&&region.reading_context==original.reading_context
                &&region.math_mode==original.math_mode&&region.passive_label==original.passive_label;
            const auto sentences=backend->plan(region.source_text,"es",{},region.reading_context,region.math_mode,
                windows::NarrationOutputDataPolicy::current,region.text_semantics);
            script.insert(script.end(),sentences.begin(),sentences.end());
        }
        if(sources&&backend->identity(archive.page_identity,"es","ef_dora",script)==paused.key)authenticated=true;
    }
    require(authenticated,"The positional constants archive did not reconstruct its actual original whole-page narration key");

    bool current_execution=false;
    const auto preserve=[&]{const auto data=f.saved();f.preserved();
        require(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes
            &&same_draft(state::get_draft(data,draft_key,compiler.initial_code),saved_draft),
            "Constants reading changed a published source or the actual independent editor draft");
        for(const auto& item:prerequisites){const auto& original=original_owner(before,item.position);const auto& current=original_owner(after,item.position);
            const auto stored=state::get_response(data,before,original),restored=state::get_response(data,after,current);
            require(state::response_storage_key(after,current)==item.key&&state::string_value(data,"learning_progress",item.key)==item.raw
                &&stored&&restored&&stored->correct&&restored->correct&&stored->raw_json==restored->raw_json,
                "Constants adoption changed one of its six disclosed ordinary prerequisite responses");}
        for(const auto& page:after.pages)for(const auto& block:page.blocks){
            const auto key=state::response_storage_key(after,block);
            if(std::ranges::none_of(prerequisites,[&](const auto& item){return item.key==key;}))
                require(!state::get_response(data,after,block),"Constants narration invented a response for a new or unattempted activity");}
        for(const auto& [key,value]:old_receipts)require(state::find(data,"native_windows",key)&&*state::find(data,"native_windows",key)==value,
            "Constants adoption overwrote a genuine positional execution receipt/rejection");
        require(windows::compiler_puzzle_completed(data,before,old_compiler)==correct
            &&windows::compiler_puzzle_case_count(data,before,old_compiler)==(correct?1u:0u)
            &&windows::compiler_puzzle_review(data,before,old_compiler,capture)==old_review
            &&windows::compiler_puzzle_completed(data,after,compiler)==(current_execution&&correct)
            &&windows::compiler_puzzle_case_count(data,after,compiler)==(current_execution&&correct?1u:0u),
            "Constants adoption lost old execution evidence or credited the new objective without its own run");
        const auto current_feedback=windows::compiler_puzzle_current_feedback(data,after,compiler,capture);
        require(current_feedback.has_value()==current_execution&&(!current_feedback||current_feedback->correct==correct),
            "A positional verdict was presented as a newly executed v2 result");
        for(const auto& [file,saved]:run_files)require(state::sha256(state::read_bytes(file))==saved.sha
            &&std::filesystem::last_write_time(file)==saved.modified,"Constants reading changed a genuine captured educational run");
    };
    preserve();f.reopen_paused(mapped);preserve();
    std::vector<std::size_t> criteria;
    const auto current=f.snapshot();
    for(std::size_t block=1;block<compiler_at.block;++block){const auto& text=after.pages[page_index].blocks[block].text;
        require(edit_text(f.control(block,0))==visible(text),"A revised constants criterion is not fully displayed");
        prepared_prose(f,block,text);
        for(std::size_t index=0;index<current.segments.size();++index)
            if(current.segments[index].block==block&&!current.segments[index].feedback&&current.segments[index].target==f.control(block,0))criteria.push_back(index);
    }
    require(!criteria.empty()&&!lesson_page_state(f.view).can_next,"Old constants execution bypassed the newly required objective");
    std::set<std::size_t> observed;f.reader->toggle();
    wait([&]{const auto value=f.snapshot();if(value.playing)observed.insert(value.index);
        return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==compiler_at.block;
    },"complete revised constants criteria followed by the unproved v2 compiler gate");
    require(std::ranges::all_of(criteria,[&](auto index){return observed.contains(index);})&&!f.snapshot().finished,
        "The constants reader skipped a revised criterion or finished an unproved objective");preserve();

    require(edit_text(f.control(compiler_at.block,10))==program,"The current editor did not restore the actual historical draft");
    const auto new_run=f.compile(compiler_at.block,program);require(new_run!=old_run,"The current objective reused an old run directory");
    verify_run(new_run);retain_run(new_run);current_execution=true;
    const auto feedback=feedback_parts(f,compiler_at.block,expected_feedback);const auto fresh=f.snapshot();
    require(fresh.index==feedback.front()&&fresh.offset==0&&!fresh.playing&&!fresh.allocated&&!fresh.finished,
        "The new genuine constants verdict began playback or inherited listening");
    preserve();f.reopen_paused(fresh);preserve();observed.clear();f.reader->toggle();
    wait([&]{const auto value=f.snapshot();if(value.playing)observed.insert(value.index);
        return std::ranges::all_of(feedback,[&](auto index){return observed.contains(index);})
            &&!value.playing&&!value.allocated&&!value.preparing&&value.finished==correct
            &&value.index==feedback.back()&&value.offset+1>=value.segments[value.index].duration_ms;
    },"actual complete current constants verdict and its success/retry boundary");
    require(lesson_page_state(f.view).can_next==correct,"The current constants verdict did not enforce its own advancement result");preserve();
    std::cout<<"PASS U11L21 historical constants reader phase "<<phase
        <<": authentic 617f034 positional source to 5d115a0 v2; six declared synthetic ordinary prerequisites; actual old/new "
        <<(correct?"successful":"rejected")<<" educational runs; genuine old "<<(correct?"edited-instruction":"compatible-title")
        <<" PCM pause rekeyed in place at zero hearing, complete current criteria and fresh verdict playback; exact silent v2 reopens, "
        <<"old receipts/draft/responses/run files/WAV retained. No Android XML or historical Android audio import, human listening or full-lesson acceptance claimed.\n";
}
} // namespace constants_historical
