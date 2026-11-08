#pragma once

// Four differently shaped positional pages. The six L19--L21 cases keep their
// existing implementation; only actual-control/PCM helpers are shared here.
namespace remaining_compiler_historical {
using compiler_historical::OrdinaryProof;
using compiler_historical::original_owner;
using compiler_historical::same_draft;
using Position=curriculum::StatePosition;
struct Owner {
    std::string_view id;
    Position original,current,reference_original,reference_current;
    std::size_t original_criterion,current_criterion;
    std::vector<std::size_t> order,before_prose,after_prose;
    std::vector<Position> prerequisites;
    std::string_view starter_sha,reference_sha,output_sha;
    std::size_t starter_bytes,reference_bytes,output_bytes;
};
struct Source {
    std::string_view label,lesson_id,before_sha,after_sha,token,folder,preference;
    std::size_t pages;
    bool characters;
    std::array<Owner,2> owners;
};
const Source session{
    "U11L22","cpp_fundamentals__texto_interpretacion_y_flujos__construir_una_sesion_con_nombres_y_estado",
    "02556412590ab4bd53c86eff1074ba2b52145fcf9a4d7d2bee771cd4af806bf7",
    "1932b3c86cabf23bf02154eb2d0371c3b8e7fe48fc01e42e1d559b3220cb1696",
    "session-historical-reader","u11-session-historical-reader","u11_session_historical_reader_qa",10,false,{{
        {"block_0003_0001",{3,1},{3,4},{8,1},{8,1},2,1,{0,2,3,4,1},{1,2,3},{},{{1,4},{2,4}},
            "593a8b8831537bf2f91652148f496ca3ce59877597398d05d43d94109e112dc7",
            "9c376181ebadf671ea0ca95457fb9c668e1bdfd9ba82553b79feefc8e156136d",
            "20a5d2ae4850389285d569572b4222aaaa11553716056212d8368aa64259bfd4",230,14045,2504},
        {"block_0006_0003",{6,3},{6,3},{9,1},{9,1},2,2,{0,1,2,3},{2},{},{{1,4},{2,4},{4,1},{4,2},{6,1}},
            "26b8992fdfc9ff7c82c8c18c98130dfcaf4d06375b144867b878fb1976011318",
            "7eb880af297120043fdf6c2049ed8162c0c88fcf81d0e690aaaaa774784ee249",
            "fbbdf61b5a97fa2796cf34eab2b78f775010339ea5ec41e3a37890c1fe734d00",13224,13383,620}
    }}
};
const Source characters{
    "U12L20","cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia",
    "f2e04cdddc482d708f29ef5954d388cc68a9d2d69c69345430b0585afed04789",
    "744739618d59f751567445eedbfa97fca20ce300ce61f3061d33eea610870532",
    "characters-historical-reader","u12-characters-historical-reader","u12_characters_historical_reader_qa",11,true,{{
        {"block_0005_0004",{5,4},{5,5},{8,1},{7,1},1,1,{0,1,2,3,6,4,5},{1,2,3,4},{6},
            {{0,5},{0,6},{1,3},{1,5},{1,6},{2,3},{2,4},{2,5},{3,4},{3,5},{4,4},{4,6}},
            "5b5d9a36e9c9b94ae1719daa09ded0942108b6942c82aa0ccc4580f1da2d8584",
            "532ede77d6450b3fe22b1a7f95a118fcf4e5c0062796be66e01dae053aea18c5",
            "b70c4f3d4868a39d6cb96b98ef77ff711d0cfd777ef74621073c8d96cadbf852",160,2594,802},
        {"block_0009_0002",{9,2},{9,2},{10,1},{10,1},1,1,{0,1,2},{1},{},
            {{0,5},{0,6},{1,3},{1,5},{1,6},{2,3},{2,4},{2,5},{3,4},{3,5},{4,4},{4,6},{7,1},{7,2},{7,4}},
            "1f6e465ea4079dfc144def9a2fe856649f07920bf6f81193b71591057c73a164",
            "fc2ce4fb5b55ed73a7db9c815d055dd319112c49b4189e128f23b3de9702ae4c",
            "0e72aa89f954e384f1bc0a08b5a71db1fb2b5e0c3f93e01fae119476f85b6106",142,552,22}
    }}
};

const curriculum::Block& at(const curriculum::Lesson& lesson,Position p){return lesson.pages.at(p.page).blocks.at(p.block);}
std::string crt(std::string_view value){std::string result;for(const char c:value){if(c=='\n')result+='\r';result+=c;}return result;}
std::string fingerprint(const std::string& raw){return test::compile_check_test_fingerprint(
    winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(raw)));}

