// Real Windows runner and receipt QA for two original source-protected goals.
// The public output is deliberately insufficient: the private process must
// actually run before a rejected attempt can be restored. Curricular files are
// read-only; no successful private capture is fabricated.
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    require(stream.good(),"Missing original source or authoring reference");
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
const Block& owner(const Lesson& lesson,std::string_view activity) {
    const auto position=stable_block_position(lesson,activity);
    require(position.has_value(),"Original private-check owner is missing");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
void preserve(const state::StateData& current,const state::StateData& previous) {
    for(const auto& [domain,values]:previous.preferences) {
        const auto found=current.preferences.find(domain);
        require(found!=current.preferences.end(),"An existing preference domain disappeared");
        for(const auto& [key,value]:values) {
            const auto stored=found->second.find(key);
            require(stored!=found->second.end()&&stored->second==value,
                "A rejected attempt rewrote prior progress, feedback or preferences");
        }
    }
}
std::string key(const Lesson& lesson,const Block& block) {
    return "compiler-puzzle/v1/"+lesson.locale+"/"+lesson.id+"/"+
        exercise_key(lesson,block)+"/"+
        state::sha256(reference_objective_frame(lesson,block))+"/";
}
struct Programs { std::string good,bad; };
Programs programs(const Block& block,bool clone) {
    Programs result{block.initial_code,block.initial_code};
    if(clone) {
        require(block.polymorphic_clone_probe.has_value(),"Missing clone policy");
        const auto& policy=*block.polymorphic_clone_probe;
        const auto spans=cpp_polymorphic_clone_detail::spans(
            policy,policy.original_source,false);
        require(spans&&spans->size()==3,"Original clone bodies are not unique");
        const std::array<std::string,3> good{{
            " return std::make_unique<MuestraSimple>(numero()); ",
            " return std::make_unique<MuestraIdentificada>(numero(),codigo()); ",
            R"cpp(
    Muestras copias;
    for(const auto& item:origen)copias.push_back(item->clonar());
    return copias;
)cpp"
        }};
        auto bad=good;
        bad[2]=R"cpp(
    Muestras copias;
    for(const auto& item:origen) {
        if(const auto* simple=dynamic_cast<const MuestraSimple*>(item.get()))
            copias.push_back(simple->clonar());
        else if(const auto* identificada=dynamic_cast<const MuestraIdentificada*>(item.get()))
            copias.push_back(identificada->clonar());
    }
    return copias;
)cpp";
        for(std::size_t i=good.size();i!=0;) {
            --i;const auto span=spans->at(i);
            result.good.replace(span.begin,span.end-span.begin,good[i]);
            result.bad.replace(span.begin,span.end-span.begin,bad[i]);
        }
        require(cpp_polymorphic_clone_matches(policy,result.good)&&
                cpp_polymorphic_clone_matches(policy,result.bad),
            "Clone candidates changed protected source");
    } else {
        require(block.dual_interface_probe.has_value(),"Missing dual-interface policy");
        const auto& policy=*block.dual_interface_probe;
        const auto spans=cpp_multi_body_detail::spans(
            policy.original_source,cpp_dual_interface_detail::bodies(policy),false);
        require(spans&&spans->size()==2,"Original dual-interface bodies are not unique");
        const std::array<std::string,2> good{{
            " return valor_; "," salida << codigo_ << '=' << valor_; "
        }};
        const std::array<std::string,2> bad{{
            good[0]," (void)salida; std::cout << codigo_ << '=' << valor_; "
        }};
        for(std::size_t i=good.size();i!=0;) {
            --i;const auto span=spans->at(i);
            result.good.replace(span.begin,span.end-span.begin,good[i]);
            result.bad.replace(span.begin,span.end-span.begin,bad[i]);
        }
        require(cpp_dual_interface_matches(policy,result.good)&&
                cpp_dual_interface_matches(policy,result.bad),
            "Dual-interface candidates changed protected source");
    }
    return result;
}
std::string fingerprint(const CppRunRequest& request,bool clone) {
    return clone?cpp_polymorphic_clone_request_fingerprint(request):
        cpp_dual_interface_request_fingerprint(request);
}
template<class Function>
void with_evidence(CppRunResult& result,bool clone,Function function) {
    if(clone) {
        require(result.polymorphic_clone_evidence.has_value(),"Missing actual clone execution");
        function(*result.polymorphic_clone_evidence);
    } else {
        require(result.dual_interface_evidence.has_value(),"Missing actual dual-interface execution");
        function(*result.dual_interface_evidence);
    }
}
void without_probe(CppRunRequest& request) {
    request.polymorphic_clone_probe.reset();
    request.dual_interface_probe.reset();
}
void require_public_success(const CppRunResult& result) {
    require(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded(),
        "The original public program did not compile and execute successfully: "+
        result.error_utf8+result.compilation.stdout_bytes+result.compilation.stderr_bytes+
        result.execution.stderr_bytes);
}
void saved_rejection(const state::StateData& data,const Lesson& lesson,const Block& block,
        const CppRunRequest& request,std::string_view feedback,bool completed) {
    require(compiler_puzzle_review(data,lesson,block,request)==feedback,
        "The exact private rejection was not restored");
    const auto shown=compiler_puzzle_current_feedback(data,lesson,block,request);
    require(shown&&!shown->correct&&shown->text==feedback&&shown->source_path.empty(),
        "A private rejection was presented as success or borrowed its explanation source");
    require(compiler_puzzle_completed(data,lesson,block)==completed,
        "The latest rejected attempt changed the scope of an earned achievement");
}
void rejected_captures(const Lesson& lesson,const Block& block,const CppRunRequest& request,
        const CppRunResult& result,const FreshExecution& capture,
        const state::StateData& before,bool clone) {
    const auto reject=[&](const CppRunRequest& candidate,const FreshExecution& observed,
            std::string_view reason) {
        auto data=before;
        require(!record_compiler_puzzle_review(data,lesson,block,candidate,observed,
                    "Unbound private-check feedback")&&data==before,reason);
    };
    const auto reject_result=[&](const CppRunResult& altered,std::string_view reason) {
        const auto observed=capture_cpp_execution(request,altered);
        require(observed.private_probe_review_fingerprint.empty(),reason);
        reject(request,observed,reason);
    };
    for(int index=0;index<16;++index) {
        auto altered=result;
        if(index==0) {
            if(clone)altered.polymorphic_clone_evidence.reset();
            else altered.dual_interface_evidence.reset();
        } else if(index==1)++altered.request_id;
        else if(index==2)++altered.revision;
        else if(index==3)altered.error_utf8="No actual completed capture";
        else with_evidence(altered,clone,[&](auto& proof) {
            if(index==4)proof.driver_sha256=std::string(64,'0');
            if(index==5)proof.program_sha256=std::string(64,'0');
            if(index==6)proof.request_fingerprint="other source/spec/request";
            if(index==7)proof.compilation.status=CppPhaseStatus::launch_failed;
            if(index==8)proof.compilation.exit_code=1;
            if(index==9)proof.execution.status=CppPhaseStatus::not_started;
            if(index==10)proof.execution.status=CppPhaseStatus::timed_out;
            if(index==11)proof.execution.status=CppPhaseStatus::cancelled;
            if(index==12)proof.execution.status=CppPhaseStatus::output_limit;
            if(index==13)proof.execution.status=CppPhaseStatus::launch_failed;
            if(index==14)proof.execution.exit_code.reset();
            if(index==15)proof.compilation.status=CppPhaseStatus::cancelled;
        });
        reject_result(altered,"An absent, altered or incomplete private run created a rejection");
    }
    auto public_request=request;without_probe(public_request);
    const auto public_capture=capture_cpp_execution(public_request,result);
    require(public_capture.compilation_succeeded&&public_capture.execution_succeeded&&
            public_capture.private_probe_review_fingerprint.empty(),
        "The same actual public run invented a private observation");
    reject(request,public_capture,"A copied public-only capture created a private rejection");
    auto missing=capture;missing.private_probe_review_fingerprint.clear();
    reject(request,missing,"Missing private observation was accepted");
    auto foreign=capture;foreign.private_probe_review_fingerprint="unrelated";
    reject(request,foreign,"Another private observation was accepted");
    for(int index=0;index<8;++index) {
        auto changed=request;
        if(index==0)++changed.request_id;
        if(index==1)++changed.revision;
        if(index==2)changed.source_utf8+="\n// different protected source\n";
        if(index==3)changed.stdin_utf8+=" ";
        if(index==4)changed.project_key+="/other";
        if(index==5)without_probe(changed);
        if(index==6)changed.action=CppRunRequest::Action::configure;
        if(index==7) {
            if(clone)changed.polymorphic_clone_probe->original_source+="\n";
            else changed.dual_interface_probe->report_separator=":";
        }
        reject(changed,capture,"Source, spec, phase or request identity borrowed a private rejection");
    }
    // These APIs consume genuine CppRunResult data. The following mutated
    // objects are rejection-only tests; no artificial success is registered.
    std::cout<<"PASS private-review negative captures: missing/altered evidence, public-only capture, "
        "source/spec/request/input/phase changes and incomplete private phases.\n";
}
void saved_tamper(const state::StateData& saved,const Lesson& lesson,const Block& block,
        const CppRunRequest& request,std::string_view feedback,bool clone) {
    const auto prefix=key(lesson,block)+"review/";
    for(const auto* suffix:{"rules","feedback","seal"}) {
        auto invalid=saved;invalid.preferences["native_windows"][prefix+suffix]=std::string("foreign");
        require(compiler_puzzle_review(invalid,lesson,block,request).empty(),
            "Altered saved private rejection was restored");
    }
    auto invalid=saved;
    invalid.preferences["native_windows"][prefix+"capture"]=std::vector<std::string>{};
    require(compiler_puzzle_review(invalid,lesson,block,request).empty(),
        "An empty saved private observation was restored");
    auto reopened_request=request;
    reopened_request.request_id+=1000;reopened_request.revision+=1000;
    require(compiler_puzzle_review(saved,lesson,block,reopened_request)==feedback,
        "New ephemeral IDs on reopening discarded an authentic review");
    for(int index=0;index<5;++index) {
        auto changed=request;auto changed_owner=block;
        if(index==0)changed.source_utf8+="\n// changed draft\n";
        if(index==1)changed.stdin_utf8+=" ";
        if(index==2)changed.project_key+="/other";
        if(index==3) {
            if(clone) {
                changed.polymorphic_clone_probe->original_source+="\n";
                changed_owner.polymorphic_clone_probe=changed.polymorphic_clone_probe;
            } else {
                changed.dual_interface_probe->report_separator=":";
                changed_owner.dual_interface_probe=changed.dual_interface_probe;
            }
        }
        if(index==4)without_probe(changed);
        require(compiler_puzzle_review(saved,lesson,changed_owner,changed).empty(),
            "A saved private rejection followed another source, input, owner contract or project");
    }
}
void execute(bool clone) {
    const std::string unit=clone?"U8L10":"U10L1";
    const auto source_root=std::filesystem::path(build::source_root);
    const auto reference_path=source_root/"docs/examples/p3_ownership_authoring.json";
    const auto reference_bytes=read(reference_path);
    const auto reference=JsonObject::Parse(winrt::to_hstring(reference_bytes));
    require(reference.GetNamedNumber(L"schemaVersion")==1&&
            reference.GetNamedString(L"goal")==L"P3_ACREDITACION_TRANSFERENCIA_DE_PROPIEDAD"&&
            reference.GetNamedArray(L"entries").Size()==14,
        "The shared original-owner authoring reference changed");
    JsonObject entry{nullptr};
    for(const auto& value:reference.GetNamedArray(L"entries"))
        if(value.GetObject().GetNamedString(L"unit")==winrt::to_hstring(unit)) {
            require(!entry,"The authoring reference duplicated an owner");
            entry=value.GetObject();
        }
    require(static_cast<bool>(entry),"Missing original-owner authoring entry");
    const auto lesson_path=source_root/std::filesystem::path(
        winrt::to_string(entry.GetNamedString(L"lesson")));
    const auto original_bytes=read(lesson_path);
    const auto original=parse_lesson_json(original_bytes,"es");
    const auto declaration=entry.GetNamedObject(L"declaration");
    const auto activity=winrt::to_string(declaration.GetNamedString(L"activity"));
    const auto root_field=entry.GetNamedString(L"rootField");
    const auto& before=owner(original,activity);
    const auto case_count=clone?std::size_t{5}:std::size_t{3};
    require(before.type==BlockType::cpp_compiler&&before.output_check&&
            before.output_check->cases.size()==case_count&&!before.polymorphic_clone_probe&&
            !before.dual_interface_probe,"The original owner or public case contract changed");
    auto document=JsonObject::Parse(winrt::to_hstring(original_bytes));
    require(!document.HasKey(root_field),"The read-only fixture already contains the private declaration");
    JsonArray declarations;declarations.Append(declaration);
    document.Insert(root_field,declarations);
    const auto revised_bytes=winrt::to_string(document.Stringify());
    const auto lesson=parse_lesson_json(revised_bytes,"es");
    const auto progress=parse_lesson_progress_metadata(revised_bytes,"es");
    const auto& block=owner(lesson,activity);
    const auto& compact=owner(progress,activity);
    require(block.polymorphic_clone_probe==compact.polymorphic_clone_probe&&
            block.dual_interface_probe==compact.dual_interface_probe&&
            block.output_check&&compact.output_check&&
            block.output_check->cases.size()==case_count&&compact.output_check->cases.size()==case_count&&
            block.initial_code==before.initial_code&&block.stable_id==before.stable_id&&
            exercise_key(lesson,block)==exercise_key(original,before)&&
            draft_key("programming",lesson,block)==draft_key("programming",original,before)&&
            lesson.state_layout.raw_json==original.state_layout.raw_json&&
            progress.state_layout.raw_json==original.state_layout.raw_json&&
            JsonObject::Parse(winrt::to_hstring(block.raw_json)).Stringify()==
                JsonObject::Parse(winrt::to_hstring(before.raw_json)).Stringify(),
        "Root-only activation changed original source, cases, state or saved addresses");
    const auto candidates=programs(block,clone);
    const LaunchOptions options{ProfileId::Codex,
        std::wstring(L"private-review-")+(clone?L"clone":L"dual")+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),
        "Private-review QA would open an existing or personal profile");
    initialize_profile_directories(environment.paths);
    const auto request=[&](std::string source,std::uint64_t id) {
        CppRunRequest run;run.request_id=id;run.revision=id+100;
        run.source_utf8=std::move(source);run.stdin_utf8=block.output_check->cases.front().standard_input;
        run.projects_directory=environment.paths.projects/L"Private & (review)";
        run.project_key=draft_key("programming",lesson,block);
        run.compile_timeout=std::chrono::seconds(90);run.execution_timeout=std::chrono::seconds(10);
        run.polymorphic_clone_probe=block.polymorphic_clone_probe;
        run.dual_interface_probe=block.dual_interface_probe;
        return run;
    };
    auto old_request=request(candidates.good,501);without_probe(old_request);
    const auto old_result=run_cpp(old_request);
    require_public_success(old_result);
    const auto old_capture=capture_cpp_execution(old_request,old_result);
    state::StateData data;state::prepare_lesson_state(data,original);
    data.preferences["qa-other-course"]["unchanged"]=std::string("original unrelated state");
    state::Draft draft;draft.source=candidates.bad;draft.input=old_request.stdin_utf8;
    draft.selection_start=4;draft.selection_end=8;
    const auto draft_identity=draft_key("programming",lesson,block);
    state::put_draft(data,draft_identity,draft);
    require(record_compiler_puzzle_case(data,original,before,old_request,old_capture,
                "Historic first-case stdout")&&compiler_puzzle_completed(data,original,before)&&
            compiler_puzzle_case_count(data,original,before)==1,
        "A genuine historical first-case execution could not be stored");
    const auto legacy=data;
    state::prepare_lesson_state(data,lesson);
    require(!compiler_puzzle_completed(data,lesson,block),
        "Historical stdout was promoted into the newly activated private criterion");

    const auto bad=request(candidates.bad,502);
    require(compiler_runtime_request_allowed(block,bad),"Bad candidate must preserve its source contract");
    const auto bad_result=run_cpp(bad);require_public_success(bad_result);
    const auto bad_capture=capture_cpp_execution(bad,bad_result);
    require(evaluate_cpp_output_check(*block.output_check,bad_capture,bad.revision).verdict==OutputVerdict::correct&&
            !compiler_independent_checks_satisfied(block,bad_capture)&&
            !bad_capture.private_probe_review_fingerprint.empty()&&
            bad_capture.private_probe_review_fingerprint==fingerprint(bad,clone),
        "Actual public-correct/private-failed execution was not observed");
    const std::string rejected="Incorrecto. La comprobacion privada no acredito el contrato del programa.";
    const auto unchanged=data;
    require(!record_compiler_puzzle_case(data,lesson,block,bad,bad_capture,rejected)&&data==unchanged,
        "A private failure earned credit or changed the prior achievement");
    // This assertion fails with a243f598: its review route rejects stdout-correct
    // failures of these two real private contracts instead of preserving them.
    require(record_compiler_puzzle_review(data,lesson,block,bad,bad_capture,rejected),
        "A real private rejection with correct stdout was not saved");
    saved_rejection(data,lesson,block,bad,rejected,false);
    preserve(data,legacy);
    rejected_captures(lesson,block,bad,bad_result,bad_capture,data,clone);
    saved_tamper(data,lesson,block,bad,rejected,clone);
    state::StateData disk;
    const auto round_trip=[&](const state::StateData& expected) {
        {
            state::StateStore writer(environment.paths.root);
            require(writer.load()==disk,"Physical state changed before its explicit save");
            writer.save(expected);
        }
        state::StateStore reader(environment.paths.root);
        const auto reopened=reader.load();
        require(reopened==expected,"Physical reopen changed private feedback or historical state");
        disk=expected;
        return reopened;
    };
    data=round_trip(data);
    saved_rejection(data,progress,compact,bad,rejected,false);
    require(compiler_puzzle_completed(data,original,before)&&
            state::get_draft(data,draft_identity,block.initial_code).source==draft.source&&
            state::get_draft(data,draft_identity,block.initial_code).input==draft.input,
        "Reopening lost the old stdout scope or exact saved draft");
    state::validate_lesson_state(data,progress);

    const auto good=request(candidates.good,503);
    require(compiler_runtime_request_allowed(block,good),"Good candidate failed the original-source gate");
    const auto good_result=run_cpp(good);require_public_success(good_result);
    const auto good_capture=capture_cpp_execution(good,good_result);
    require(evaluate_cpp_output_check(*block.output_check,good_capture,good.revision).verdict==OutputVerdict::correct&&
            compiler_independent_checks_satisfied(block,good_capture)&&
            record_compiler_puzzle_case(data,lesson,block,good,good_capture,"Private criterion verified")&&
            compiler_puzzle_completed(data,lesson,block)&&compiler_puzzle_case_count(data,lesson,block)==1&&
            compiler_puzzle_review(data,lesson,block,bad).empty(),
        "A genuine first-case private success did not clear only its previous rejection");
    preserve(data,legacy);
    const auto earned=data;
    require(compiler_puzzle_completed(earned,original,before),
        "Private success removed the historical output achievement");

    // A later bad draft is a new actual execution, not replay of the first
    // failed capture. The earlier successful program stays earned independently.
    const auto later=request(candidates.bad,504);
    const auto later_result=run_cpp(later);require_public_success(later_result);
    const auto later_capture=capture_cpp_execution(later,later_result);
    require(!compiler_independent_checks_satisfied(block,later_capture)&&
            later_capture.private_probe_review_fingerprint==fingerprint(later,clone),
        "The later rejected draft did not receive its own private observation");
    require(!record_compiler_puzzle_case(data,lesson,block,later,later_capture,rejected)&&data==earned&&
            record_compiler_puzzle_review(data,lesson,block,later,later_capture,rejected),
        "Rechecking a changed bad draft revoked an earned program or lost its rejection");
    preserve(data,earned);
    saved_rejection(data,lesson,block,later,rejected,true);
    data=round_trip(data);
    saved_rejection(data,progress,compact,later,rejected,true);
    preserve(data,legacy);preserve(data,earned);
    require(compiler_puzzle_completed(data,original,before)&&
            compiler_puzzle_case_count(data,progress,compact)==1&&
            compiler_puzzle_review(data,lesson,block,good).empty()&&
            state::string_value(data,"qa-other-course","unchanged")=="original unrelated state",
        "Final reopen promoted another case, replayed another draft or changed unrelated state");
    state::validate_lesson_state(data,progress);
    require(read(lesson_path)==original_bytes&&read(reference_path)==reference_bytes,
        "Runtime QA rewrote the original lesson or authoring reference");
    std::cout<<"PASS "<<unit<<": actual public/private runner observations, original full/progress loaders, "
        "stdout-only history not promoted, one genuine required case, rejected draft restored before and "
        "after achievement, exact StateStore reopen and unchanged publication SHA256 "
        <<state::sha256(original_bytes)<<". No UI/audio acceptance claimed.\n";
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::string_view mode=argv[1];
    if(mode!="--polymorphic-clone"&&mode!="--dual-interface")return 2;
    try {
        winrt::init_apartment();
        execute(mode=="--polymorphic-clone");
        return 0;
    } catch(const winrt::hresult_error& error) {
        std::cerr<<"FAIL private probe review: "<<winrt::to_string(error.message())<<'\n';
    } catch(const std::exception& error) {
        std::cerr<<"FAIL private probe review: "<<error.what()<<'\n';
    }
    return 1;
}

