#include "platform/windows/cpp_history_training.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "core/cmake_selective_recovery.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
using Stage=CmakeSelectiveRecoveryTrace::Stage;

void check(bool ok,const char* issue){if(!ok)throw std::runtime_error(issue);}
std::string output(bool fail);
curriculum::Block published(){
    curriculum::Block block;
    block.type=curriculum::BlockType::cpp_compiler;
    block.cmake=curriculum::CmakeConfig{"comprobar_cajas",{},"Debug"};
    block.project_files={
        {"CMakeLists.txt","cmake_minimum_required(VERSION 3.20)\nproject(Envios LANGUAGES CXX)\nadd_subdirectory(componente)\n"},
        {"componente/CMakeLists.txt","add_library(envios src/cajas.cpp)\n"},
        {"componente/publico/envio/cajas.h","int cajas_para(int piezas);\n"},
        {"componente/src/cajas.cpp","#include \"envio/cajas.h\"\nint cajas_para(int piezas) {\n    return (piezas + 3) / 4;\n}\n"},
        {"app/consulta.cpp","int main() { return 0; }\n"},
        {"pruebas/cajas_casos.cpp","int main() { return 0; }\n"}
    };
    curriculum::OutputCase goal;goal.outputs={output(false)};
    goal.executable="comprobar_cajas";
    block.output_check=curriculum::OutputCheck{};
    block.output_check->cases.push_back(std::move(goal));
    return block;
}
constexpr std::string_view source_path="componente/src/cajas.cpp";
constexpr std::string_view root_path="CMakeLists.txt";
std::string value(const curriculum::CppProjectFiles& files,std::string_view path){
    for(const auto& [name,source]:files)if(name==path)return source;
    throw std::runtime_error("Unknown source");
}
curriculum::CppProjectFiles broken(curriculum::CppProjectFiles base){
    for(auto& [path,source]:base){
        if(path==root_path)source+="# Se conserva el comentario de la raíz.\n";
        if(path==source_path){
            const auto position=source.find("return (piezas + 3) / 4;");
            check(position!=source.npos,"No original formula");
            source.replace(position,std::string("return (piezas + 3) / 4;").size(),"return piezas / 4;");
        }
    }
    return base;
}
curriculum::CppProjectFiles restored(curriculum::CppProjectFiles broken,
                                     const curriculum::CppProjectFiles& base){
    for(auto& [path,source]:broken)if(path==source_path)source=value(base,source_path);
    return broken;
}
CppHistoryRecordEvidence registered(const curriculum::CppProjectFiles& base){
    CppHistoryRecordEvidence e;e.revision_id=std::string(64,'a');e.recorded_file_count=base.size();
    for(const auto& [path,source]:base)e.file_sha256.emplace(path,state::sha256(source));
    return e;
}
CppHistoryComparisonEvidence compared(const curriculum::CppProjectFiles& base,
                                    const curriculum::CppProjectFiles& bad,std::string path){
    return {std::string(64,'a'),path,state::sha256(value(base,path)),
        state::sha256(value(bad,path)),1,1,true};
}
CppHistoryRecoveryEvidence recovery(const curriculum::CppProjectFiles& base,
                                   const curriculum::CppProjectFiles& bad){
    return {std::string(64,'a'),std::string(source_path),
        state::sha256(value(bad,source_path)),
        state::sha256(value(base,source_path)),base.size()-1,true};
}
std::string output(bool fail){
    std::string text;
    constexpr std::array<std::pair<int,int>,7> cases{{{0,0},{4,1},{8,2},{20,5},{1,1},{5,2},{17,5}}};
    for(const auto [pieces,expected]:cases) {
        auto actual=fail?pieces/4:(pieces+3)/4;
        text+="piezas="+std::to_string(pieces)+" esperado="+std::to_string(expected)
            +" obtenido="+std::to_string(actual)+(actual==expected?" OK\n":" FALLO\n");
    }
    text+=fail?"Bateria: FALLO\n":"Bateria: OK\n";
    return text;
}
CppRunRequest request(curriculum::CppProjectFiles files,CppRunRequest::Action action,
                      std::uint64_t id,std::uint64_t revision) {
    CppRunRequest r;
    r.request_id=id;r.revision=revision;
    r.project_key="qa/u3/history";r.projects_directory=L"C:\\LGAED-QA\\history-tests";
    r.project_files=std::move(files);r.cmake=curriculum::CmakeConfig{"comprobar_cajas",{},"Debug"};
    r.action=action;
    return r;
}
CppRunResult result(const CppRunRequest& r,bool broken_run){
    CppRunResult v;v.request_id=r.request_id;v.revision=r.revision;
    v.project_fingerprint=std::string(64,broken_run?'b':'c');
    v.configuration_fingerprint=std::string(64,'d');
    v.run_directory=L"C:\\LGAED-QA\\history-tests\\owned-run";
    v.artifact=L"C:\\LGAED-QA\\history-tests\\built\\comprobar_cajas.exe";
    v.target_type="EXECUTABLE";
    v.compilation.status=CppPhaseStatus::completed;v.compilation.exit_code=0;
    if(r.action==CppRunRequest::Action::execute||r.action==CppRunRequest::Action::compile_run){
        v.executable="comprobar_cajas";
        v.execution.status=CppPhaseStatus::completed;
        v.execution.exit_code=broken_run?1u:0u;
        v.execution.stdout_bytes=output(broken_run);
    }
    return v;
}
void full_sequence(){
    const auto p=published();
    const auto bad=broken(p.project_files),good=restored(bad,p.project_files);
    const auto base=registered(p.project_files);
    CmakeSelectiveRecoveryTrace trace(p);
    check(!trace.complete()&&trace.stage()==Stage::unrecorded,"Empty tracker gave credit");
    const auto badbuild=request(bad,CppRunRequest::Action::build,1,2);
    const auto badrun=request(bad,CppRunRequest::Action::execute,2,2);
    const auto fixedbuild=request(good,CppRunRequest::Action::build,3,3);
    const auto fixedrun=request(good,CppRunRequest::Action::execute,4,3);
    check(!trace.run(badbuild,result(badbuild,true)),"Build without registration gave credit");
    check(trace.record(base),"The original revision was not recorded");
    check(!trace.run(badrun,result(badrun,true)),"Execution before build gave credit");
    check(trace.run(badbuild,result(badbuild,true)),"The defective CMake build was not observed");
    auto not_defective=result(badrun,true);not_defective.execution.exit_code=0;
    check(!trace.run(badrun,not_defective),"A good exit invented the defect");
    check(trace.run(badrun,result(badrun,true)),"The actual defect output was not observed");
    check(!trace.restore(recovery(p.project_files,bad)),"Restoration without two comparisons was accepted");
    check(trace.compare(compared(p.project_files,bad,std::string(source_path))),"Source comparison not recorded");
    auto wrong_compare=compared(p.project_files,bad,std::string(root_path));
    wrong_compare.revision_id=std::string(64,'f');
    check(!trace.compare(wrong_compare),"A different revision satisfied the second comparison");
    check(trace.compare(compared(p.project_files,bad,std::string(root_path))),"Second file comparison not recorded");
    auto bad_restore=recovery(p.project_files,bad);bad_restore.preserved_other_files=0;
    check(!trace.restore(bad_restore),"Restoring other files was accepted as selective");
    check(trace.restore(recovery(p.project_files,bad)),"Selective recovery of cajas.cpp was not observed");
    check(!trace.run(fixedrun,result(fixedrun,false)),"A correct output before rebuild gave credit");
    auto forged_build=fixedbuild;forged_build.project_files=p.project_files;
    check(!trace.run(forged_build,result(forged_build,false)),
        "Rebuilding the pristine starter instead of preserving the root comment was accepted");
    check(trace.run(fixedbuild,result(fixedbuild,false)),"The new CMake build was not observed");
    auto bad_final=result(fixedrun,false);bad_final.execution.stdout_bytes=output(true);
    check(!trace.run(fixedrun,bad_final),"Stale defective stdout completed the sequence");
    check(trace.run(fixedrun,result(fixedrun,false)),"Fresh repaired execution did not complete sequence");
    check(trace.complete()&&trace.stage()==Stage::verified,"Complete action chain was not recognized");
    check(trace.record(base)&&!trace.complete(),"Recording a new revision reused the previous completion");
}
void comments_between_unchanged_commands_are_allowed(){
    auto p=published();
    auto bad=broken(p.project_files);
    for(auto& [path,code]:bad)if(path==root_path) {
        const std::string comment="# Se conserva el comentario de la raíz.\n";
        check(code.ends_with(comment),"The appended QA comment was lost");
        code.resize(code.size()-comment.size());
        const auto first=code.find('\n');
        check(first!=code.npos,"The first CMake command has no newline");
        code.insert(first+1,comment);
    }
    CmakeSelectiveRecoveryTrace trace(p);
    check(trace.record(registered(p.project_files)),"The original snapshot did not record");
    auto a=request(bad,CppRunRequest::Action::build,10,5);
    auto x=request(bad,CppRunRequest::Action::execute,11,5);
    check(trace.run(a,result(a,true))&&trace.run(x,result(x,true)),
        "A harmless inserted comment should not invalidate the failing build");
    check(trace.compare(compared(p.project_files,bad,std::string(root_path)))&&
        trace.compare(compared(p.project_files,bad,std::string(source_path))),
        "Both files with the inserted comment must be compared");
    check(trace.restore(recovery(p.project_files,bad)),"Source restoration failed");
    auto fixed=restored(bad,p.project_files);
    auto b=request(fixed,CppRunRequest::Action::build,12,6);
    auto y=request(fixed,CppRunRequest::Action::execute,13,6);
    check(trace.run(b,result(b,false))&&trace.run(y,result(y,false))&&trace.complete(),
        "A valid comment between CMake commands was mistaken for a source edit");
}
void inline_cmake_comment_is_valid_but_new_commands_are_not(){
    const auto p=published();
    auto bad=broken(p.project_files);
    for(auto& [path,code]:bad)if(path==root_path){
        const std::string tail="# Se conserva el comentario de la raíz.\n";
        check(code.ends_with(tail),"Original QA standalone comment was lost");
        code.resize(code.size()-tail.size());
        const auto newline=code.find('\n');
        check(newline!=code.npos,"No root CMake command available for inline comment");
        code.insert(newline," # Nota libre junto al comando");
    }
    const auto good=restored(bad,p.project_files);
    CmakeSelectiveRecoveryTrace trace(p);
    check(trace.record(registered(p.project_files)),"Cannot record U3 baseline");
    const auto broken_run=request(bad,CppRunRequest::Action::compile_run,31,7);
    const auto corrected_run=request(good,CppRunRequest::Action::compile_run,32,8);
    auto altered=bad;
    for(auto& [path,code]:altered)if(path==root_path)
        code+="message(STATUS \"Unexpected extra CMake statement\")\n";
    auto changed=request(altered,CppRunRequest::Action::compile_run,33,7);
    check(!trace.run(changed,result(changed,true)),
        "An extra CMake command was treated as a harmless inline comment");
    check(trace.run(broken_run,result(broken_run,true)),
        "A valid inline CMake comment was rejected before the failing run");
    check(trace.compare(compared(p.project_files,bad,std::string(root_path)))&&
          trace.compare(compared(p.project_files,bad,std::string(source_path)))&&
          trace.restore(recovery(p.project_files,bad)),
          "Two real per-file comparisons must still precede inline-comment recovery");
    check(trace.run(corrected_run,result(corrected_run,false))&&trace.complete(),
        "The restored CMake execution rejected an otherwise valid inline comment");
    check(value(good,root_path)==value(bad,root_path),
        "Selective restoration must not erase the valid inline CMake comment");
}
void history_receipt_preserves_old_stdout_and_requires_new_actions(){
    auto block=published();
    block.stable_id="block_0010_0003";
    block.address.page=10;block.address.block=3;
    curriculum::Lesson lesson;lesson.locale="es";
    lesson.id="programming_practice_tools__comprobaciones_repetibles__reorganizar_la_descripcion_conservando_el_comprobador";
    state::StateData stored;
    auto initial=request(block.project_files,CppRunRequest::Action::execute,40,4);
    curriculum::FreshExecution observed;
    observed.source_revision=initial.revision;
    observed.compilation_succeeded=true;observed.execution_succeeded=true;
    observed.execution_completed=true;observed.exit_code=0;
    observed.executable="comprobar_cajas";
    observed.standard_output=output(false);
    check(record_compiler_puzzle_case(stored,lesson,block,initial,observed,"Correcto"),
        "The old stdout-only case no longer accepts its original output");
    check(!compiler_puzzle_completed(stored,lesson,block)&&
        compiler_puzzle_case_count(stored,lesson,block)==0,
        "An old output-only receipt unlocked the new selective-recovery sequence");
    auto unchanged=stored;
    auto edited=initial;edited.project_files.front().second+="# Comentario nuevo\n";
    invalidate_compiler_puzzle_attempt(stored,lesson,block,edited);
    check(stored.preferences==unchanged.preferences,
        "Editing while U3 sequence is pending erased an earned historical stdout-only receipt");

    const auto bad=broken(block.project_files),good=restored(bad,block.project_files);
    CmakeSelectiveRecoveryTrace trace(block);
    auto final=request(good,CppRunRequest::Action::execute,44,6);
    observed.source_revision=final.revision;
    check(!record_compiler_selective_history_completion(stored,lesson,block,final,observed,trace),
        "Output alone created the new history-sequence receipt");
    check(trace.record(registered(block.project_files)),"Cannot register the exact baseline");
    auto badbuild=request(bad,CppRunRequest::Action::build,41,5);
    auto badrun=request(bad,CppRunRequest::Action::execute,42,5);
    auto goodbuild=request(good,CppRunRequest::Action::build,43,6);
    check(trace.run(badbuild,result(badbuild,true)) &&
        trace.run(badrun,result(badrun,true)) &&
        trace.compare(compared(block.project_files,bad,std::string(root_path))) &&
        trace.compare(compared(block.project_files,bad,std::string(source_path))) &&
        trace.restore(recovery(block.project_files,bad)) &&
        trace.run(goodbuild,result(goodbuild,false)) &&
        trace.run(final,result(final,false)),
        "The authenticated action chain did not reach the last exact execution");
    check(trace.completed_by(final),"The completed trace does not belong to its actual final request");
    auto replay=final;replay.request_id=42;
    check(!trace.completed_by(replay),"A different request reused the verified action trace");
    auto replaced=final;replaced.project_files.front().second+="# Otro cambio\n";
    check(!trace.completed_by(replaced),"A changed draft reused the previous action trace");
    check(record_compiler_puzzle_case(stored,lesson,block,final,observed,"Correcto"),
        "The history-authorized fresh output did not match its original case");
    check(!compiler_puzzle_completed(stored,lesson,block),
        "The old receipt unlocked the new curriculum before the independent seal");
    check(!record_compiler_selective_history_completion(stored,lesson,block,replay,observed,trace),
        "A replayed request granted the new history achievement");
    check(record_compiler_selective_history_completion(stored,lesson,block,final,observed,trace),
        "The verified action chain failed to store its independent completion");
    check(compiler_puzzle_completed(stored,lesson,block) &&
        compiler_puzzle_case_count(stored,lesson,block)==1,
        "The saved and sealed new goal was not restored by the normal curriculum gate");
    validate_compiler_puzzle_state(stored,lesson,block);
    auto corrupted=stored;
    bool changed=false;
    for(auto& [key,value]:corrupted.preferences["native_windows"])
        if(key.ends_with("history-sequence/v1")){
            value=std::vector<std::string>{"forged","forged"};changed=true;
        }
    check(changed,"The independent history receipt was not stored");
    check(!compiler_puzzle_completed(corrupted,lesson,block),
        "A forged history receipt was accepted");
    bool rejected=false;
    try{validate_compiler_puzzle_state(corrupted,lesson,block);}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected,"The state validator accepted a forged sequence completion");
    reset_compiler_puzzle(stored,lesson,block);
    check(!compiler_puzzle_completed(stored,lesson,block)&&
        compiler_puzzle_case_count(stored,lesson,block)==0,
        "Explicit Reset did not clear both existing and new completion receipts");
}
void combined_cmake_check_follows_the_real_lesson_button(){
    const auto p=published();
    const auto bad=broken(p.project_files),good=restored(bad,p.project_files);
    const auto record=registered(p.project_files);
    for(const bool explicit_build:{false,true}){
        CmakeSelectiveRecoveryTrace trace(p);
        check(trace.record(record),"Cannot register baseline before combined CMake test");
        const auto bad_build=request(bad,CppRunRequest::Action::build,10,21);
        const auto bad_combined=request(bad,CppRunRequest::Action::compile_run,11,21);
        if(explicit_build)
            check(trace.run(bad_build,result(bad_build,true)),
                "The optional actual CMake build did not register");
        auto wrong=result(bad_combined,true);wrong.execution.exit_code=0;
        check(!trace.run(bad_combined,wrong),"A combined CMake action accepted the wrong exit status");
        check(trace.run(bad_combined,result(bad_combined,true))&&
            trace.stage()==Stage::failed_executed,
            "The real Compile-run-check action did not count its own fresh build and failed run");
        check(trace.compare(compared(p.project_files,bad,std::string(root_path)))&&
            trace.compare(compared(p.project_files,bad,std::string(source_path)))&&
            trace.restore(recovery(p.project_files,bad)),
            "The combined CMake state did not reach the source-only recovery");
        const auto good_build=request(good,CppRunRequest::Action::build,12,22);
        const auto good_combined=request(good,CppRunRequest::Action::compile_run,13,22);
        if(explicit_build)
            check(trace.run(good_build,result(good_build,false)),
                "The optional final CMake build failed");
        check(!trace.run(good_combined,result(good_combined,true)),
            "A broken or replayed output was accepted as a final combined result");
        check(trace.run(good_combined,result(good_combined,false))&&
            trace.complete()&&trace.completed_by(good_combined),
            "The real checked CMake button cannot finish a legitimate full history sequence");
        auto wrong_revision=good_combined;wrong_revision.revision=21;
        check(!trace.completed_by(wrong_revision),"The sealed sequence accepted the prior build revision");
    }
}
void first_output_case_unlocks_without_discarding_optional_evidence(){
    // Synthetic two-input objective: no account profile, CMake build or audio.
    // Its first authored input grants progression. A different second input
    // may be explored freely, but it cannot substitute for the first.
    curriculum::Lesson lesson;lesson.locale="es";lesson.id="qa_first_authored_case";
    curriculum::Block block;block.type=curriculum::BlockType::cpp_compiler;
    block.stable_id="block_0002_0003";block.address.page=2;block.address.block=3;
    block.initial_code="int main() { return 0; }";
    curriculum::OutputCase first;first.standard_input="4\\n";first.outputs={"4\\n"};
    curriculum::OutputCase second;second.standard_input="8\\n";second.outputs={"8\\n"};
    block.output_check=curriculum::OutputCheck{};
    block.output_check->cases={first,second};
    CppRunRequest run;run.action=CppRunRequest::Action::compile_run;
    run.revision=19;run.request_id=101;run.source_utf8=block.initial_code;
    const auto observed=[&](std::string input,std::string output){
        curriculum::FreshExecution result;result.source_revision=run.revision;
        result.compilation_succeeded=true;result.execution_succeeded=true;
        result.execution_completed=true;result.exit_code=0;
        result.standard_input=std::move(input);result.standard_output=std::move(output);
        return result;
    };
    state::StateData data;
    check(!compiler_puzzle_completed(data,lesson,block),"A missing run unlocked an editor");
    run.stdin_utf8=second.standard_input;
    auto other=observed(second.standard_input,second.outputs.front());
    check(!record_compiler_puzzle_case(data,lesson,block,run,other,"Correcto\\nSegundo caso")&&
        compiler_puzzle_case_count(data,lesson,block)==1&&
        !compiler_puzzle_completed(data,lesson,block),
        "The optional second case unlocked progress before the first");
    auto edited=run;edited.source_utf8+="\\n// new unverified draft";
    auto incomplete=data;invalidate_compiler_puzzle_attempt(incomplete,lesson,block,edited);
    check(!compiler_puzzle_completed(incomplete,lesson,block)&&
        compiler_puzzle_case_count(incomplete,lesson,block)==0,
        "An incomplete second-case receipt survived a different program");

    run.stdin_utf8=first.standard_input;
    const auto one=observed(first.standard_input,first.outputs.front());
    auto wrong=one;wrong.standard_output="wrong\\n";
    const auto unchanged=data;
    check(!record_compiler_puzzle_case(data,lesson,block,run,wrong,"Incorrecto")&&
        data==unchanged,"A wrong first output modified an authentic optional receipt");
    check(record_compiler_puzzle_case(data,lesson,block,run,one,"Correcto\\nPrimer caso")&&
        compiler_puzzle_completed(data,lesson,block)&&
        compiler_puzzle_case_count(data,lesson,block)==2,
        "A verified first case failed to complete after an earlier optional case");
    check(state::decode(state::encode(data))==data,
        "First-case receipt changed after state round trip");
    const auto earned=data;
    auto reopened=state::decode(state::encode(data));
    check(compiler_puzzle_completed(reopened,lesson,block),
        "Reopening lost a first-case achievement");

    // A first-only attempt must also unlock with exactly one receipt.
    state::StateData first_only;
    check(record_compiler_puzzle_case(first_only,lesson,block,run,one,"Correcto\\nPrimer caso")&&
        compiler_puzzle_case_count(first_only,lesson,block)==1&&
        compiler_puzzle_completed(first_only,lesson,block),
        "The first case still requires the remaining authored inputs");
    const auto original_feedback=compiler_puzzle_feedback(first_only,lesson,block);
    std::string sealed_first;
    for(const auto& [key,value]:first_only.preferences.at("native_windows"))
        if(key.ends_with("/completed")){sealed_first=std::get<std::string>(value);break;}
    check(!sealed_first.empty(),"A first-only completion seal was not stored");
    run.stdin_utf8=second.standard_input;
    check(record_compiler_puzzle_case(first_only,lesson,block,run,other,"Correcto\\nEnsayo optativo")&&
        compiler_puzzle_case_count(first_only,lesson,block)==2&&
        compiler_puzzle_completed(first_only,lesson,block)&&
        compiler_puzzle_feedback(first_only,lesson,block)==original_feedback,
        "An optional run erased or replaced the first verified achievement");
    auto state_key=[](const state::StateData& state,std::string_view suffix){
        for(const auto& [name,value]:state.preferences.at("native_windows"))
            if(name.ends_with(suffix))return std::get<std::string>(value);
        throw std::runtime_error("Missing first-case receipt suffix");
    };
    check(state_key(first_only,"completed")==sealed_first,
        "Optional second case rewrote the first-case completion seal");
    auto altered=first_only;
    for(auto& [name,value]:altered.preferences["native_windows"])
        if(name.ends_with("/cases")){
            auto& cases=std::get<std::vector<std::string>>(value);
            check(cases.size()==2,"Case count changed after optional run");
            cases[0]="tampered";break;
        }
    check(!compiler_puzzle_completed(altered,lesson,block),
        "A forged first-case receipt passed the progress gate");
    const auto saved=first_only;reset_compiler_puzzle(first_only,lesson,block);
    check(!compiler_puzzle_completed(first_only,lesson,block)&&
        compiler_puzzle_case_count(first_only,lesson,block)==0,
        "Reset retained a first-only achievement");
    check(compiler_puzzle_completed(saved,lesson,block)&&
        compiler_puzzle_completed(earned,lesson,block),
        "Another state mutation revoked independent first-case achievements");
}

