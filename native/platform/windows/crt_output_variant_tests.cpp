#include "core/crt_output_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
void require(bool condition,std::string_view message) {if(!condition)throw std::runtime_error(std::string(message));}
template<class Action>void rejects(Action&& action,std::string_view message) {
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
    require(rejected,message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"Missing owned CRT QA artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
const Block& at(const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);require(position.has_value(),"Missing exact CRT QA identity");
    return lesson.pages[position->page].blocks[position->block];
}
state::StateData reopen(const state::StateStore& store) {
    state::StateStore next(store.root().parent_path());
    require(next.root()==store.root(),"CRT QA reopened a different profile/state directory");return next.load();
}
void seed_private_state(state::StateData& data,const Lesson& lesson,const Block& editor) {
    state::prepare_lesson_state(data,lesson);
    data.preferences["crt-qa-unrelated"]["opaque"]=std::string(" preserve\t\r\n保存 ");
    state::Draft draft;draft.source=editor.initial_code+"\n// private synthetic draft metadata\n";
    draft.input="";draft.selection_start=3;draft.selection_end=7;
    state::put_draft(data,draft_key("programming",lesson,editor),draft);
    state::put_response(data,"crt-qa-later-response",state::Response{"synthetic later answer",true});
    narration::History history;
    // Synthetic paused metadata only: no audio, listened prefix or playback.
    for(unsigned index=0;index<3;++index)history.entries.push_back({"synthetic-crt-page-"+std::to_string(index),
        "synthetic-stable-"+std::to_string(index),narration::Position{1,static_cast<std::int64_t>(20+index),0,false},
        "synthetic-other-page-"+std::to_string(index)});
    narration::write_history(data,history);
    state::save_current_page(data,lesson,5);
}
struct Capture {CppRunRequest request;CppRunResult result;FreshExecution execution;};
}

