// The genuine Win32 compiler receipt logic and StateStore, with in-memory
// exported historical/reviewed Work JSON. Modelled receipts do not claim
// execution by a learner; real reference compilation is tested separately.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
void require(bool b,std::string_view why) {
    if(!b)throw std::runtime_error(std::string(why));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);
    require(static_cast<bool>(f),"Missing generated U6L19 QA JSON");
    return std::string(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
}
const Block& get(const Lesson& lesson,std::string_view id) {
    const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"Missing real U6L19 stable block identity");
    return lesson.pages.at(at->page).blocks.at(at->block);
}
CppRunRequest request(const Lesson& lesson,const Block& b,std::size_t case_index) {
    CppRunRequest q;
    q.request_id=42+case_index;
    q.revision=100;
    q.source_utf8=b.initial_code;
    q.project_files=b.project_files;
    q.source_files=b.source_files;
    q.cmake=b.cmake;
    q.stdin_utf8=b.output_check->cases.at(case_index).standard_input;
    bool replaced=false;
    for(auto& file:q.project_files)if(file.first=="sobres.cpp") {
        file.second=get(lesson,"capacidad_veinte_referencia_codigo").code;
        replaced=true;
    }
    require(replaced,"Real U6L19 project lost sobres.cpp");
    return q;
}
FreshExecution evidence(const CppRunRequest& q,const OutputCase& c) {
    FreshExecution x;
    x.source_revision=q.revision;
    x.compilation_succeeded=x.execution_succeeded=x.execution_completed=true;
    x.exit_code=0;x.standard_input=q.stdin_utf8;
    x.standard_output=c.outputs.front();x.executable=c.executable;
    return x;
}
void verify(const std::filesystem::path& oldfile,
            const std::filesystem::path& newfile,
            const std::filesystem::path& isolated) {
    auto old= parse_lesson_json(read(oldfile),"es");
    auto now= parse_lesson_json(read(newfile),"es");
    const auto& a=get(old,"capacidad_veinte_bateria");
    const auto& b=get(now,"capacidad_veinte_bateria");
    require(old.id==now.id && a.output_check && b.output_check &&
        a.output_check->cases.size()==1 && b.output_check->cases.size()==8 &&
        a.address.page==b.address.page && a.address.block==b.address.block &&
        draft_key("programming",old,a)==draft_key("programming",now,b) &&
        !b.compiler_case_revisions.empty(),
        "Authenticated U6L19 client or its stable state identity changed");
    state::StateStore store(isolated);
    auto saved=store.load();
    require(saved.preferences.empty(),"StateStore QA must have an empty dedicated profile");
    state::prepare_lesson_state(saved,old);
    state::Draft draft;
    draft.source="// QA learner-owned draft\n"+a.initial_code;
    draft.input="untouched historical stdin";
    draft.selection_start=3;draft.selection_end=10;
    state::put_draft(saved,draft_key("programming",old,a),draft);
    saved.preferences["u6l19_case_qa"]["unrelated"]=std::string("keep \t\r\n");
    const auto oldreq=request(old,a,0);
    require(record_compiler_puzzle_case(saved,old,a,oldreq,
        evidence(oldreq,a.output_check->cases.front()),"Correcto\nQA old batch") &&
        compiler_puzzle_completed(saved,old,a),
        "Model old grouped case could not form an authenticated receipt");
    const auto baseline=saved;
    store.save(saved);
    saved=store.load();
    require(saved==baseline,"StateStore changed the old aggregated receipt");
    state::prepare_lesson_state(saved,now);
    state::validate_lesson_state(saved,now);
    require(compiler_puzzle_historical_completed(saved,now,b) &&
       !compiler_puzzle_completed(saved,now,b) &&
        compiler_puzzle_case_count(saved,now,b)==0,
        "Old grouped stdout invented evidence for newly separated first case");
    require(saved.preferences.at("u6l19_case_qa")==baseline.preferences.at("u6l19_case_qa") &&
       state::get_draft(saved,draft_key("programming",now,b),"").source==draft.source,
       "U6L19 migration overwrote draft or unrelated settings");
    for(const auto& [key,value]:baseline.preferences.at("native_windows"))
        require(saved.preferences.at("native_windows").at(key)==value,
            "U6L19 case migration rewrote the old program/cases/seal");
    const auto once=saved;
    state::prepare_lesson_state(saved,now);
    require(saved==once,"U6L19 migration repeated a one-time receipt adoption");

    state::StateData optional;
    state::prepare_lesson_state(optional,now);
    const auto later=request(now,b,1);
    const auto later_result=evidence(later,b.output_check->cases[1]);
    require(!record_compiler_puzzle_case(optional,now,b,later,later_result,
        "QA optional") &&
        !compiler_puzzle_completed(optional,now,b) &&
        compiler_puzzle_case_count(optional,now,b)==1,
        "Second optional case bypassed the first");
    const auto first=request(now,b,0);
    require(record_compiler_puzzle_case(saved,now,b,first,
        evidence(first,b.output_check->cases[0]),"Correcto\nQA first input") &&
        compiler_puzzle_completed(saved,now,b) &&
        compiler_puzzle_case_count(saved,now,b)==1,
        "One verified first case did not unlock the new objective");
    const auto original_feedback=compiler_puzzle_feedback(saved,now,b);
    require(record_compiler_puzzle_case(saved,now,b,later,later_result,
        "Correcto\nQA second input") &&
        compiler_puzzle_case_count(saved,now,b)==2 &&
        compiler_puzzle_completed(saved,now,b) &&
        compiler_puzzle_feedback(saved,now,b)==original_feedback,
        "Optional case changed the first-case achievement");
    for(const auto& [key,value]:baseline.preferences.at("native_windows"))
        require(saved.preferences.at("native_windows").at(key)==value,
            "Optional case changed original batch execution receipts");
    store.save(saved);
    const auto reopened=store.load();
    require(reopened==saved && compiler_puzzle_completed(reopened,now,b) &&
       compiler_puzzle_historical_completed(reopened,now,b) &&
       compiler_puzzle_case_count(reopened,now,b)==2,
       "Reopening changed current/historical receipt ownership");
    state::validate_lesson_state(reopened,now);
    std::cout<<"PASS real Windows StateStore U6L19: historic aggregate intact, "
       "no borrowed first-case credit, optional second cannot unlock, "
       "original draft/preferences and both epochs survive reopen; model evidence only\n";
}
}
int main(int argc,char** argv) try {
    winrt::init_apartment();
    require(argc==4,"Usage: before.json after.json isolated-profile");
    verify(argv[1],argv[2],argv[3]);
    return 0;
}catch(const winrt::hresult_error& err) {
    std::cerr<<"FAIL U6L19 WinRT: "<<winrt::to_string(err.message())<<'\n';
    return 1;
}catch(const std::exception& err) {
    std::cerr<<"FAIL U6L19 StateStore: "<<err.what()<<'\n';
    return 1;
}
