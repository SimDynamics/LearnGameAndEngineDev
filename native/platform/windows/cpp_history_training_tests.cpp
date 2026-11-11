#include "platform/windows/cpp_history_training.hpp"
#include "platform/windows/compiler_completion.hpp"
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
void history_receipt_preserves_old_stdout_and_requires_new_actions(){
    auto block=published();
    block.stable_id="block_0010_0003";
    block.address.page=10;block.address.block=3;
    curriculum::OutputCase goal;goal.standard_input="";
    goal.outputs={output(false)};goal.executable="comprobar_cajas";
    block.output_check=curriculum::OutputCheck{};
    block.output_check->cases.push_back(goal);
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
    const auto p=published(),bad=broken(p.project_files),good=restored(bad,p.project_files);
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
        history_receipt_preserves_old_stdout_and_requires_new_actions();counterexamples();
        std::cout<<"PASS CMake selective-history action chain: baseline, failed build/run, both diffs, one-file restore, rebuilt fresh success and negative cases; no credit stored\n";
        return 0;
    }catch(const std::exception& exc){
        std::cerr<<"History sequence failure: "<<exc.what()<<'\n';
        return 1;
    }
}
