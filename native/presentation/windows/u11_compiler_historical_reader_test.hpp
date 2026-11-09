#pragma once

// Included after Harness and its actual-control/PCM helpers. These are native
// Windows runs of authentic old content, never imported Android verdicts or
// manually constructed listening/compilation receipts.
namespace compiler_historical {
struct HistoricalReaderSpec {
    const ReaderSpec& current;
    std::string_view before_sha,after_sha,lesson_id;
    std::size_t page_count;
    std::string_view original_compiler,criterion;
    curriculum::StatePosition original_reference;
    std::array<std::string_view,5> current_owners;
    std::span<const curriculum::StatePosition> prerequisites;
    std::string_view token,folder,preference,prerequisite_preference;
};
constexpr std::array<curriculum::StatePosition,5> declarations_prerequisites{{{0,3},{1,3},{3,2},{3,3},{3,4}}};
constexpr std::array<curriculum::StatePosition,6> assignments_prerequisites{{{0,3},{1,3},{2,3},{4,1},{4,2},{4,3}}};
constexpr std::array<curriculum::StatePosition,6> constants_prerequisites{{{0,3},{1,3},{2,2},{4,1},{4,2},{4,3}}};
// Exact 617f034 -> 5d115a0 publications from compiler_import_fixture_source.py.
// The current ReaderSpec supplies its existing independent program/output
// oracle, but never substitutes its live asset for either historical source.
const HistoricalReaderSpec declarations_source{
    declarations,"887dbfb3f98c0f04c92567d2d1a46d82ddb1bd3ebe761ecf60c17a905e1815a0",
    "bfaa88c3f4294c30bf50b58f96aff91fbf71ffdbdfd20c7471d1c71e358c1cb6",
    "cpp_fundamentals__texto_interpretacion_y_flujos__declaraciones_aceptadas_solo_al_completar",
    8,"block_4_2","block_0004_0001",{7,1},
    {"block_0004_0000","block_0004_0001","block_0004_0002","block_0004_0003","block_0004_0004"},
    declarations_prerequisites,"declarations-historical-reader","u11-declarations-historical-reader",
    "u11_declarations_historical_reader_qa","u11_declarations_historical_synthetic_prerequisites_qa"
};
const HistoricalReaderSpec assignments_source{
    assignments,"cde8f8a248fcc8a7eab16033f2e469932eb24929e5fff3cae3d77c688f1338f8",
    "69eef7a0633bafaea663bc5b3bfa23da2a8940cf25ecbbd8af60090587204cf9",
    "cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada",
    9,"block_5_2","block_0005_0001",{8,1},
    {"block_0005_0000","block_0005_0001","block_0005_0002","block_0005_0003","block_0005_0004"},
    assignments_prerequisites,"assignments-historical-reader","u11-assignments-historical-reader",
    "u11_assignments_historical_reader_qa","u11_assignments_historical_synthetic_prerequisites_qa"
};
const HistoricalReaderSpec constants_source{
    constants,"65b1eb97508bfb7f6dbbc695f25c8cfa27994333892878c44c297b7ccea463cc",
    "d444222f6ce6f69436279bee30e734f6fb9823ce7c6df592f6085a07734b04db",
    "cpp_fundamentals__texto_interpretacion_y_flujos__constantes_del_lenguaje_y_nombres_predefinidos",
    8,"block_5_2","block_0005_0001",{7,1},
    {"block_0005_0000","block_0005_0001","block_0005_0002","block_0005_0003","block_0005_0004"},
    constants_prerequisites,"constants-historical-reader","u11-constants-historical-reader",
    "u11_constants_historical_reader_qa","u11_constants_historical_synthetic_prerequisites_qa"
};
constexpr std::size_t old_compiler_index=2;
struct OrdinaryProof {curriculum::StatePosition position;std::string key,raw;};

const curriculum::Block& original_owner(const curriculum::Lesson& lesson,curriculum::StatePosition at){
    const curriculum::Block* found=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.address.page==at.page&&block.address.block==at.block&&block.address.addition_namespace.empty()){
            require(!found,"The historical compiler owner has duplicate original coordinates");found=&block;}
    require(found!=nullptr,"The historical compiler owner lost its original coordinates");return *found;
}

