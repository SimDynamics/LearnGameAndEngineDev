#include "platform/windows/cpp_history_training.hpp"
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
    if(r.action==CppRunRequest::Action::execute){
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
    auto p=published(),bad=broken(p.project_files);
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
        full_sequence();comments_between_unchanged_commands_are_allowed();counterexamples();
        std::cout<<"PASS CMake selective-history action chain: baseline, failed build/run, both diffs, one-file restore, rebuilt fresh success and negative cases; no credit stored\n";
        return 0;
    }catch(const std::exception& exc){
        std::cerr<<"History sequence failure: "<<exc.what()<<'\n';
        return 1;
    }
}