std::string binary_prerequisite(const Source& source,const curriculum::Lesson& before){
    // Only the first U12 objective needed to reach the second old page: its
    // authored CR bytes deliberately disallow Windows text-mode translation.
    // Keep the complete real algorithm. Do not print a canned output oracle.
    require(source.characters,"Only the authenticated U12 prerequisite needs binary stdout QA");
    const auto& ref=at(before,source.owners[0].reference_original).code;
    require(ref.size()==2594&&state::sha256(ref)==source.owners[0].reference_sha,
        "Binary prerequisite needs the complete original U12 reference");
    constexpr std::string_view prefix="#include <cstdio>\n#include <fcntl.h>\n#include <io.h>\n";
    constexpr std::string_view main="int main() {\n";
    constexpr std::string_view call="    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 86; // QA transport only\n";
    const auto entry=ref.find(main);
    require(entry!=ref.npos&&ref.find(main,entry+main.size())==ref.npos&&ref.find("_setmode")==ref.npos,
        "Binary prerequisite has an ambiguous original main");
    auto program=std::string(prefix)+ref;const auto insertion=prefix.size()+entry+main.size();program.insert(insertion,call);
    auto restored=program;restored.erase(insertion,call.size());restored.erase(0,prefix.size());
    require(restored==ref,"Binary QA changed the authored algorithm beyond its exact stdout-mode prelude");
    return program;
}

// Sequential QA admission stops at an unproved compiler. Only ordinary
// exercises are populated synthetically, using the authentic old evaluator.
std::size_t prepare_entry(state::StateData& data,const curriculum::Lesson& lesson,const Owner& target,
        std::vector<OrdinaryProof>& proofs){
    require(lesson.state_layout.version==0,"Historical entry must be prepared against the actual positional document");
    const auto finish=[&](std::size_t page){std::vector<std::string> disclosed;for(const auto& proof:proofs)disclosed.push_back(proof.key);
        data.preferences["remaining_compiler_synthetic_prerequisites_qa"]["ordinary_only"]=std::move(disclosed);
        state::validate_lesson_state(data,lesson);return page;};
    std::size_t expected=0;
    for(std::size_t page=0;page<=target.original.page;++page){
        require(state::can_reach_page(data,lesson,page),"A historical QA prerequisite skipped sequential arrival");
        state::save_current_page(data,lesson,page);
        const auto limit=page==target.original.page?target.original.block:lesson.pages[page].blocks.size();
        for(std::size_t index=0;index<limit;++index){const auto& block=lesson.pages[page].blocks[index];
            if(block.type==curriculum::BlockType::cpp_compiler){
                if(!windows::compiler_puzzle_completed(data,lesson,block))return finish(page);
                continue;
            }
            if(block.type==curriculum::BlockType::heading||block.type==curriculum::BlockType::paragraph
                ||block.type==curriculum::BlockType::note||block.type==curriculum::BlockType::code)continue;
            require(expected<target.prerequisites.size()&&target.prerequisites[expected++]==Position{page,index}
                &&test::compiler_reader_ordinary(block.type),"The historical ordinary prerequisite set changed");
            const auto key=state::response_storage_key(lesson,block);
            if(const auto response=state::get_response(data,lesson,block)){
                const auto known=std::ranges::find(proofs,key,&OrdinaryProof::key);
                require(known!=proofs.end()&&response->correct&&state::string_value(data,"learning_progress",key)==known->raw,
                    "QA entry would overwrite an existing response");continue;
            }
            state::Response response;curriculum::AnswerEvaluation evaluation;
            if(block.type==curriculum::BlockType::multiple_choice){
                require(block.correct_index&&*block.correct_index<block.options.size(),"Missing original QA choice");
                response.selected=static_cast<std::int64_t>(*block.correct_index);response.answer=block.options[*block.correct_index];
                evaluation=curriculum::evaluate_choice(block,*block.correct_index);
            }else if(block.type==curriculum::BlockType::order_code){
                response.order.assign(block.correct_order.begin(),block.correct_order.end());evaluation=curriculum::evaluate_order(block,block.correct_order);
            }else{require(!block.answers.empty(),"Missing original QA answer");response.answer=block.answers.front();
                evaluation=curriculum::evaluate_text_answer(block,response.answer);}
            require(evaluation.correct,"An original prerequisite failed its real authored evaluator");
            response.correct=true;state::mark_response_reviewed(response,block);state::put_response(data,lesson,block,response);
            proofs.push_back({{page,index},key,state::string_value(data,"learning_progress",key)});
        }
    }
    require(expected==target.prerequisites.size()&&proofs.size()==expected,"Historical QA entry omitted an ordinary prerequisite");
    return finish(target.original.page);
}

