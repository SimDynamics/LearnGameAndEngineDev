#include "core/u12l26_exit_test_fixture.hpp"
#include "core/compiler_flow.hpp"
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
    bool rejected=false;try{action();}catch(...){rejected=true;}require(rejected,message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"Missing private U12L26 exit artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
const Block& at(const Lesson& lesson) {
    const auto p=stable_block_position(lesson,test::u12l26_exit_editor_id);
    require(p.has_value(),"Missing U12L26 stable exit editor");return lesson.pages[p->page].blocks[p->block];
}
state::StateData reopen(const state::StateStore& store) {
    state::StateStore next(store.root().parent_path());
    require(next.root()==store.root(),"U12L26 exit QA reopened another private state directory");return next.load();
}
struct Capture {CppRunRequest request;CppRunResult result;FreshExecution execution;};
}

int main(int argc,char** argv) try {
    const bool model_only=argc==2&&std::string_view(argv[1])=="--model-only";
    require(argc==1||model_only,"Unknown U12L26 exit test selection");
    const auto before=test::load_pre_u12l26_exit_lesson();
    const auto after=test::with_u12l26_intentional_exit(before);
    const auto metadata=parse_lesson_progress_metadata(after.raw_json);
    const auto& old=at(before);const auto& current=at(after);const auto& projected=at(metadata);
    const auto position=stable_block_position(before,test::u12l26_exit_editor_id);
    const auto active_path=before.source_path;const auto active_hash=state::sha256(read(active_path));
    const LaunchOptions options{ProfileId::Codex,L"exit-u12l26-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    unsigned passed=0,failed=0,runs=0;std::uint64_t sequence=0;
    const auto check=[&](std::string_view name,const std::function<void()>& action) {
        try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    const auto run=[&](std::string source) {
        Capture capture;capture.request.request_id=++sequence;capture.request.revision=100+sequence;
        capture.request.source_utf8=std::move(source);
        capture.request.projects_directory=environment.paths.projects/L"Código & (salida U12L26)";
        capture.request.compile_timeout=30s;capture.request.execution_timeout=3s;
        ++runs;capture.result=run_cpp(capture.request);capture.execution=capture_cpp_execution(capture.request,capture.result);
        require(capture.result.error_utf8.empty()&&capture.result.compilation.succeeded()&&capture.execution.execution_completed,
            "U12L26 run failed technically: "+capture.result.error_utf8+capture.result.compilation.stdout_bytes+capture.result.compilation.stderr_bytes);
        require(read(capture.result.run_directory/L"main.cpp")==capture.request.source_utf8
            &&read(capture.result.run_directory/L"execution-stdout.bin")==capture.execution.standard_output,
            "U12L26 native execution changed its captured source or stdout bytes");
        return capture;
    };

    check("closed U12L26 adoption preserves the source, owner, whole family and every draft address",[&] {
        require(!old.output_check&&current.output_check&&projected.output_check&&has_compiler_objective(current)
            &&compiler_requires_completion(current)&&compiler_requires_completion(projected),
            "The exact new exit objective disappeared from runtime or progress metadata");
        require(current.initial_code==old.initial_code&&state::sha256(current.initial_code)==
            "55e1e30b1c83b9c85cc9de03d6dedec3c310d6b1f2ea098547807aedb348b504"
            &&exercise_key(after,current)==exercise_key(before,old)
            &&draft_key("programming",after,current)==draft_key("programming",before,old)
            &&before.state_layout.lineage==after.state_layout.lineage&&before.state_layout.signatures==after.state_layout.signatures,
            "L26 adoption changed source bytes, progress lineage or stable ownership");
        require(current.output_check->input_comparison==TextComparison::exact&&current.output_check->cases.size()==1,
            "The deliberate storage failure gained a fuzzy or multi-case objective");
        const auto& item=current.output_check->cases.front();
        require(item.standard_input.empty()&&item.outputs==std::vector<std::string>{"Error al abrir salida"}
            &&item.exit_code==1&&item.explanation==test::u12l26_exit_explanation,
            "The closed exit objective changed its exact output, status, empty input or explanation");
        rejects([&]{(void)reference_objective_frame(before,old);},"A free U12L26 editor unexpectedly acquired an objective identity");
        require(reference_objective_frame(after,current)==reference_objective_frame(metadata,projected),
            "The new goal diverged between runtime and catalog metadata");
        auto restored=JsonObject::Parse(winrt::to_hstring(after.raw_json));
        restored.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks")
            .GetObjectAt(static_cast<unsigned>(position->block)).Remove(L"outputCheck");
        restored.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
        require(restored.Stringify()==JsonObject::Parse(winrt::to_hstring(before.raw_json)).Stringify(),
            "The reviewed in-memory goal changed something outside its one objective and appended edge");
    });

    check("altered source, output, status, owner, language, map and provenance are rejected",[&] {
        const auto target=[&](const JsonObject& root) {
            return root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks")
                .GetObjectAt(static_cast<unsigned>(position->block));
        };
        for(const std::string_view mode:{"source","input","stdout","newline","alternative","case","exit0","exit2","bool",
                "missing-status","comparison","blank","owner","identity","edge","wrong-before","unmapped","v1","locale"}) {
            const auto root=JsonObject::Parse(winrt::to_hstring(after.raw_json));const auto block=target(root);
            const auto config=block.GetNamedObject(L"outputCheck"),item=config.GetNamedArray(L"cases").GetObjectAt(0);
            if(mode=="source")block.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(old.initial_code+"\n// altered")));
            if(mode=="input")item.Insert(L"standardInput",JsonValue::CreateStringValue(L"other"));
            if(mode=="stdout"||mode=="newline")item.GetNamedArray(L"outputs").SetAt(0,
                JsonValue::CreateStringValue(mode=="stdout"?L"Error al abrir muestra":L"Error al abrir salida\n"));
            if(mode=="alternative")item.GetNamedArray(L"outputs").Append(JsonValue::CreateStringValue(L"other"));
            if(mode=="case")config.GetNamedArray(L"cases").Append(JsonObject::Parse(item.Stringify()));
            if(mode=="exit0"||mode=="exit2")item.Insert(L"exitCode",JsonValue::CreateNumberValue(mode=="exit0"?0:2));
            if(mode=="bool")item.Insert(L"exitCode",JsonValue::CreateBooleanValue(true));
            if(mode=="missing-status")item.Remove(L"exitCode");
            if(mode=="comparison")config.Insert(L"inputComparison",JsonValue::CreateStringValue(L"tokens"));
            if(mode=="blank")item.Insert(L"explanation",JsonValue::CreateStringValue(L" \t"));
            const auto layout=root.GetNamedObject(L"stateLayout");const auto proofs=layout.GetNamedArray(L"editorial");
            proofs.GetObjectAt(proofs.Size()-1).Insert(L"afterSha256",
                JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(block))));
            if(mode=="owner")root.Insert(L"id",JsonValue::CreateStringValue(L"foreign_owner"));
            if(mode=="identity"){
                const auto ids=layout.GetNamedArray(L"current").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks");
                const auto index=static_cast<unsigned>(position->block),prior=index-1;
                const auto first=ids.GetAt(index),second=ids.GetAt(prior);ids.SetAt(index,second);ids.SetAt(prior,first);
            }
            if(mode=="edge")proofs.RemoveAtEnd();
            if(mode=="wrong-before")proofs.GetObjectAt(proofs.Size()-1).Insert(L"beforeSha256",JsonValue::CreateStringValue(L"0000000000000000000000000000000000000000000000000000000000000000"));
            if(mode=="unmapped")root.Remove(L"stateLayout");
            if(mode=="v1"){
                JsonObject v1;v1.Insert(L"version",JsonValue::CreateNumberValue(1));v1.Insert(L"origin",layout.GetNamedArray(L"origin"));
                v1.Insert(L"current",layout.GetNamedArray(L"current"));root.Insert(L"stateLayout",v1);
            }
            const auto raw=winrt::to_string(root.Stringify());
            rejects([&]{(void)parse_lesson_json(raw,mode=="locale"?"fr":"es");},"An altered U12L26 proposal passed runtime loading");
            if(mode!="locale")rejects([&]{(void)parse_lesson_progress_metadata(raw);},"An altered U12L26 proposal passed progress metadata");
        }
    });

    check("alias adoption and exact reopening preserve state without awarding an unexecuted goal",[&] {
        state::StateStore store(environment.paths.root/L"adoption");auto data=store.load();state::prepare_lesson_state(data,before);
        data.preferences["u12l26-qa"]["opaque"]=std::string(" preserve\r\n保存 ");
        state::Draft draft;draft.source=old.initial_code;draft.input="";draft.selection_start=3;draft.selection_end=9;
        state::put_draft(data,draft_key("programming",before,old),draft);
        state::put_response(data,before,old,{"opaque old correct flag; no execution claimed",true});
        state::put_response(data,"synthetic-later-l26-answer",{"preserve later answer",true});
        narration::History history;
        for(unsigned index=0;index<3;++index)history.entries.push_back({"synthetic-l26-page-"+std::to_string(index),
            "synthetic-stable-"+std::to_string(index),narration::Position{1,static_cast<std::int64_t>(index+17),0,false},
            "synthetic-other-l26-page-"+std::to_string(index)});
        narration::write_history(data,history);state::save_current_page(data,before,4);
        const auto preserved=data;
        state::prepare_lesson_state(data,after);state::validate_lesson_state(data,after);
        require(data==preserved&&!compiler_puzzle_completed(data,after,current)&&compiler_puzzle_case_count(data,after,current)==0,
            "Adopting the L26 alias manufactured completion or rewrote drafts, responses, pauses or preferences");
        store.save(data);const auto loaded=reopen(store);
        require(loaded==preserved&&narration::read_history(loaded)==history&&!compiler_puzzle_completed(loaded,metadata,projected),
            "Reopening an adopted L26 editor lost state or awarded an unexecuted objective");
    });

    if(model_only){
        std::cout<<"Headless U12L26 model: "<<passed<<" groups passed, "<<failed<<" failed; no compiler programs, UI or audio.\n";
        return failed?1:0;
    }
    std::optional<Capture> accepted,rejected;
    check("the unchanged full L26 program produces the exact deliberate opening failure and earns its proof",[&] {
        accepted=run(old.initial_code);const auto& capture=*accepted;
        require(!capture.execution.execution_succeeded&&capture.execution.exit_code==1
            &&capture.execution.standard_output=="Error al abrir salida"&&capture.execution.standard_output.size()==21
            &&capture.execution.standard_error.empty()&&std::filesystem::is_regular_file(capture.result.run_directory/L"inspecciones_entrada.txt"),
            "The unchanged L26 source failed outside its deliberate file-as-folder opening branch");
        require(evaluate_cpp_output_check(*current.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::correct,
            "The actual intended L26 return 1 was confused with a technical runner failure");
    });
    check("a real zero exit with identical stdout is rejected, not converted to intentional failure",[&] {
        const std::string fragment="if (!guardado) { std::cout << \"Error al abrir salida\"; return 1; }";
        auto source=old.initial_code;const auto found=source.find(fragment);
        require(found!=source.npos&&source.find(fragment,found+fragment.size())==source.npos,"L26 exit contrast no longer identifies one exact statement");
        auto zero=fragment;zero.replace(zero.find("return 1;"),9,"return 0;");source.replace(found,fragment.size(),zero);
        rejected=run(source);const auto& capture=*rejected;
        const auto evaluation=evaluate_cpp_output_check(*current.output_check,capture.execution,capture.request.revision);
        require(capture.execution.execution_succeeded&&capture.execution.exit_code==0&&capture.execution.standard_output=="Error al abrir salida"
            &&evaluation.verdict==OutputVerdict::incorrect&&!evaluation.exit_code_correct&&evaluation.stdout_correct,
            "The authored normal nonzero code was ignored when stdout matched");
    });
    check("accepted then rejected recheck restores the current verdict while preserving achievement and independent state",[&] {
        require(accepted.has_value()&&rejected.has_value(),"The two actual L26 captures are missing");
        state::StateStore store(environment.paths.root/L"actual-runs");auto data=store.load();state::prepare_lesson_state(data,after);
        data.preferences["u12l26-qa"]["later-progress"]=std::int64_t{29};
        const std::string good="Correcto\nThe actual opening branch returned 1.",bad="Incorrecto\nThe new process returned 0 instead of 1.";
        require(record_compiler_puzzle_case(data,after,current,accepted->request,accepted->execution,good)
            &&compiler_puzzle_completed(data,after,current),"The actual correct L26 execution did not earn its new receipt");
        const auto achievement=data;
        require(!record_compiler_puzzle_case(data,after,current,rejected->request,rejected->execution,bad)&&data==achievement,
            "An old achievement bypassed current rechecking or was erased by the rejection");
        require(record_compiler_puzzle_review(data,after,current,rejected->request,rejected->execution,bad),
            "The rejected L26 recheck could not store its exact current feedback");
        store.save(data);data=reopen(store);const auto feedback=compiler_puzzle_current_feedback(data,after,current,rejected->request);
        require(feedback&&!feedback->correct&&feedback->text==bad&&compiler_puzzle_completed(data,metadata,projected)
            &&compiler_puzzle_feedback(data,after,current)==good,"L26 reopening hid the new rejection or changed earned history");
        invalidate_compiler_puzzle_attempt(data,after,current,accepted->request);
        require(data==achievement,"Editing back to another exact program erased more than the current review");
        auto stale=accepted->execution;--stale.source_revision;
        require(!record_compiler_puzzle_case(data,after,current,accepted->request,stale,"stale")&&data==achievement,
            "Stale L26 execution was accepted after completion");
        const auto later=data.preferences.at("u12l26-qa");reset_compiler_puzzle(data,after,current);
        require(!compiler_puzzle_completed(data,after,current)&&compiler_puzzle_case_count(data,after,current)==0
            &&data.preferences.at("u12l26-qa")==later,"Explicit L26 reset kept its proof or erased unrelated later progress");
        store.save(data);require(reopen(store)==data&&!compiler_puzzle_completed(reopen(store),metadata,projected),
            "Reset L26 requirement changed after a real StateStore reopen");
    });
    check("two real programs only and active curriculum unchanged",[&] {
        require(runs==2,"U12L26 exit QA omitted or repeated a compiler program");
        require(state::sha256(read(active_path))==active_hash,"U12L26 QA modified the active curricular asset");
    });
    std::cout<<"Headless U12L26 exit: "<<passed<<" groups passed, "<<failed<<" failed; "<<runs<<" real programs; no UI or audio.\n";
    return failed?1:0;
} catch(const std::exception& error) {std::cerr<<"FAIL U12L26 exit setup: "<<error.what()<<'\n';return 1;}
catch(const winrt::hresult_error& error) {std::cerr<<"FAIL U12L26 exit Windows setup: "<<error.code().value<<'\n';return 1;}
catch(...) {std::cerr<<"FAIL U12L26 exit setup: unexpected exception\n";return 1;}