constexpr std::string_view individual_success=
    "piezas=1 esperado=1 obtenido=1 OK\nBateria: OK\n";
constexpr std::string_view individual_failure=
    "piezas=1 esperado=1 obtenido=0 FALLO\nBateria: FALLO\n";

curriculum::Block individual_published(){
    auto block=published();
    block.output_check->cases.clear();
    // Independent literal oracles; the runtime helper does not author these.
    constexpr std::array<std::pair<std::string_view,std::string_view>,7> cases{{
        {"1\n",individual_success},
        {"0\n","piezas=0 esperado=0 obtenido=0 OK\nBateria: OK\n"},
        {"4\n","piezas=4 esperado=1 obtenido=1 OK\nBateria: OK\n"},
        {"8\n","piezas=8 esperado=2 obtenido=2 OK\nBateria: OK\n"},
        {"20\n","piezas=20 esperado=5 obtenido=5 OK\nBateria: OK\n"},
        {"5\n","piezas=5 esperado=2 obtenido=2 OK\nBateria: OK\n"},
        {"17\n","piezas=17 esperado=5 obtenido=5 OK\nBateria: OK\n"}
    }};
    for(const auto& [input,expected]:cases){
        curriculum::OutputCase goal;goal.standard_input=input;
        goal.outputs={std::string(expected)};
        block.output_check->cases.push_back(std::move(goal));
    }
    // This is a model fixture, not an executed compiler. Actual published
    // client provenance and compilation are covered by the pinned Win32 QA.
    block.project_files.back().second="// Model of the separately revised stdin checker.\nint main() { return 0; }\n";
    block.compiler_client_epoch=std::string(64,'e');
    return block;
}
CppRunRequest individual_request(curriculum::CppProjectFiles files,CppRunRequest::Action action,
        std::uint64_t id,std::uint64_t revision){
    auto captured=request(std::move(files),action,id,revision);
    captured.stdin_utf8="1\n";
    return captured;
}
CppRunResult individual_result(const CppRunRequest& captured,bool failed){
    auto observed=result(captured,failed);
    if(captured.action==CppRunRequest::Action::execute||
            captured.action==CppRunRequest::Action::compile_run)
        observed.execution.stdout_bytes=failed?individual_failure:individual_success;
    return observed;
}
void individual_goal_contract_is_closed(){
    const auto original=published();
    const auto aggregate=curriculum::selective_recovery::goal(*original.output_check);
    check(aggregate&&!aggregate->individual&&aggregate->standard_input.empty()&&
        aggregate->broken_output==output(true)&&aggregate->restored_output==output(false),
        "The independent contract no longer accepts the historical aggregate");
    const auto p=individual_published();
    const auto current=curriculum::selective_recovery::goal(*p.output_check);
    check(current&&current->individual&&current->standard_input=="1\n"&&
        current->broken_output==individual_failure&&current->restored_output==individual_success,
        "The new goal does not require exactly the first individual failure and success");
    auto reordered=*p.output_check;
    std::reverse(reordered.cases.begin()+1,reordered.cases.end());
    check(curriculum::selective_recovery::goal(reordered).has_value(),
        "Permuting only optional cases changed the required History goal");
    auto bad=reordered;std::swap(bad.cases[0],bad.cases[1]);
    check(!curriculum::selective_recovery::goal(bad),"An optional input became the required History case");
    bad=*p.output_check;bad.cases.pop_back();
    check(!curriculum::selective_recovery::goal(bad),"An individual contract omitted a reviewed example");
    bad=*p.output_check;bad.cases.back()=bad.cases[1];
    check(!curriculum::selective_recovery::goal(bad),"Duplicate inputs hid a missing independent case");
    for(const auto input:{"","1","01\n","1 \n","1\r\n"}){
        bad=*p.output_check;bad.cases[0].standard_input=input;
        check(!curriculum::selective_recovery::goal(bad),"A noncanonical required input was accepted");
    }
    bad=*p.output_check;bad.cases[0].outputs[0].pop_back();
    check(!curriculum::selective_recovery::goal(bad),"The required exact final newline became optional");
    bad=*p.output_check;bad.cases[0].outputs.push_back(output(false));
    check(!curriculum::selective_recovery::goal(bad),"An aggregate output could substitute for one case");
    bad=*p.output_check;bad.cases[0].exit_code=1;
    check(!curriculum::selective_recovery::goal(bad),"An individual success accepted a failing process");
    bad=*p.output_check;bad.cases[0].executable="consultar_cajas";
    check(!curriculum::selective_recovery::goal(bad),"The application replaced the independent checker");
    bad=*p.output_check;bad.cases[0].standard_error_exact="Error\n";
    check(!curriculum::selective_recovery::goal(bad),"The exact clean stderr contract changed");
    bad=*p.output_check;
    for(auto& item:bad.cases){item.executable="comprobar_cajas";item.standard_error_exact="";}
    check(curriculum::selective_recovery::goal(bad).has_value(),
        "Explicit checker aliases and empty stderr were rejected despite equivalent requirements");
    auto missing=p;missing.output_check.reset();
    bool rejected=false;
    try{CmakeSelectiveRecoveryTrace invalid(missing);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"A History trace without an authored output objective was accepted");
}
void individual_sequence_needs_the_first_case_and_every_action(){
    const auto p=individual_published();
    const auto bad=broken(p.project_files),good=restored(bad,p.project_files);
    for(const bool combined:{false,true}){
        CmakeSelectiveRecoveryTrace trace(p);
        check(trace.individual_case(),"The individual trace selected the historical aggregate mode");
        const auto initial=individual_request(p.project_files,CppRunRequest::Action::compile_run,1,1);
        check(!trace.run(initial,individual_result(initial,false)),
            "A correct individual output without recording the baseline gave History credit");
        check(trace.record(registered(p.project_files)),"The individual baseline could not be registered");
        auto build=individual_request(bad,CppRunRequest::Action::build,2,2);
        if(!combined)check(trace.run(build,individual_result(build,true)),
            "The real separate build could not begin the individual sequence");
        const auto failing=individual_request(bad,combined?CppRunRequest::Action::compile_run:
            CppRunRequest::Action::execute,3,2);
        const auto before_failure=trace.stage();
        for(std::size_t index=1;index<p.output_check->cases.size();++index){
            auto optional=failing;optional.stdin_utf8=p.output_check->cases[index].standard_input;
            auto observed=individual_result(optional,false);
            observed.execution.stdout_bytes=p.output_check->cases[index].outputs.front();
            check(!trace.run(optional,observed)&&trace.stage()==before_failure,
                "An optional passing case advanced the required failure sequence");
        }
        auto optional_failed=failing;optional_failed.stdin_utf8="5\n";
        auto failed_five=individual_result(optional_failed,true);
        failed_five.execution.stdout_bytes="piezas=5 esperado=2 obtenido=1 FALLO\nBateria: FALLO\n";
        check(!trace.run(optional_failed,failed_five)&&trace.stage()==before_failure,
            "An optional failing case replaced the first required failure");
        auto mismatch=individual_result(failing,true);mismatch.execution.exit_code=0;
        check(!trace.run(failing,mismatch),"The individual failure accepted a zero exit");
        mismatch=individual_result(failing,true);mismatch.execution.stdout_bytes=output(true);
        check(!trace.run(failing,mismatch),"The previous aggregate replaced a fresh individual failure");
        mismatch=individual_result(failing,true);mismatch.execution.stderr_bytes="Error\n";
        check(!trace.run(failing,mismatch),"An unrelated stderr diagnostic replaced the intended defect");
        mismatch=individual_result(failing,true);++mismatch.request_id;
        check(!trace.run(failing,mismatch),"Another request's failure was attached to this run");
        check(trace.run(failing,individual_result(failing,true))&&trace.stage()==Stage::failed_executed,
            "The first actual individual failure did not advance its build/run sequence");
        check(!trace.restore(recovery(p.project_files,bad)),
            "The individual objective omitted its two required comparisons");
        check(trace.compare(compared(p.project_files,bad,std::string(source_path)))&&
              !trace.restore(recovery(p.project_files,bad))&&
              trace.compare(compared(p.project_files,bad,std::string(root_path))),
            "Both independent diffs were not required before source restoration");
        auto all_files=recovery(p.project_files,bad);all_files.preserved_other_files=0;
        check(!trace.restore(all_files),"Whole-project restoration counted as selective recovery");
        check(trace.restore(recovery(p.project_files,bad)),"The selective source recovery was lost");
        const auto final=individual_request(good,combined?CppRunRequest::Action::compile_run:
            CppRunRequest::Action::execute,5,3);
        if(!combined){
            check(!trace.run(final,individual_result(final,false)),
                "A first-case success before rebuilding the restored source gave credit");
            auto rebuild=individual_request(good,CppRunRequest::Action::build,4,3);
            auto pristine=rebuild;pristine.project_files=p.project_files;
            check(!trace.run(pristine,individual_result(pristine,false)),
                "Restoring the root comment away was accepted in the individual mode");
            check(trace.run(rebuild,individual_result(rebuild,false)),
                "The individual recovery could not rebuild its preserved root");
        }else{
            auto pristine=final;pristine.project_files=p.project_files;
            check(!trace.run(pristine,individual_result(pristine,false)),
                "A combined run removed the independent root comment");
        }
        const auto before_final=trace.stage();
        for(std::size_t index=1;index<p.output_check->cases.size();++index){
            auto optional=final;optional.stdin_utf8=p.output_check->cases[index].standard_input;
            auto observed=individual_result(optional,false);
            observed.execution.stdout_bytes=p.output_check->cases[index].outputs.front();
            check(!trace.run(optional,observed)&&trace.stage()==before_final,
                "Optional success completed the History goal before the required first case");
        }
        mismatch=individual_result(final,false);mismatch.execution.stdout_bytes=output(false);
        check(!trace.run(final,mismatch),"An old aggregate success completed the individual objective");
        mismatch=individual_result(final,false);mismatch.execution.stdout_bytes.pop_back();
        check(!trace.run(final,mismatch),"Missing final newline completed the exact individual goal");
        check(trace.run(final,individual_result(final,false))&&trace.complete()&&trace.completed_by(final),
            "The complete first-case recovery sequence failed without optional executions");
        auto replay=final;replay.stdin_utf8="0\n";
        check(!trace.completed_by(replay),"Changing only stdin reused a first-case History seal");
        auto optional=individual_result(replay,false);
        optional.execution.stdout_bytes=p.output_check->cases[1].outputs.front();
        check(!trace.run(replay,optional)&&trace.complete()&&trace.completed_by(final),
            "An optional run erased the completed first-case trace");
        check(value(good,root_path)==value(bad,root_path),
            "The test failed to retain the independent root comment");
    }
}
void individual_rebuild_after_optional_input_keeps_fresh_action_evidence(){
    const auto p=individual_published();
    const auto bad=broken(p.project_files),good=restored(bad,p.project_files);
    for(const bool combined:{false,true}){
        CmakeSelectiveRecoveryTrace trace(p);
        check(trace.record(registered(p.project_files)),"The input-revision baseline could not be recorded");
        const auto first_build=individual_request(bad,CppRunRequest::Action::build,301,2);
        check(trace.run(first_build,individual_result(first_build,true)),
            "The failing-source build was not observed before changing only stdin");
        auto optional=individual_request(bad,CppRunRequest::Action::compile_run,302,3);
        optional.stdin_utf8="0\n";
        auto zero=individual_result(optional,false);
        zero.execution.stdout_bytes="piezas=0 esperado=0 obtenido=0 OK\nBateria: OK\n";
        check(!trace.run(optional,zero)&&trace.stage()==Stage::failed_built,
            "An optional input advanced or erased the captured failing-source build");
        auto stale_execute=individual_request(bad,CppRunRequest::Action::execute,303,4);
        check(!trace.run(stale_execute,individual_result(stale_execute,true)),
            "An Execute-only action borrowed a build captured before the input revision");
        const auto failed=individual_request(bad,combined?CppRunRequest::Action::compile_run:
            CppRunRequest::Action::build,304,4);
        auto foreign=failed;foreign.project_key="qa/another-project";
        check(!trace.run(foreign,individual_result(foreign,true)),
            "A fresh build switched projects while retaining another project's History");
        auto stale=failed;stale.revision=1;
        check(!trace.run(stale,individual_result(stale,true)),
            "A new build refreshed its evidence with an older editor revision");
        auto wrong_fingerprint=individual_result(failed,true);
        wrong_fingerprint.project_fingerprint=std::string(64,'f');
        check(!trace.run(failed,wrong_fingerprint),
            "A changed build fingerprint reused the previous failing-source snapshot");
        check(trace.run(failed,individual_result(failed,true))&&
            trace.stage()==(combined?Stage::failed_executed:Stage::failed_built),
            "A genuine fresh build could not return to the required input");
        if(!combined){
            check(!trace.complete()&&!trace.compare(compared(p.project_files,bad,std::string(root_path))),
                "A build-only refresh invented the required failed execution");
            const auto executed=individual_request(bad,CppRunRequest::Action::execute,305,4);
            check(trace.run(executed,individual_result(executed,true))&&
                trace.stage()==Stage::failed_executed,
                "An Execute matching the refreshed failing build was rejected");
        }
        check(trace.compare(compared(p.project_files,bad,std::string(root_path)))&&
            trace.compare(compared(p.project_files,bad,std::string(source_path)))&&
            trace.restore(recovery(p.project_files,bad)),
            "Changing stdin removed the requirement for both diffs and one-file restoration");
        const auto restored_build=individual_request(good,CppRunRequest::Action::build,306,5);
        check(trace.run(restored_build,individual_result(restored_build,false)),
            "The recovered source did not retain its independent build evidence");
        optional=individual_request(good,CppRunRequest::Action::compile_run,307,6);
        optional.stdin_utf8="0\n";
        zero=individual_result(optional,false);
        zero.execution.stdout_bytes="piezas=0 esperado=0 obtenido=0 OK\nBateria: OK\n";
        check(!trace.run(optional,zero)&&trace.stage()==Stage::restored_built,
            "An optional input completed or erased the recovered-source build");
        stale_execute=individual_request(good,CppRunRequest::Action::execute,308,7);
        check(!trace.run(stale_execute,individual_result(stale_execute,false)),
            "The recovered source accepted Execute without a build for its current revision");
        auto final=individual_request(good,combined?CppRunRequest::Action::compile_run:
            CppRunRequest::Action::build,309,7);
        foreign=final;foreign.projects_directory=L"C:\\LGAED-QA\\another-project";
        check(!trace.run(foreign,individual_result(foreign,false)),
            "A recovered rebuild reused History from a different project directory");
        stale=final;stale.revision=4;
        check(!trace.run(stale,individual_result(stale,false)),
            "An obsolete source revision completed the recovered build");
        wrong_fingerprint=individual_result(final,false);
        wrong_fingerprint.project_fingerprint=std::string(64,'f');
        check(!trace.run(final,wrong_fingerprint),
            "A different recovered-build fingerprint replaced the saved snapshot");
        check(trace.run(final,individual_result(final,false))&&
            trace.stage()==(combined?Stage::verified:Stage::restored_built),
            "A fresh rebuilt first case could not follow optional input exploration");
        if(!combined){
            check(!trace.complete()&&!trace.completed_by(final),
                "A build-only refresh completed History without a fresh successful execution");
            final.action=CppRunRequest::Action::execute;final.request_id=310;
            check(trace.run(final,individual_result(final,false)),
                "An Execute matching the refreshed restored build was rejected");
        }
        check(trace.complete()&&trace.completed_by(final),
            "The refreshed first-case execution did not seal its complete action sequence");
    }
}
void individual_receipts_preserve_the_aggregate_and_optional_history(){
    curriculum::Lesson lesson;lesson.locale="es";
    lesson.id="programming_practice_tools__comprobaciones_repetibles__reorganizar_la_descripcion_conservando_el_comprobador";
    auto legacy=published(),current=individual_published();
    for(auto* block:{&legacy,&current}){
        block->stable_id="block_0010_0003";block->address.page=10;block->address.block=3;
        block->compiler_workflow_check=curriculum::CompilerWorkflowCheck::cmake_selective_recovery_v1;
    }
    state::StateData stored;
    const auto capture=[](const CppRunRequest& run,const std::string& stdout_bytes){
        curriculum::FreshExecution execution;execution.source_revision=run.revision;
        execution.compilation_succeeded=true;execution.execution_succeeded=true;
        execution.execution_completed=true;execution.exit_code=0;
        execution.executable="comprobar_cajas";execution.standard_input=run.stdin_utf8;
        execution.standard_output=stdout_bytes;
        return execution;
    };
    const auto complete=[&](const curriculum::Block& block,std::uint64_t start){
        const bool single=!block.output_check->cases.front().standard_input.empty();
        const auto bad=broken(block.project_files),good=restored(bad,block.project_files);
        CmakeSelectiveRecoveryTrace trace(block);
        auto failed=request(bad,CppRunRequest::Action::compile_run,start,2);
        auto final=request(good,CppRunRequest::Action::compile_run,start+1,3);
        failed.stdin_utf8=final.stdin_utf8=block.output_check->cases.front().standard_input;
        check(trace.record(registered(block.project_files))&&
            trace.run(failed,single?individual_result(failed,true):result(failed,true))&&
            trace.compare(compared(block.project_files,bad,std::string(root_path)))&&
            trace.compare(compared(block.project_files,bad,std::string(source_path)))&&
            trace.restore(recovery(block.project_files,bad))&&
            trace.run(final,single?individual_result(final,false):result(final,false)),
            "The model receipt omitted a real required action in its own source epoch");
        const auto execution=capture(final,block.output_check->cases.front().outputs.front());
        check(record_compiler_puzzle_case(stored,lesson,block,final,execution,"Correcto")&&
            record_compiler_selective_history_completion(stored,lesson,block,final,execution,trace),
            "The complete model chain failed to seal its own authored History goal");
        return final;
    };
    (void)complete(legacy,100);
    const auto historical=stored.preferences["native_windows"];
    check(compiler_puzzle_completed(stored,lesson,legacy)&&
        !compiler_puzzle_completed(stored,lesson,current)&&
        compiler_puzzle_case_count(stored,lesson,current)==0,
        "The aggregate History seal certified the revised individual client");
    auto initial=individual_request(current.project_files,CppRunRequest::Action::compile_run,200,1);
    const auto execution=capture(initial,std::string(individual_success));
    auto stdout_only=stored;
    check(record_compiler_puzzle_case(stdout_only,lesson,current,initial,execution,"Primero correcto")&&
        !compiler_puzzle_completed(stdout_only,lesson,current)&&
        compiler_puzzle_case_count(stdout_only,lesson,current)==0,
        "Correct first-case stdout bypassed the revised client's independent History requirement");
    auto final=complete(current,201);
    check(compiler_puzzle_completed(stored,lesson,current)&&
        compiler_puzzle_case_count(stored,lesson,current)==1,
        "Optional cases became mandatory for the revised History objective");
    const auto earned=stored.preferences["native_windows"];
    auto optional=final;optional.request_id=203;optional.stdin_utf8="0\n";
    const auto extra=capture(optional,current.output_check->cases[1].outputs.front());
    check(record_compiler_puzzle_case(stored,lesson,current,optional,extra,"Opcional correcto")&&
        compiler_puzzle_completed(stored,lesson,current)&&
        compiler_puzzle_case_count(stored,lesson,current)==2,
        "An optional case discarded or replaced the completed first-only achievement");
    for(const auto& [key,value]:historical)
        check(stored.preferences["native_windows"].contains(key)&&
            stored.preferences["native_windows"].at(key)==value,
            "Revising the client or recording optional output rewrote historical aggregate evidence");
    for(const auto& [key,value]:earned)
        if(key.ends_with("history-sequence/v1"))
            check(stored.preferences["native_windows"].at(key)==value,
                "Optional output replaced an independent historical or current History seal");
    const auto reopened=state::decode_state(state::encode_state(stored));
    check(compiler_puzzle_completed(reopened,lesson,legacy)&&
        compiler_puzzle_completed(reopened,lesson,current)&&
        compiler_puzzle_case_count(reopened,lesson,current)==2,
        "State serialization lost a historical goal, first-case seal or optional receipt");
    validate_compiler_puzzle_state(reopened,lesson,current);
}