void authenticate(const Source& source,const curriculum::Lesson& before,const curriculum::Lesson& after){
    require(before.id==source.lesson_id&&after.id==before.id&&before.state_layout.version==0&&after.state_layout.version==2
        &&before.pages.size()==source.pages&&after.pages.size()==source.pages&&!before.compiler_puzzles_required&&after.compiler_puzzles_required
        &&before.state_layout.positional_reading_pages.empty()&&after.state_layout.positional_reading_pages.size()==2,
        "Historical remaining owners need their complete two-page positional adoption");
    for(std::size_t number=0;number<source.owners.size();++number){const auto& owner=source.owners[number];
        const auto& old=at(before,owner.original);const auto& now=at(after,owner.current);
        const auto& ref=at(before,owner.reference_original);const auto& current_ref=at(after,owner.reference_current);
        require(position(after,owner.id)==owner.current&&old.type==curriculum::BlockType::cpp_compiler&&now.type==old.type
            &&old.initial_code==now.initial_code&&old.initial_code.size()==owner.starter_bytes&&state::sha256(old.initial_code)==owner.starter_sha
            &&ref.type==curriculum::BlockType::code&&ref.code==current_ref.code&&ref.code.size()==owner.reference_bytes&&state::sha256(ref.code)==owner.reference_sha
            &&old.output_check&&now.output_check&&old.output_check->cases.size()==1&&now.output_check->cases.size()==1,
            "An authentic remaining compiler/reference was replaced or moved to another owner");
        const auto& old_case=old.output_check->cases.front();const auto& current_case=now.output_check->cases.front();
        require(old_case.standard_input.empty()&&current_case.standard_input.empty()&&old_case.outputs.size()==1
            &&old_case.outputs.front().size()==owner.output_bytes&&state::sha256(old_case.outputs.front())==owner.output_sha
            &&old_case.explanation.empty()&&old_case.review_feedback.empty()
            &&current_case.outputs==(source.characters&&number==0
                ?std::vector<std::string>{old_case.outputs.front(),crt(old_case.outputs.front())}:old_case.outputs)
            &&curriculum::draft_key("programming",before,old)==curriculum::draft_key("programming",after,now)
            &&curriculum::reference_objective_frame(before,old)!=curriculum::reference_objective_frame(after,now),
            "The remaining compiler lost its exact original oracle, CRT alternative or separate native identity");
        const auto& original_page=before.pages[owner.original.page];const auto& current_page=after.pages[owner.current.page];
        require(original_page.blocks.size()==owner.order.size()&&current_page.blocks.size()==owner.order.size(),"Unexpected historical page shape");
        for(std::size_t i=0;i<owner.order.size();++i)require(current_page.blocks[i].address.page==owner.original.page
            &&current_page.blocks[i].address.block==owner.order[i]&&current_page.blocks[i].address.addition_namespace.empty(),
            "A historical page lost its exact reordering");
        const auto archive=std::ranges::find(after.state_layout.positional_reading_pages,owner.original.page,&curriculum::StatePositionalReadingPage::page);
        require(archive!=after.state_layout.positional_reading_pages.end()&&archive->original.stable_id==original_page.stable_id
            &&archive->original.narration_language==original_page.narration_language&&archive->original.blocks.size()==original_page.blocks.size()
            &&archive->current_owners.size()==original_page.blocks.size(),"Missing complete positional page provenance");
        for(std::size_t i=0;i<original_page.blocks.size();++i){const auto& original=original_page.blocks[i];const auto& reconstructed=archive->original.blocks[i];
            const auto& mapped=original_owner(after,{owner.original.page,i});
            require(archive->current_owners[i]==mapped.stable_id&&reconstructed.stable_id==original.stable_id
                &&reconstructed.address.page==original.address.page&&reconstructed.address.block==original.address.block
                &&reconstructed.address.progress_page==original.address.progress_page&&reconstructed.address.addition_namespace.empty()
                &&fingerprint(reconstructed.raw_json)==fingerprint(original.raw_json)&&reconstructed.type==original.type
                &&reconstructed.text==original.text&&reconstructed.question==original.question&&reconstructed.initial_code==original.initial_code,
                "The private archive differs from an authentic original block");
        }
        require(original_page.blocks[0].text==current_page.blocks[0].text
            &&original_page.blocks[owner.original_criterion].text!=current_page.blocks[owner.current_criterion].text,
            "The title/criterion pause no longer distinguishes compatible and revised prose");
    }
}