std::vector<OrdinaryProof> prepare_original_entry(state::StateData& data,const curriculum::Lesson& lesson,const HistoricalReaderSpec& source){
    require(lesson.id==source.lesson_id&&lesson.locale=="es"&&lesson.state_layout.version==0&&lesson.pages.size()==source.page_count,
        "Historical compiler entry requires its exact positional Spanish source");
    const auto expected=source.prerequisites;const auto page_index=source.current.compiler_page;
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
                "The historical compiler ordinary prerequisite set changed");
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
    data.preferences[std::string(source.prerequisite_preference)]["ordinary_only"]=std::move(disclosed);
    state::validate_lesson_state(data,lesson);return prepared;
}

bool same_draft(const state::Draft& a,const state::Draft& b){
    return a.has_saved==b.has_saved&&a.source==b.source&&a.input==b.input&&a.indentation_json==b.indentation_json
        &&a.selection_start==b.selection_start&&a.selection_end==b.selection_end;
}

void exercise(const std::shared_ptr<windows::NarrationWorker>& backend,const HistoricalReaderSpec& source,const std::filesystem::path& before_path,
        const std::filesystem::path& after_path,unsigned phase){
    require(phase<2,"Historical compiler reading has two audited phases");const bool correct=phase==1;
    const auto& selected=source.current;const auto page_index=selected.compiler_page;
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    require(state::sha256(before_bytes)==source.before_sha&&state::sha256(after_bytes)==source.after_sha,
        "Historical compiler reading needs the exact 617f034 and 5d115a0 Git documents");
    const auto before=windows::load_lesson(before_path,"es"),after=windows::load_lesson(after_path,"es");
    require(before.id==source.lesson_id&&after.id==before.id&&before.state_layout.version==0&&after.state_layout.version==2
        &&before.pages.size()==source.page_count&&after.pages.size()==source.page_count&&!before.compiler_puzzles_required&&after.compiler_puzzles_required,
        "Historical compiler reading lost its positional-to-v2 publication boundary");
    const auto& old_compiler=before.pages[page_index].blocks[old_compiler_index];
    const auto compiler_at=position(after,selected.compiler),criterion_at=position(after,source.criterion),reference_at=position(after,selected.reference);
    const auto& compiler=after.pages[compiler_at.page].blocks[compiler_at.block];
    const auto& reference=before.pages[source.original_reference.page].blocks[source.original_reference.block];
    require(before.pages[page_index].blocks.size()==5&&after.pages[page_index].blocks.size()==5
        &&old_compiler.stable_id==source.original_compiler&&compiler_at.page==page_index&&compiler_at.block==4
        &&criterion_at.page==page_index&&criterion_at.block==1
        &&old_compiler.type==curriculum::BlockType::cpp_compiler&&compiler.type==old_compiler.type
        &&old_compiler.initial_code==compiler.initial_code&&old_compiler.initial_code.size()==selected.initial_bytes
        &&old_compiler.output_check&&compiler.output_check&&old_compiler.output_check->cases.size()==1&&compiler.output_check->cases.size()==1
        &&old_compiler.output_check->cases.front().standard_input.empty()&&compiler.output_check->cases.front().standard_input.empty()
        &&old_compiler.output_check->cases.front().outputs==std::vector<std::string>{std::string(selected.output)}
        &&compiler.output_check->cases.front().outputs==old_compiler.output_check->cases.front().outputs
        &&old_compiler.output_check->cases.front().explanation.empty()&&compiler.output_check->cases.front().explanation.empty()
        &&old_compiler.output_check->cases.front().review_feedback.empty()&&compiler.output_check->cases.front().review_feedback.empty()
        &&reference.type==curriculum::BlockType::code&&reference.code.size()==selected.reference_bytes
        &&reference_at==source.original_reference&&reference.code==after.pages[reference_at.page].blocks[reference_at.block].code,
        "The historical compiler controls, program, generic verdict or independent execution oracle changed");
    constexpr std::array<std::size_t,5> order{{0,1,3,4,2}};
    for(std::size_t index=0;index<order.size();++index){const auto& block=after.pages[page_index].blocks[index];
        require(block.address.page==page_index&&block.address.block==order[index]&&block.address.addition_namespace.empty(),
            "The compiler page no longer has its authentic original-owner reordering");}
    require(before.state_layout.positional_reading_pages.empty()&&after.state_layout.positional_reading_pages.size()==1,
        "The exact compiler publication did not receive its one authenticated positional page archive");
    const auto& archived=after.state_layout.positional_reading_pages.front();
    const auto& original_page=before.pages[page_index];
    require(archived.page==page_index&&std::ranges::equal(archived.current_owners,source.current_owners)
        &&archived.original.stable_id==original_page.stable_id&&archived.original.blocks.size()==original_page.blocks.size()
        &&archived.original.narration_language==original_page.narration_language,
        "The compiler archive changed the authentic positional page, language or original-to-current ownership");
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
            "A reconstructed compiler block differs from the complete authentic positional source or native identity");
        if(original.type==curriculum::BlockType::cpp_compiler)
            require(rebuilt.output_check&&rebuilt.output_check->cases.size()==original.output_check->cases.size()
                &&rebuilt.output_check->input_comparison==original.output_check->input_comparison
                &&rebuilt.output_check->cases.front().standard_input==original.output_check->cases.front().standard_input
                &&rebuilt.output_check->cases.front().outputs==original.output_check->cases.front().outputs,
                "The positional compiler archive changed its actual compiler input/output objective");
    }
    require(before.pages[page_index].blocks[0].text==after.pages[page_index].blocks[0].text
        &&before.pages[page_index].blocks[1].text!=after.pages[page_index].blocks[1].text
        &&curriculum::exercise_key(before,old_compiler)==curriculum::exercise_key(after,compiler)
        &&curriculum::reference_objective_frame(before,old_compiler)!=curriculum::reference_objective_frame(after,compiler),
        "The compiler test no longer distinguishes compatible title, edited instructions and versioned objective identities");

    auto spec=selected;spec.token=source.token;spec.folder=source.folder;spec.preference=source.preference;
    std::vector<OrdinaryProof> prerequisites;
    std::cout<<"PHASE "<<selected.label<<" historical "<<phase<<": opening original reader\n";
    Harness f(backend,spec,before_path,source.before_sha,[&](state::StateData& data,const curriculum::Lesson& lesson){
        prerequisites=prepare_original_entry(data,lesson,source);
    });
    std::cout<<"PHASE original first recording ready; preparing complete original page\n";
    f.ready();f.privacy();
    std::cout<<"PHASE complete original page ready\n";
    const auto program=correct?reference.code:old_compiler.initial_code;
    if(correct)f.edit(old_compiler_index,10,program);
    const auto old_run=f.compile(old_compiler_index,program);
    const auto expected_feedback=correct?std::string("✓ Correcto"):std::string("✗ Incorrecto");
    std::cout<<"PHASE verifying original compiler feedback PCM\n";
    (void)feedback_parts(f,old_compiler_index,expected_feedback);
    std::cout<<"PHASE original compiler feedback PCM verified\n";
    windows::CppRunRequest capture;capture.source_utf8=program;
    const auto recorded=f.saved();
    require(windows::compiler_puzzle_completed(recorded,before,old_compiler)==correct
        &&windows::compiler_puzzle_case_count(recorded,before,old_compiler)==(correct?1u:0u)
        &&(correct?windows::compiler_puzzle_review(recorded,before,old_compiler,capture).empty()
            :!windows::compiler_puzzle_review(recorded,before,old_compiler,capture).empty()),
        "The historical verdict was not earned by its actual educational execution");
    std::string crt_output;for(const char value:selected.output){if(value=='\n')crt_output+='\r';crt_output+=value;}
    const auto verify_run=[&](const std::filesystem::path& run){const auto output=bytes(run/"execution-stdout.bin");
        require(bytes(run/"main.cpp")==program&&bytes(run/"stdin.bin").empty()&&bytes(run/"execution-stderr.bin").empty()
            &&(correct?(output==selected.output||output==crt_output):output.empty()),
            "A compiler receipt disagrees with its literal captured source/stdin/stdout/stderr");};
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
    std::cout<<"PHASE verifying complete original page PCM: "<<planned.segments.size()<<" segments\n";
    for(const auto& part:planned.segments){const auto region=std::ranges::find(source_regions,part.region,&LessonReadingRegion::key);
        require(region!=source_regions.end(),"A historical compiler recording has no actual original reading region");
        f.verify_audio(part,!region->passive_label);}
    std::cout<<"PHASE complete original page PCM verified; starting actual old passage\n";
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
    },"actual old compiler partial PCM playback");
    f.reader->pause();const auto paused=f.snapshot();f.verify_audio(paused.segments[paused.index]);
    const auto old_history=narration::read_history(f.saved());
    require(old_history.entries.size()==1&&old_history.entries.front().key==paused.key
        &&old_history.entries.front().page=="programming/"+before.id+'/'+std::to_string(page_index)
        &&old_history.entries.front().position.heard_prefix&&old_history.entries.front().position.heard_prefix->microseconds>0
        &&old_history.entries.front().position.milliseconds==paused.offset&&!paused.playing&&!paused.allocated&&!paused.finished,
        "The old compiler pause is not one released positional bookmark earned from actual PCM");

    std::cout<<"PHASE adoption begins after the actual original PCM pause\n";
    f.close();f.asset=after_path;f.original_asset=after_bytes;f.lesson=after;
    std::cout<<"PHASE opening revised page and authenticating its saved history\n";
    f.open(page_index);
    std::cout<<"PHASE revised selected recording ready; preparing complete revised page\n";
    f.ready();f.privacy();
    std::cout<<"PHASE complete revised page ready; verifying adoption\n";
    const auto mapped=f.snapshot();const auto current_target=f.control(old_passage,0);
    const auto first=std::ranges::find_if(mapped.segments,[&](const auto& part){return part.block==old_passage&&part.target==current_target&&!part.feedback;});
    require(first!=mapped.segments.end()&&(!correct||first!=mapped.segments.begin()),"The revised compiler passage is missing or aliases the title");
    const auto boundary=static_cast<std::size_t>(first-mapped.segments.begin());
    require(mapped.key!=paused.key&&mapped.index==boundary&&mapped.offset==0&&mapped.heard_through==0
        &&!mapped.playing&&!mapped.allocated&&!mapped.finished,
        "The positional compiler pause did not return to its first current passage without inherited playback");
    const auto migrated=narration::read_history(f.saved());
    require(migrated.entries.size()==1&&migrated.entries.front().key==mapped.key
        &&migrated.entries.front().page==lesson_page_state(f.view).narration_page_identity
        &&migrated.entries.front().page!=old_history.entries.front().page
        &&migrated.active_key==mapped.key&&migrated.active_owner==old_history.active_owner
        &&(!migrated.entries.front().position.heard_prefix||migrated.entries.front().position.heard_prefix->microseconds==0),
        "The positional compiler history fell back instead of rekeying its existing slot without a certificate");
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
    require(authenticated,"The positional compiler archive did not reconstruct its actual original whole-page narration key");

    bool current_execution=false;
    const auto preserve=[&]{const auto data=f.saved();f.preserved();
        require(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes
            &&same_draft(state::get_draft(data,draft_key,compiler.initial_code),saved_draft),
            "Compiler reading changed a published source or the actual independent editor draft");
        for(const auto& item:prerequisites){const auto& original=original_owner(before,item.position);const auto& current=original_owner(after,item.position);
            const auto stored=state::get_response(data,before,original),restored=state::get_response(data,after,current);
            require(state::response_storage_key(after,current)==item.key&&state::string_value(data,"learning_progress",item.key)==item.raw
                &&stored&&restored&&stored->correct&&restored->correct&&stored->raw_json==restored->raw_json,
                "Compiler adoption changed one of its disclosed ordinary prerequisite responses");}
        for(const auto& page:after.pages)for(const auto& block:page.blocks){
            const auto key=state::response_storage_key(after,block);
            if(std::ranges::none_of(prerequisites,[&](const auto& item){return item.key==key;}))
                require(!state::get_response(data,after,block),"Compiler narration invented a response for a new or unattempted activity");}
        for(const auto& [key,value]:old_receipts)require(state::find(data,"native_windows",key)&&*state::find(data,"native_windows",key)==value,
            "Compiler adoption overwrote a genuine positional execution receipt/rejection");
        require(windows::compiler_puzzle_completed(data,before,old_compiler)==correct
            &&windows::compiler_puzzle_case_count(data,before,old_compiler)==(correct?1u:0u)
            &&windows::compiler_puzzle_review(data,before,old_compiler,capture)==old_review
            &&windows::compiler_puzzle_completed(data,after,compiler)==(current_execution&&correct)
            &&windows::compiler_puzzle_case_count(data,after,compiler)==(current_execution&&correct?1u:0u),
            "Compiler adoption lost old execution evidence or credited the new objective without its own run");
        const auto current_feedback=windows::compiler_puzzle_current_feedback(data,after,compiler,capture);
        require(current_feedback.has_value()==current_execution&&(!current_feedback||current_feedback->correct==correct),
            "A positional verdict was presented as a newly executed v2 result");
        for(const auto& [file,saved]:run_files)require(state::sha256(state::read_bytes(file))==saved.sha
            &&std::filesystem::last_write_time(file)==saved.modified,"Compiler reading changed a genuine captured educational run");
    };
    preserve();f.reopen_paused(mapped);preserve();
    std::vector<std::size_t> criteria;
    const auto current=f.snapshot();
    for(std::size_t block=1;block<compiler_at.block;++block){const auto& text=after.pages[page_index].blocks[block].text;
        require(edit_text(f.control(block,0))==visible(text),"A revised compiler criterion is not fully displayed");
        prepared_prose(f,block,text);
        for(std::size_t index=0;index<current.segments.size();++index)
            if(current.segments[index].block==block&&!current.segments[index].feedback&&current.segments[index].target==f.control(block,0))criteria.push_back(index);
    }
    require(!criteria.empty()&&!lesson_page_state(f.view).can_next,"Old compiler execution bypassed the newly required objective");
    std::set<std::size_t> observed;f.reader->toggle();
    wait([&]{const auto value=f.snapshot();if(value.playing)observed.insert(value.index);
        return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==compiler_at.block;
    },"complete revised compiler criteria followed by the unproved v2 compiler gate");
    require(std::ranges::all_of(criteria,[&](auto index){return observed.contains(index);})&&!f.snapshot().finished,
        "The compiler reader skipped a revised criterion or finished an unproved objective");preserve();

    require(edit_text(f.control(compiler_at.block,10))==program,"The current editor did not restore the actual historical draft");
    const auto new_run=f.compile(compiler_at.block,program);require(new_run!=old_run,"The current objective reused an old run directory");
    verify_run(new_run);retain_run(new_run);current_execution=true;
    const auto feedback=feedback_parts(f,compiler_at.block,expected_feedback);const auto fresh=f.snapshot();
    require(fresh.index==feedback.front()&&fresh.offset==0&&!fresh.playing&&!fresh.allocated&&!fresh.finished,
        "The new genuine compiler verdict began playback or inherited listening");
    preserve();f.reopen_paused(fresh);preserve();observed.clear();f.reader->toggle();
    wait([&]{const auto value=f.snapshot();if(value.playing)observed.insert(value.index);
        return std::ranges::all_of(feedback,[&](auto index){return observed.contains(index);})
            &&!value.playing&&!value.allocated&&!value.preparing&&value.finished==correct
            &&value.index==feedback.back()&&value.offset+1>=value.segments[value.index].duration_ms;
    },"actual complete current compiler verdict and its success/retry boundary");
    require(lesson_page_state(f.view).can_next==correct,"The current compiler verdict did not enforce its own advancement result");preserve();
    std::cout<<"PASS "<<selected.label<<" historical compiler reader phase "<<phase
        <<": authentic 617f034 positional source to 5d115a0 v2; "<<prerequisites.size()<<" declared synthetic ordinary prerequisites; actual old/new "
        <<(correct?"successful":"rejected")<<" educational runs; genuine old "<<(correct?"edited-instruction":"compatible-title")
        <<" PCM pause rekeyed in place at zero hearing, complete current criteria and fresh verdict playback; exact silent v2 reopens, "
        <<"old receipts/draft/responses/run files/WAV retained. No Android XML or historical Android audio import, human listening or full-lesson acceptance claimed.\n";
}
} // namespace compiler_historical