void counterexamples(){
    const auto p=published();
    const auto base=registered(p.project_files);
    const auto bad=broken(p.project_files),good=restored(bad,p.project_files);
    CmakeSelectiveRecoveryTrace trace(p);
    auto forged=base;forged.file_sha256["CMakeLists.txt"]=std::string(64,'0');
    check(!trace.record(forged),"Mismatched baseline was accepted");
    check(trace.record(base),"Valid baseline did not work after rejection");
    auto badbuild=request(bad,CppRunRequest::Action::build,1,1);
    auto failed_compile=result(badbuild,true);failed_compile.compilation.exit_code=1;
    check(!trace.run(badbuild,failed_compile),"Failed build was counted");
    auto altered=badbuild;
    altered.project_files.front().second+="message(STATUS \"invented\")\n";
    check(!trace.run(altered,result(altered,true)),"Extra CMake commands were accepted as comments");
    check(trace.run(badbuild,result(badbuild,true)),"Legitimate build was rejected");
    auto badrun=request(bad,CppRunRequest::Action::execute,2,1);
    auto changed_comment=badrun;
    for(auto& [path,source]:changed_comment.project_files)
        if(path==root_path)source+="# A comment added AFTER the first build\n";
    check(!trace.run(changed_comment,result(changed_comment,true)),
        "A changed CMake file reused the prior source revision");
    auto stderr_case=result(badrun,true);stderr_case.execution.stderr_bytes="Error\n";
    check(!trace.run(badrun,stderr_case),"Nonempty diagnostic passed the exact failed run");
    auto aborted=result(badrun,true);aborted.execution.status=CppPhaseStatus::timed_out;
    check(!trace.run(badrun,aborted),"A timeout passed as a normal failure");
    check(trace.run(badrun,result(badrun,true)),"Legitimate failed run was rejected");
    auto cmp=compared(p.project_files,bad,std::string(source_path));
    cmp.draft_sha256=state::sha256("modified unrelated code");
    check(!trace.compare(cmp),"Comparison with unrelated draft passed");
    check(trace.compare(compared(p.project_files,bad,std::string(source_path))),"Comparison rejected");
    check(trace.compare(compared(p.project_files,bad,std::string(root_path))),"Root comparison rejected");
    check(trace.restore(recovery(p.project_files,bad)),"Recovery rejected");
    auto fixedbuild=request(good,CppRunRequest::Action::build,3,2);
    auto oldrevision=fixedbuild;oldrevision.revision=1;
    check(!trace.run(oldrevision,result(oldrevision,false)),"Rebuild from prior source revision passed");
    check(trace.run(fixedbuild,result(fixedbuild,false)),"Final build rejected");
    auto final=request(good,CppRunRequest::Action::execute,4,2);
    auto replay=final;replay.request_id=2;
    check(!trace.run(replay,result(replay,false)),"Reused failed-run ID passed as a fresh test");
    check(trace.run(final,result(final,false)),"Final result rejected");
    check(trace.complete(),"The complete chain was not recognized");
}
}
int main(){
    try{
        full_sequence();combined_cmake_check_follows_the_real_lesson_button();
        comments_between_unchanged_commands_are_allowed();
        inline_cmake_comment_is_valid_but_new_commands_are_not();
        history_receipt_preserves_old_stdout_and_requires_new_actions();
        first_output_case_unlocks_without_discarding_optional_evidence();counterexamples();
        individual_goal_contract_is_closed();
        individual_sequence_needs_the_first_case_and_every_action();
        individual_rebuild_after_optional_input_keeps_fresh_action_evidence();
        individual_receipts_preserve_the_aggregate_and_optional_history();
        std::cout<<"PASS CMake selective-history action chain: baseline, failed build/run, both diffs, one-file restore, rebuilt fresh success and negative cases; no credit stored\n";
        return 0;
    }catch(const std::exception& exc){
        std::cerr<<"History sequence failure: "<<exc.what()<<'\n';
        return 1;
    }
}