struct Evidence {
    std::size_t owner;
    bool mapped,correct;
    std::string program,review;
    state::Preferences receipts;
};
struct Preserved {
    std::vector<Evidence> executions;
    std::map<std::filesystem::path,Harness::Recording> files;
    std::map<std::string,state::Draft> drafts;
};
std::string feedback_text(const curriculum::Block& block,std::string_view output,bool correct){
    const auto& check=block.output_check->cases.front();std::string body;
    if(correct)body=check.explanation;
    else for(const auto& rule:check.review_feedback){
        if(std::ranges::any_of(rule.outputs,[&](const auto& accepted){return output==accepted
                ||(accepted.find('\r')==accepted.npos&&output==crt(accepted));})){body=rule.text;break;}
    }
    const auto first=body.find_first_not_of("\r\n");body=first==body.npos?std::string{}:body.substr(first);
    return (correct?std::string("✓ Correcto"):std::string("✗ Incorrecto"))+(body.empty()?"":"\n\n"+body);
}
void verify_prepared(Harness& f){const auto regions=lesson_reading_regions(f.view);
    for(const auto& part:f.snapshot().segments){const auto region=std::ranges::find(regions,part.region,&LessonReadingRegion::key);
        require(region!=regions.end(),"A historical PCM segment has no displayed reading region");f.verify_audio(part,!region->passive_label);}
}
std::vector<std::size_t> execute(Harness& f,const Source& source,const curriculum::Lesson& lesson,
        std::size_t number,bool mapped,const std::string& program,std::string_view expected_output,bool correct,
        bool exact_output,Preserved& kept){
    const auto& owner=source.owners[number];const auto where=mapped?owner.current:owner.original;const auto& block=at(lesson,where);
    require(lesson_page_state(f.view).index==where.page,"The real execution opened another historical page");
    if(edit_text(f.control(where.block,10))!=program)f.edit(where.block,10,program);
    const auto run=f.compile(where.block,program);
    const auto output=bytes(run/"execution-stdout.bin");
    require(output==expected_output||(!exact_output&&output==crt(expected_output)),
        "A real historical program disagrees with the independently authenticated byte oracle");
    const auto feedback=feedback_parts(f,where.block,feedback_text(block,output,correct));verify_prepared(f);f.privacy();
    const auto data=f.saved();windows::CppRunRequest capture;capture.source_utf8=program;
    const auto current=windows::compiler_puzzle_current_feedback(data,lesson,block,capture);
    require(current&&current->correct==correct&&windows::compiler_puzzle_completed(data,lesson,block)==correct
        &&windows::compiler_puzzle_case_count(data,lesson,block)==(correct?1u:0u),
        "The current verdict was not earned by this actual educational execution");
    const auto prefix="compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+curriculum::exercise_key(lesson,block)+'/'
        +state::sha256(curriculum::reference_objective_frame(lesson,block))+'/';
    Evidence proof{number,mapped,correct,program,windows::compiler_puzzle_review(data,lesson,block,capture),{}};
    for(const auto& [key,value]:data.preferences.at("native_windows"))if(key.starts_with(prefix))proof.receipts.emplace(key,value);
    require(!proof.receipts.empty()&&(correct?proof.review.empty():!proof.review.empty()),"The actual run has no durable native proof");
    kept.executions.push_back(std::move(proof));
    for(const auto name:{"main.cpp","stdin.bin","execution-stdout.bin","execution-stderr.bin"}){const auto path=run/name;
        kept.files.emplace(path,Harness::Recording{state::sha256(state::read_bytes(path)),std::filesystem::last_write_time(path)});}
    const auto key=curriculum::draft_key("programming",lesson,block);const auto draft=state::get_draft(data,key,block.initial_code);
    require(draft.has_saved&&draft.source==program&&draft.input.empty(),"The actual historical editor did not save its draft");
    const auto existing=kept.drafts.find(key);
    require(existing==kept.drafts.end()||same_draft(existing->second,draft),"A subsequent actual run changed the retained draft");
    kept.drafts.try_emplace(key,draft);return feedback;
}
void preserve(Harness& f,const Source& source,const curriculum::Lesson& before,const curriculum::Lesson& after,
        const std::vector<OrdinaryProof>& ordinary,const Preserved& kept,
        const std::vector<std::uint8_t>& before_bytes,const std::vector<std::uint8_t>& after_bytes){
    const auto data=f.saved();f.preserved();
    require(state::read_bytes(before.source_path)==before_bytes&&state::read_bytes(after.source_path)==after_bytes,
        "The historical regression changed an authenticated source");
    for(const auto& proof:ordinary){const auto& old=original_owner(before,proof.position);const auto& current=original_owner(after,proof.position);
        const auto response=state::get_response(data,after,current);
        require(state::response_storage_key(after,current)==proof.key&&state::response_storage_key(before,old)==proof.key
            &&state::string_value(data,"learning_progress",proof.key)==proof.raw&&response&&response->correct,
            "Adoption changed an original synthetic prerequisite response");}
    for(const auto& page:after.pages)for(const auto& block:page.blocks){const auto key=state::response_storage_key(after,block);
        if(std::ranges::none_of(ordinary,[&](const auto& proof){return proof.key==key;}))
            require(!state::get_response(data,after,block),"Narration invented an ordinary response");}
    for(const auto& [key,draft]:kept.drafts)require(same_draft(state::get_draft(data,key,{}),draft),"Adoption changed a real retained compiler draft");
    for(const auto& [file,record]:kept.files)require(state::sha256(state::read_bytes(file))==record.sha
        &&std::filesystem::last_write_time(file)==record.modified,"Historical execution evidence was rewritten");
    for(const auto& proof:kept.executions){const auto& lesson=proof.mapped?after:before;
        const auto& owner=source.owners[proof.owner];const auto& block=at(lesson,proof.mapped?owner.current:owner.original);
        windows::CppRunRequest capture;capture.source_utf8=proof.program;
        require(windows::compiler_puzzle_completed(data,lesson,block)==proof.correct
            &&windows::compiler_puzzle_case_count(data,lesson,block)==(proof.correct?1u:0u)
            &&windows::compiler_puzzle_review(data,lesson,block,capture)==proof.review,
            "A genuine old or current compiler verdict lost its own execution identity");
        for(const auto& [key,value]:proof.receipts)require(state::find(data,"native_windows",key)&&*state::find(data,"native_windows",key)==value,
            "A preserved native receipt/review was overwritten");
    }
    for(std::size_t n=0;n<source.owners.size();++n)if(std::ranges::none_of(kept.executions,[&](const auto& p){return p.mapped&&p.owner==n;})){
        const auto& block=at(after,source.owners[n].current);windows::CppRunRequest capture;
        const auto draft=state::get_draft(data,curriculum::draft_key("programming",after,block),block.initial_code);capture.source_utf8=draft.source;
        require(!windows::compiler_puzzle_completed(data,after,block)&&windows::compiler_puzzle_case_count(data,after,block)==0
            &&!windows::compiler_puzzle_current_feedback(data,after,block,capture),"An old receipt falsely credited an unexecuted v2 objective");
    }
}