int main(int argc,char** argv) try {
    const bool contracts_only=argc==2&&std::string_view(argv[1])=="--contracts-only";
    require(argc==1||contracts_only,"Unknown CRT QA selector");
    const auto before=test::load_pre_crt_output_lesson();
    const auto after=test::with_crt_output_variant(before);
    const auto metadata=parse_lesson_progress_metadata(after.raw_json);
    const auto& old=at(before,test::crt_output_editor_id);const auto& current=at(after,test::crt_output_editor_id);
    const auto& projected=at(metadata,test::crt_output_editor_id);
    const auto& partial=at(before,"block_0006_0001");const auto& reference=at(before,"block_0008_0001");
    const auto position=stable_block_position(before,test::crt_output_editor_id);
    const auto original=old.output_check->cases.front().outputs.front();
    const auto variant=current.output_check->cases.front().outputs.back();
    const auto active_path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/"
        "lesson_clasificar_caracteres_y_normalizar_una_copia.json";
    const auto active_hash=state::sha256(read(active_path));
    const LaunchOptions options{ProfileId::Codex,L"crt-u12l20-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    const auto projects=environment.paths.projects/L"Código & (controles CRT)";
    unsigned passed=0,failed=0,runs=0;std::uint64_t sequence=0;
    const auto test_case=[&](std::string_view name,const std::function<void()>& action) {
        try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    const auto run=[&](const std::string& source) {
        Capture value;value.request.request_id=++sequence;value.request.revision=100+sequence;
        value.request.source_utf8=source;value.request.projects_directory=projects;
        value.request.compile_timeout=30s;value.request.execution_timeout=3s;
        ++runs;value.result=run_cpp(value.request);value.execution=capture_cpp_execution(value.request,value.result);
        require(value.result.error_utf8.empty()&&value.result.compilation.succeeded()&&value.execution.execution_completed
            &&value.execution.execution_succeeded&&value.execution.exit_code==0&&value.execution.standard_error.empty(),
            "A reviewed CRT program failed: "+value.result.error_utf8+value.result.compilation.stdout_bytes+value.result.compilation.stderr_bytes);
        require(read(value.result.run_directory/L"main.cpp")==source
            &&read(value.result.run_directory/L"execution-stdout.bin")==value.execution.standard_output,
            "The native adapter changed the authored CRT source or raw captured stdout");
        return value;
    };

    test_case("exact CRT pair keeps original control bytes, addresses and completion identity",[&] {
        require(!old.crt_output_original&&current.crt_output_original==std::optional<std::string>{original}
            &&projected.crt_output_original==current.crt_output_original&&projected.output_check,
            "The closed CRT provenance was missing or leaked into the unexpanded objective");
        require(original.size()==802&&variant.size()==844&&std::ranges::count(original,'\n')==42
            &&std::ranges::count(original,'\r')==3&&std::ranges::count(variant,'\n')==42&&std::ranges::count(variant,'\r')==45,
            "The raw reviewed CRT controls or exact byte lengths changed");
        require(current.initial_code==old.initial_code&&after.state_layout.lineage==before.state_layout.lineage
            &&after.state_layout.signatures==before.state_layout.signatures
            &&exercise_key(after,current)==exercise_key(before,old)
            &&draft_key("programming",after,current)==draft_key("programming",before,old),
            "The extra raw spelling changed source, progress lineage or draft ownership");
        require(reference_objective_frame(before,old)==reference_objective_frame(after,current)
            &&reference_objective_frame(metadata,projected)==reference_objective_frame(before,old),
            "The reviewed equivalent spelling invalidated an original complete-execution proof");
        FreshExecution modeled{77,true,true,0,"",original};
        require(evaluate_output_check(*current.output_check,modeled,77).verdict==OutputVerdict::correct,
            "The original exact output stopped satisfying the expanded objective");
        modeled.standard_output=variant;
        require(evaluate_output_check(*current.output_check,modeled,77).verdict==OutputVerdict::correct
            &&evaluate_cpp_output_check(*old.output_check,modeled,77).verdict==OutputVerdict::incorrect,
            "The raw CRT spelling was not accepted explicitly or escaped into the original CR-sensitive adapter contract");
        auto foreign=current;foreign.stable_id+="_different";auto unmarked=foreign;unmarked.crt_output_original.reset();
        require(reference_objective_frame(after,foreign)==reference_objective_frame(after,unmarked),
            "A copied CRT marker granted another stable editor the original proof identity");
        auto altered=current;altered.output_check->cases.front().outputs.back()+=' ';
        require(reference_objective_frame(after,altered)!=reference_objective_frame(before,old),
            "A stale derived CRT marker hid a meaningful output change");
        OutputCheck other;other.cases.push_back({"",{"A\rB\n"}});modeled.standard_output="A\rB\r\n";
        require(evaluate_cpp_output_check(other,modeled,77).verdict==OutputVerdict::incorrect,
            "An unrelated explicit CR became globally normalized");
    });

    test_case("scope and complete-block guards reject honest mutated proposals",[&] {
        const auto target=[&](const JsonObject& root) {
            return root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks")
                .GetObjectAt(static_cast<unsigned>(position->block));
        };
        for(const auto change:{"source","input","first","second","controls","swap","extra","exit","owner","identity","edge","unmapped","v1"}) {
            const auto root=JsonObject::Parse(winrt::to_hstring(after.raw_json));const auto editor=target(root);
            const auto item=editor.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
            const auto outputs=item.GetNamedArray(L"outputs");const std::string_view mode=change;
            bool payload=false;
            if(mode=="source"){editor.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(old.initial_code+"\n// altered")));payload=true;}
            if(mode=="input"){item.Insert(L"standardInput",JsonValue::CreateStringValue(L"other"));payload=true;}
            if(mode=="first"||mode=="second"){
                const unsigned index=mode=="first"?0:1;
                outputs.SetAt(index,JsonValue::CreateStringValue(winrt::to_hstring(winrt::to_string(outputs.GetStringAt(index))+" ")));payload=true;
            }
            if(mode=="controls"){
                auto incorrect=original;std::erase(incorrect,'\r');std::string expanded;
                for(const char c:incorrect){if(c=='\n')expanded+='\r';expanded+=c;}
                outputs.SetAt(1,JsonValue::CreateStringValue(winrt::to_hstring(expanded)));payload=true;
            }
            if(mode=="swap"){const auto first=outputs.GetAt(0),second=outputs.GetAt(1);outputs.SetAt(0,second);outputs.SetAt(1,first);payload=true;}
            if(mode=="extra"){outputs.Append(JsonValue::CreateStringValue(L"other"));payload=true;}
            if(mode=="exit"){item.Insert(L"exitCode",JsonValue::CreateNumberValue(1));payload=true;}
            if(payload){const auto proofs=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
                proofs.GetObjectAt(proofs.Size()-1).Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(editor))));}
            if(mode=="owner")root.Insert(L"id",JsonValue::CreateStringValue(L"another_owner"));
            if(mode=="identity"){
                const auto ids=root.GetNamedObject(L"stateLayout").GetNamedArray(L"current").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks");
                const auto index=static_cast<unsigned>(position->block),other=index?index-1:1;const auto first=ids.GetAt(index),second=ids.GetAt(other);
                ids.SetAt(index,second);ids.SetAt(other,first);
            }
            if(mode=="edge")root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
            if(mode=="unmapped")root.Remove(L"stateLayout");
            if(mode=="v1"){
                const auto layout=root.GetNamedObject(L"stateLayout");JsonObject v1;v1.Insert(L"version",JsonValue::CreateNumberValue(1));
                v1.Insert(L"origin",layout.GetNamedArray(L"origin"));v1.Insert(L"current",layout.GetNamedArray(L"current"));root.Insert(L"stateLayout",v1);
            }
            const auto raw=winrt::to_string(root.Stringify());
            rejects([&]{(void)parse_lesson_json(raw);},"An unreviewed CRT proposal passed the runtime loader");
            rejects([&]{(void)parse_lesson_progress_metadata(raw);},"An unreviewed CRT proposal passed progress metadata");
        }
    });

    test_case("editorial CRT guidance retains exact execution payload and native full/progress ownership",[&] {
        const std::vector<OutputReviewFeedback> reviews{{{""},"QA empty output guidance"},{{"partial"},"QA partial report guidance"}};
        const auto guided=test::with_crt_output_guidance(after,"QA complete report explanation",reviews);
        const auto progressed=parse_lesson_progress_metadata(guided.raw_json);
        const auto& editor=at(guided,test::crt_output_editor_id);const auto& progress=at(progressed,test::crt_output_editor_id);
        require(editor.crt_output_original==current.crt_output_original&&progress.crt_output_original==current.crt_output_original&&
            editor.output_check->cases.front().outputs==current.output_check->cases.front().outputs&&editor.initial_code==current.initial_code&&
            guided.state_layout.lineage==after.state_layout.lineage&&guided.state_layout.signatures==after.state_layout.signatures&&
            reference_objective_frame(guided,editor)==reference_objective_frame(before,old)&&
            reference_objective_frame(progressed,progress)==reference_objective_frame(before,old),
            "Adding CRT guidance changed an output/control byte, source, state or execution proof identity");
        require(editor.output_check->cases.front().explanation=="QA complete report explanation"&&
            progress.output_check->cases.front().explanation==editor.output_check->cases.front().explanation&&
            editor.output_check->cases.front().review_feedback.size()==2&&progress.output_check->cases.front().review_feedback.size()==2&&
            progress.output_check->cases.front().review_feedback[1].outputs==reviews[1].outputs&&
            progress.output_check->cases.front().review_feedback[1].text==reviews[1].text,
            "Full/progress loading discarded or normalized the ordinary authored guidance");
        const auto updated=test::with_crt_output_guidance(guided,"QA another explanation",std::vector<OutputReviewFeedback>{{{""},"QA changed wording"}});
        const auto& revised=at(updated,test::crt_output_editor_id);
        require(revised.crt_output_original==current.crt_output_original&&revised.output_check->cases.front().review_feedback.front().text=="QA changed wording"&&
            updated.state_layout.lineage==after.state_layout.lineage&&reference_objective_frame(updated,revised)==reference_objective_frame(before,old),
            "A later arbitrary editorial wording required another execution alias or receipt");
        for(const auto mode:{"source","input","output","case","unknown","guide_shape","lost_edge"}){
            const auto root=JsonObject::Parse(winrt::to_hstring(guided.raw_json));const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page))
                .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));const auto config=block.GetNamedObject(L"outputCheck");const auto item=config.GetNamedArray(L"cases").GetObjectAt(0);
            if(std::string_view(mode)=="source")block.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(old.initial_code+"\n// changed")));
            if(std::string_view(mode)=="input")item.Insert(L"standardInput",JsonValue::CreateStringValue(L"other"));
            if(std::string_view(mode)=="output")item.GetNamedArray(L"outputs").SetAt(1,JsonValue::CreateStringValue(winrt::to_hstring(variant+" ")));
            if(std::string_view(mode)=="case")config.GetNamedArray(L"cases").Append(item);
            if(std::string_view(mode)=="unknown")item.Insert(L"unknown",JsonValue::CreateBooleanValue(true));
            if(std::string_view(mode)=="guide_shape")item.Insert(L"reviewFeedback",JsonValue::CreateStringValue(L"not a list"));
            const auto history=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
            history.GetObjectAt(history.Size()-1).Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(block))));
            if(std::string_view(mode)=="lost_edge")history.RemoveAtEnd();
            const auto source=winrt::to_string(root.Stringify());rejects([&]{(void)parse_lesson_json(source);},"CRT guidance hid a protected payload or provenance change");
            rejects([&]{(void)parse_lesson_progress_metadata(source);},"CRT metadata guidance hid a protected payload or provenance change");
        }
    });

    if(!contracts_only)test_case("a synthetic valid old receipt survives alias adoption and exact reopen without new credit",[&] {
        state::StateStore store(environment.paths.root/L"synthetic-prior");auto data=store.load();seed_private_state(data,before,old);
        data.preferences["crt-qa-unrelated"]["provenance"]=std::string("Synthetic historical receipt fixture; no prior execution or listening claimed.");
        state::put_response(data,before,old,state::Response{"old opaque compiler correct flag",true});
        const auto merely_answered=data;state::prepare_lesson_state(data,after);
        require(data==merely_answered&&!compiler_puzzle_completed(data,after,current),
            "The CRT alias or an old opaque correct flag manufactured a fresh native achievement");
        CppRunRequest modeled;modeled.revision=41;modeled.source_utf8=reference.code;
        FreshExecution proof{41,true,true,0,"",original};
        require(record_compiler_puzzle_case(data,before,old,modeled,proof,"Synthetic correct historical receipt"),
            "Could not create the explicitly synthetic prior-execution fixture");
        const auto history=data;const auto pauses=narration::read_history(data);
        state::prepare_lesson_state(data,after);state::validate_lesson_state(data,after);
        require(data==history&&compiler_puzzle_completed(data,after,current)&&compiler_puzzle_completed(data,metadata,projected)
            &&narration::read_history(data)==pauses&&pauses.entries.size()==3,
            "Adopting the raw CRT spelling rewrote an old receipt, draft, response or bounded pause metadata");
        store.save(data);require(reopen(store)==history,"Exact old CRT achievement/state changed on a real StateStore reopen");
        auto fresh=history;reset_compiler_puzzle(fresh,after,current);
        const auto prefix="compiler-puzzle/v1/"+before.locale+'/'+before.id+'/'+exercise_key(before,old)+'/';
        auto expected=history;if(auto domain=expected.preferences.find("native_windows");domain!=expected.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
        require(fresh==expected&&!compiler_puzzle_completed(fresh,before,old)&&!compiler_puzzle_completed(fresh,after,current)
            &&narration::read_history(fresh)==pauses,"Reset retained the CRT achievement or erased downstream/draft/pause state");
        store.save(fresh);require(reopen(store)==expected,"The explicitly reset CRT requirement changed on reopen");
    });

    if(!contracts_only)test_case("three real CRT programs reject starter/partial and accept the full 844-byte raw reference",[&] {
        state::StateStore store(environment.paths.root/L"actual-runs");auto data=store.load();seed_private_state(data,after,current);
        require(!compiler_puzzle_completed(data,after,current),"A fresh CRT run profile inherited an old model receipt");
        const auto protected_state=data;
        for(const auto& source:{old.initial_code,partial.code}) {
            const auto capture=run(source);
            require(evaluate_cpp_output_check(*current.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::incorrect
                &&!record_compiler_puzzle_case(data,after,current,capture.request,capture.execution,"Incorrecto"),
                "The initial empty source or fixed single-sample report satisfied seven required CRT cases");
            require(record_compiler_puzzle_review(data,after,current,capture.request,capture.execution,"Incorrecto\nThe complete seven-case report is still required."),
                "A real CRT rejection could not retain its current review");
            store.save(data);const auto restored=reopen(store);
            const auto feedback=compiler_puzzle_current_feedback(restored,after,current,capture.request);
            require(restored==data&&feedback&&!feedback->correct&&!compiler_puzzle_completed(restored,after,current),
                "A real CRT rejection was lost or became success after reopening");
        }
        const auto capture=run(reference.code);
        require(capture.execution.standard_output==variant&&capture.execution.standard_output.size()==844
            &&capture.result.execution.stdout_bytes==variant,"The complete reference did not retain its exact 844 raw Windows bytes");
        require(evaluate_cpp_output_check(*old.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::incorrect
            &&evaluate_output_check(*current.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::correct,
            "The real CRT output bypassed the old guard or failed its explicitly added exact alternative");
        require(record_compiler_puzzle_case(data,after,current,capture.request,capture.execution,"Correcto\nThe exact raw seven-case report matches.")
            &&compiler_puzzle_completed(data,after,current)&&compiler_puzzle_review(data,after,current,capture.request).empty(),
            "The real complete CRT run failed to earn its own proof or retained obsolete rejection");
        for(const auto& [domain,values]:protected_state.preferences)for(const auto& [key,value]:values)
            require(state::find(data,domain,key)&&*state::find(data,domain,key)==value,
                "Grading real CRT programs changed a draft, answer, marker or unrelated preference");
        store.save(data);require(reopen(store)==data&&compiler_puzzle_completed(reopen(store),metadata,projected),
            "The real complete CRT achievement did not survive exact state/catalog reopening");
        auto stale=capture.execution;--stale.source_revision;const auto earned=data;
        require(!record_compiler_puzzle_case(data,after,current,capture.request,stale,"stale")&&data==earned,
            "An old execution bypassed reevaluation after CRT completion");
        auto incorrect=capture.execution;incorrect.standard_output.back()='X';
        require(!record_compiler_puzzle_case(data,after,current,capture.request,incorrect,"wrong")&&data==earned,
            "An existing CRT achievement overrode meaningful wrong raw output");
    });
    test_case("active curriculum unchanged and only the requested real compiler programs executed",[&] {
        require(runs==(contracts_only?0u:3u),"The focused CRT test omitted or repeated a real program");
        require(state::sha256(read(active_path))==active_hash,"CRT integration QA changed the active curricular asset");
    });
    std::cout<<"Headless CRT output: "<<passed<<" groups passed, "<<failed<<" failed; "<<runs<<" real programs; no UI or audio.\n";
    return failed?1:0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL CRT output setup: "<<error.what()<<'\n';return 1;
} catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL CRT output Windows setup: "<<error.code().value<<'\n';return 1;
} catch(...) {
    std::cerr<<"FAIL CRT output setup: unexpected exception\n";return 1;
}