// Budget the full real page at its recorded playback rate, rather than infer
// duration from Spanish word counts or C++ punctuation spelling.
ULONGLONG playback_budget(const LessonReaderSnapshot& value){double duration=0;
    for(const auto& part:value.segments){require(std::isfinite(part.duration_ms)&&part.duration_ms>0,"Invalid prepared PCM duration");duration+=part.duration_ms;}
    require(std::isfinite(value.rate)&&value.rate>0&&duration/value.rate<300000,"Historical page exceeds its bounded playback scope");
    return std::max<ULONGLONG>(120000,static_cast<ULONGLONG>(duration/value.rate)+30000);
}
std::size_t slot(const narration::History& history,std::string_view key){const auto item=std::ranges::find(history.entries,key,&narration::Bookmark::key);
    require(item!=history.entries.end(),"The original focal bookmark disappeared");return static_cast<std::size_t>(item-history.entries.begin());}

void exercise(const std::shared_ptr<windows::NarrationWorker>& backend,const Source& source,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path,unsigned phase){
    require(phase<4,"Each remaining lesson has two actual pause phases per compiler");
    const auto number=phase/2;const bool reference_attempt=phase%2==1,current_correct=reference_attempt;
    const bool old_correct=reference_attempt&&!(source.characters&&number==0);
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    require(state::sha256(before_bytes)==source.before_sha&&state::sha256(after_bytes)==source.after_sha,
        "The remaining reader requires the exact 617f034 and 5d115a0 publications");
    const auto before=windows::load_lesson(before_path,"es"),after=windows::load_lesson(after_path,"es");authenticate(source,before,after);
    const auto& owner=source.owners[number];const auto& first_owner=source.owners[0];
    const auto& old=at(before,owner.original);const auto& current=at(after,owner.current);
    const auto& oracle=old.output_check->cases.front().outputs.front();
    auto spec=constants;spec.label=source.label;spec.token=source.token;spec.folder=source.folder;spec.preference=source.preference;
    spec.compiler_page=first_owner.original.page;spec.answer={};spec.hidden_first={};spec.hidden_second={};
    std::vector<OrdinaryProof> ordinary;Preserved kept;
    Harness f(backend,spec,before_path,source.before_sha,[&](state::StateData& data,const curriculum::Lesson& lesson){
        require(prepare_entry(data,lesson,owner,ordinary)==first_owner.original.page,"The first actual QA entry skipped a compiler");
        require(narration::read_history(data).entries.empty()&&!data.preferences.contains("native_windows"),"QA entry fabricated execution or listening");
    });
    std::string prerequisite;
    if(number){prerequisite=source.characters?binary_prerequisite(source,before):at(before,first_owner.reference_original).code;
        const auto& output=at(before,first_owner.original).output_check->cases.front().outputs.front();
        (void)execute(f,source,before,0,false,prerequisite,output,true,source.characters,kept);
        f.close();state::StateStore store(f.environment.paths.root);auto data=store.load();const auto history=narration::read_history(data);
        require(prepare_entry(data,before,owner,ordinary)==owner.original.page&&narration::read_history(data)==history,
            "The second historical entry did not depend on its actually earned first compiler");store.save(data);f.open(owner.original.page);
    }
    f.ready();const auto program=reference_attempt?at(before,owner.reference_original).code:old.initial_code;
    std::string expected=reference_attempt?oracle:std::string{};
    if(!reference_attempt&&!source.characters&&number==1){expected=oracle;
        for(const auto line:{"Ceros: 1\n","Ceros: 0\n","Ceros: 0\n","Ceros: 1\n"}){const auto at=expected.find(line);
            require(at!=expected.npos,"The authentic transfer starter oracle lost a missing Ceros line");expected.erase(at,std::char_traits<char>::length(line));}
        require(expected.size()==584&&state::sha256(expected)=="60a8ea4874405f66aa4d931205749848300cacd8189f02e724fa26a915788dce",
            "The rejected transfer starter must retain its actual nonempty original report");
    }
    const bool literal_crt=source.characters&&number==0&&reference_attempt;
    if(literal_crt)expected=crt(oracle);
    (void)execute(f,source,before,number,false,program,expected,old_correct,literal_crt,kept);
    f.reader->restart();f.reader->pause();const auto old_plan=f.snapshot();const auto original_regions=lesson_reading_regions(f.view);
    const auto passage=reference_attempt?owner.original_criterion:0u;std::vector<std::size_t> candidates;
    for(std::size_t i=0;i<old_plan.segments.size();++i){const auto& part=old_plan.segments[i];
        if(part.block==passage&&!part.feedback&&part.target==f.control(passage,0))candidates.push_back(i);}
    require(candidates.size()>(reference_attempt?1u:0u),"The authentic old criterion lacks its selected second spoken sentence");
    const auto wanted=candidates[reference_attempt?1u:0u];
    // Play all preceding gates for real, including the old compiler on L22 p4
    // and the answered ordinary prediction on L22 p7. A seek is not hearing.
    f.reader->toggle();wait([&]{const auto value=f.snapshot();require(value.index<=wanted,"Old playback skipped the selected actual sentence");
        return value.playing&&value.index==wanted&&value.offset>150&&value.offset+200<value.segments[value.index].duration_ms;
    },"actual positional PCM pause after its genuine preceding feedback",playback_budget(old_plan));
    f.reader->pause();const auto paused=f.snapshot();verify_prepared(f);
    f.close();state::StateStore store(f.environment.paths.root);auto data=store.load();const auto old_history=narration::read_history(data);
    const auto old_slot=slot(old_history,paused.key);const auto original_bookmark=old_history.entries[old_slot];
    require(original_bookmark.page=="programming/"+before.id+'/'+std::to_string(owner.original.page)
        &&original_bookmark.position.heard_prefix&&original_bookmark.position.heard_prefix->microseconds>0
        &&original_bookmark.position.milliseconds==paused.offset&&!paused.playing&&!paused.allocated&&!paused.finished,
        "The historical focal bookmark was not earned from real released PCM");
    state::prepare_lesson_state(data,after);state::validate_lesson_state(data,after);
    require(narration::read_history(data)==old_history,"Map adoption changed narration before the actual reader");store.save(data);
    f.asset=after_path;f.original_asset=after_bytes;f.lesson=after;
    if(number){
        // The old prerequisite receipt cannot admit today's second editor.
        f.open(first_owner.current.page);f.ready();require(!f.snapshot().playing&&!f.snapshot().allocated,"Adoption autoplayed the pending prerequisite");
        preserve(f,source,before,after,ordinary,kept,before_bytes,after_bytes);
        const auto& output=at(before,first_owner.original).output_check->cases.front().outputs.front();
        (void)execute(f,source,after,0,true,prerequisite,output,true,source.characters,kept);f.close();data=store.load();
        const auto history=narration::read_history(data);
        require(history.entries[slot(history,paused.key)]==original_bookmark&&state::can_reach_page(data,after,owner.current.page),
            "The current prerequisite changed the old focal pause or failed sequential reachability");
        // QA selects the already reached target again; create_lesson_view still
        // enforces every ordinary and compiler requirement when opening it.
        state::save_current_page(data,after,owner.current.page);
        require(narration::read_history(data)==history,"Selecting the admitted target fabricated a narration slot");store.save(data);
    }
    const auto before_recovery=narration::read_history(store.load());const auto pending_slot=slot(before_recovery,paused.key);
    f.open(owner.current.page);f.ready();f.privacy();const auto mapped=f.snapshot();
    const auto current_passage=reference_attempt?owner.current_criterion:0u;
    const auto first=std::ranges::find_if(mapped.segments,[&](const auto& p){return p.block==current_passage&&!p.feedback&&p.target==f.control(current_passage,0);});
    require(first!=mapped.segments.end()&&(!reference_attempt||first!=mapped.segments.begin()),"The revised actual criterion is missing or aliases the title");
    const auto boundary=static_cast<std::size_t>(first-mapped.segments.begin());
    require(mapped.key!=paused.key&&mapped.index==boundary&&mapped.offset==0&&!mapped.playing&&!mapped.allocated&&!mapped.finished
        &&mapped.heard_through<=static_cast<std::int64_t>(boundary),"Recovery skipped the focal revision or inherited its recording position");
    const auto recovered=narration::read_history(f.saved());
    require(recovered.entries.size()==before_recovery.entries.size()&&slot(recovered,mapped.key)==pending_slot
        &&std::ranges::none_of(recovered.entries,[&](const auto& b){return b.key==paused.key;})
        &&recovered.entries[pending_slot].page==lesson_page_state(f.view).narration_page_identity
        &&(!recovered.entries[pending_slot].position.heard_prefix||recovered.entries[pending_slot].position.heard_prefix->microseconds==0),
        "Recovery did not rekey the existing focal slot without a new listening certificate");
    for(std::size_t i=0;i<recovered.entries.size();++i)if(i!=pending_slot)
        require(recovered.entries[i]==before_recovery.entries[i],"Recovery rewrote an unrelated prior page bookmark");
    bool authenticated=false;
    for(const auto& archive:editorial_lesson_reading_archives(f.view)){
        if(!archive.positional_page_identity||archive.page_identity!=original_bookmark.page||archive.regions.size()!=original_regions.size())continue;
        bool same=true;std::vector<windows::NarrationSegment> script;
        for(std::size_t i=0;i<archive.regions.size();++i){const auto& r=archive.regions[i];const auto& old_region=original_regions[i];
            same=same&&r.source_text==old_region.source_text&&r.reading_context==old_region.reading_context
                &&r.math_mode==old_region.math_mode&&r.passive_label==old_region.passive_label&&r.feedback==old_region.feedback;
            const auto parts=backend->plan(r.source_text,"es",{},r.reading_context,r.math_mode,windows::NarrationOutputDataPolicy::current,r.text_semantics);
            script.insert(script.end(),parts.begin(),parts.end());
        }
        if(same&&backend->identity(archive.page_identity,"es","ef_dora",script)==paused.key)authenticated=true;
    }
    require(authenticated,"The complete authentic positional dynamic script was not reconstructed");
    // Only exact ordinary feedback actually played before the old pause may
    // retain credit. The revised criterion itself always starts with none.
    if(mapped.heard_through){require(!source.characters&&number==1&&reference_attempt,"Unrelated historical feedback acquired hearing");
        bool matched=false;
        for(std::size_t i=0;i<static_cast<std::size_t>(mapped.heard_through);++i){const auto& part=mapped.segments[i];if(!part.feedback||!part.correct)continue;
            const auto previous=std::ranges::find_if(old_plan.segments,[&](const auto& p){return p.block==1&&p.feedback&&p.correct
                &&p.source==part.source&&p.spoken==part.spoken;});
            require(previous!=old_plan.segments.end()&&previous-old_plan.segments.begin()<paused.heard_through,
                "Compatible-feedback credit has no actually played original source");matched=true;}
        require(matched,"A retained feedback prefix has no compatible original feedback");
    }
    preserve(f,source,before,after,ordinary,kept,before_bytes,after_bytes);f.reopen_paused(mapped);
    const auto current_plan=f.snapshot();std::set<std::size_t> criteria,heard;
    for(const auto block:owner.before_prose){prepared_prose(f,block,after.pages[owner.current.page].blocks[block].text);
        require(edit_text(f.control(block,0))==visible(after.pages[owner.current.page].blocks[block].text),"An active criterion is not fully displayed");
        for(std::size_t i=0;i<current_plan.segments.size();++i)if(current_plan.segments[i].block==block&&!current_plan.segments[i].feedback)criteria.insert(i);}
    require(!criteria.empty()&&!lesson_page_state(f.view).can_next,"The unexecuted target inherited completion");
    f.reader->toggle();wait([&]{const auto value=f.snapshot();if(value.playing)heard.insert(value.index);
        return value.waiting_gate&&!value.playing&&!value.allocated&&value.segments[value.index].block==owner.current.block;
    },"complete current criteria until the unproved focal compiler",playback_budget(current_plan));
    require(std::ranges::all_of(criteria,[&](auto i){return heard.contains(i);}),"The current reader skipped a revised criterion");
    preserve(f,source,before,after,ordinary,kept,before_bytes,after_bytes);
    const auto verdict=execute(f,source,after,number,true,program,expected,current_correct,literal_crt,kept);
    const auto fresh=f.snapshot();require(fresh.index==verdict.front()&&fresh.offset==0&&!fresh.playing&&!fresh.allocated&&!fresh.finished,
        "The new current feedback started playback or inherited hearing");
    f.reopen_paused(fresh);preserve(f,source,before,after,ordinary,kept,before_bytes,after_bytes);
    const auto complete=f.snapshot();std::set<std::size_t> required(verdict.begin(),verdict.end());
    if(current_correct)for(const auto block:owner.after_prose){prepared_prose(f,block,after.pages[owner.current.page].blocks[block].text);
        for(std::size_t i=0;i<complete.segments.size();++i)if(complete.segments[i].block==block&&!complete.segments[i].feedback)required.insert(i);}
    heard.clear();f.reader->toggle();wait([&]{const auto value=f.snapshot();if(value.playing)heard.insert(value.index);
        return std::ranges::all_of(required,[&](auto i){return heard.contains(i);})&&!value.playing&&!value.allocated&&!value.preparing
            &&value.finished==current_correct&&value.index==(current_correct?value.segments.size()-1:verdict.back())
            &&value.offset+1>=value.segments[value.index].duration_ms;
    },"complete current verdict and any newly admitted following criterion",playback_budget(complete));
    require(lesson_page_state(f.view).can_next==current_correct,"Current success/retry did not control advancement");
    preserve(f,source,before,after,ordinary,kept,before_bytes,after_bytes);
    std::cout<<"PASS "<<source.label<<" remaining historical reader phase "<<phase<<" owner="<<owner.id
        <<"; original/current verdict="<<old_correct<<'/'<<current_correct<<"; "<<ordinary.size()<<" disclosed synthetic ordinary prerequisites; "
        <<kept.executions.size()<<" actual native runs; authenticated positional PCM pause rekeyed, revised criteria/fresh feedback fully played, "
        <<"original responses/drafts/receipts/files/WAV retained. "
        <<(source.characters&&number?"First prerequisite alone uses an explicit reversible binary-stdout QA prelude, not a literal published execution. ":"")
        <<"No Android audio/verdict import, human listening or full-lesson acceptance claimed.\n";
}
} // namespace remaining_compiler_historical
