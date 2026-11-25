// Exact published U2L6 -> authenticated rename -> independently archived prose.
// Uses the production Windows full/progress loaders and durable StateStore.
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
constexpr std::string_view owner_id="block_0003_0004";
constexpr std::string_view other_id="block_0001_0002";
constexpr std::string_view publication="55e60dcd4c9da2ddf71aad6eddd1c5937e312337";
constexpr std::string_view published_sha="68799c4d572ae9914ee36ce437029035bfb7cb0d6ed6865418a106260c8c3340";

void check(bool valid,std::string_view message) {
    if(!valid)throw std::runtime_error(std::string(message));
}
std::string text(const JsonObject& value,const wchar_t* name) {
    return winrt::to_string(value.GetNamedString(name));
}
std::string read(const std::filesystem::path& path) {
    const auto bytes=state::read_bytes(path);return {bytes.begin(),bytes.end()};
}
std::filesystem::path inside(const std::filesystem::path& directory,std::string_view name) {
    const std::filesystem::path relative(name);
    check(!relative.empty()&&!relative.is_absolute(),"Rename/prose fixture needs a relative path");
    for(const auto& part:relative)check(part!=L"..","Rename/prose fixture leaves its isolated directory");
    return directory/relative;
}
const Block& owner(const Lesson& lesson,std::string_view identity=owner_id) {
    const auto at=stable_block_position(lesson,identity);
    check(at.has_value(),"The original rename/prose owner is absent");
    return lesson.pages.at(at->page).blocks.at(at->block);
}
narration::SemanticPlanProof reading_proof(const Block& block) {
    narration::SemanticPlanProof proof;
    for(const auto path:{"/question","/explanation","/reviewFeedback"}) {
        const auto found=block.text_semantics.find(path);
        check(found!=block.text_semantics.end(),"Renamed-owner prose lost an authored reading field");
        proof.regions.push_back({std::string(owner_id)+path,found->second.source,
            "auto","text",found->second});
    }
    return proof;
}

void check_state_bytes(const state::StateData& actual,const state::StateData& expected,
        std::string_view phase) {
    if(state::encode(actual)==state::encode(expected))return;
    for(const auto& [domain,entries]:expected.preferences)for(const auto& [key,value]:entries) {
        const auto found=state::find(actual,domain,key);
        check(found&&*found==value,std::string(phase)+": changed saved field "+domain+"/"+key);
    }
    for(const auto& [domain,entries]:actual.preferences)for(const auto& entry:entries)
        check(state::find(expected,domain,entry.first)!=nullptr,
            std::string(phase)+": unexpected saved field "+domain+"/"+entry.first);
    check(false,std::string(phase)+": encoded state changed");
}

void verify_state(const std::vector<Lesson>& full,const std::vector<Lesson>& metadata,
        const std::vector<std::string>& return_receipts,bool earned) {
    const auto& initial=full.front();const auto& original=owner(initial);
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"identifier-prose-"+std::to_wstring(earned?1:0)+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    check(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),
        "Rename/prose QA would open an existing learner profile");
    initialize_profile_directories(environment.paths);
    state::StateData saved;
    {
        state::StateStore empty(environment.paths.root);saved=empty.load();
        check(saved.preferences.empty(),"The isolated rename/prose QA state is not empty");
    }
    state::prepare_lesson_state(saved,initial);
    state::Response response;response.answer=earned?"3 3.25":"3.25 3";
    response.correct=evaluate_text_answer(original,response.answer).correct;
    check(response.correct==earned,"The original U2L6 output oracle changed");
    response.raw_json=R"({"incorrectAttempts":4,"opaque":{"selection":7,"history":"keep"}})";
    if(!earned)state::mark_response_reviewed(response,original);
    state::put_response(saved,initial,original,response);
    const auto response_key=state::response_storage_key(initial,original);
    const auto receipt=state::string_value(saved,"learning_progress",response_key);
    const auto& compiler=owner(initial,"p1u2l6_reparar_conversion");
    const auto draft_address=draft_key("programming",initial,compiler);
    state::Draft draft;draft.source=compiler.initial_code+"\n// Unchanged learner draft\n";
    draft.input="entrada guardada";draft.selection_start=2;draft.selection_end=7;draft.has_saved=true;
    state::put_draft(saved,draft_address,draft);
    saved.preferences["qa_foreign_course"]["earned"]=std::int64_t{41};
    saved.preferences["native_windows"]["qa_reader_preference"]=std::string("keep");
    narration::Position position;position.segment=0;position.milliseconds=250;position.heard_through=0;
    auto history=narration::begin({},"u2l6-before-rename","qa-identifier-prose",position,
        initial.id+"/es/page_0003",initial.id+"/es/page_0003");
    history.entries.front().semantic_plan=reading_proof(original);
    narration::write_history(saved,history);
    const auto old_history=narration::read_history(saved);
    const auto encoded=state::encode(saved);
    const auto return_key="lesson_"+initial.id+"_state_editorial_returns_v1_"+initial.locale;
    check(return_receipts.size()==full.size()&&return_receipts.front().empty()&&
            !state::find(saved,"learning_progress",return_key),
        "The original U2L6 seed unexpectedly contains a later editorial return receipt");
    auto durable_expected=saved;
    {
        state::StateStore store(environment.paths.root);(void)store.load();store.save(saved);
    }
    for(std::size_t stage=1;stage<full.size();++stage) {
        const auto stage_context="U2L6 rename/prose correct="+std::to_string(earned)+
            " stage="+std::to_string(stage);
        state::StateStore store(environment.paths.root);auto current=store.load();
        check_state_bytes(current,durable_expected,stage_context+" reopen before adoption");
        // Only the independently authored return receipt may extend the seed.
        // Every other domain, key, type and byte remains part of the equality.
        auto expected=saved;
        if(!return_receipts[stage].empty())
            expected.preferences["learning_progress"][return_key]=return_receipts[stage];
        for(const auto* lesson:{&full[stage],&metadata[stage]}) {
            const auto phase=stage_context+(lesson==&full[stage]?" full":" progress");
            const auto& after=owner(*lesson);
            state::prepare_lesson_state(current,*lesson);state::validate_lesson_state(current,*lesson);
            const auto kept=state::get_response(current,*lesson,after);
            check(kept.has_value()&&kept->answer==response.answer&&kept->correct==earned,
                "Rename/prose changed the original answer or correctness");
            check(state::response_storage_key(*lesson,after)==response_key&&
                    state::string_value(current,"learning_progress",response_key)==receipt,
                "Rename/prose changed original response bytes, attempts or address");
            if(!earned)check(state::response_reviewed(*kept,after),"Rename/prose lost an incorrect reviewed attempt");
            const auto kept_draft=state::get_draft(current,draft_address,compiler.initial_code);
            check(kept_draft.source==draft.source&&kept_draft.input==draft.input&&
                    kept_draft.selection_start==draft.selection_start&&kept_draft.selection_end==draft.selection_end,
                "Rename/prose rewrote a learner draft or selection");
            check(narration::read_history(current)==old_history,
                phase+": historical reading evidence changed");
            check_state_bytes(current,expected,phase+" after preparation");
            if(lesson==&full[stage]) {
                const auto evaluated=evaluate_text_answer(after,response.answer);
                check(evaluated.correct==earned,"Current prose changed the output oracle");
                check(evaluated.feedback==(earned?after.explanation:incorrect_feedback(after)),
                    "The current output response uses stale editorial feedback");
                if(stage>=2&&stage<=4)
                    check(reading_proof(after)!=reading_proof(original),
                        "Changed prose acquired the old semantic reading proof");
            }else check(after.raw_json==owner(full[stage]).raw_json,
                "The progress loader lost the exact authored current block");
        }
        store.save(current);
        state::StateStore reopened(environment.paths.root);const auto persisted=reopened.load();
        check_state_bytes(persisted,expected,stage_context+" reopen after save");
        check(narration::read_history(persisted)==old_history,
            stage_context+": durable reopen changed historical reading evidence");
        durable_expected=expected;
        // Direct import crosses each skipped publication from original bytes.
        auto imported=state::decode(encoded);
        state::prepare_lesson_state(imported,full[stage]);
        state::validate_lesson_state(imported,metadata[stage]);
        check_state_bytes(imported,expected,stage_context+" direct import");
        check(narration::read_history(imported)==old_history,
            stage_context+": direct import changed historical reading evidence");
        std::cout<<"PASS "<<stage_context
            <<": full/progress state, exact return receipt, durable reopen and direct import\n";
    }
    std::cout<<"PASS U2L6 rename/prose state correct="<<earned
        <<": actual output evaluation, durable reopen, direct import, attempts, draft and reading provenance\n";
}
} // namespace

int main(int argc,char** argv) try {
    winrt::init_apartment();
    check(argc==2,"Expected one authenticated rename/prose manifest");
    const std::filesystem::path manifest_path(argv[1]);const auto directory=manifest_path.parent_path();
    const auto manifest_bytes=read(manifest_path);
    const auto manifest=JsonObject::Parse(winrt::to_hstring(manifest_bytes));
    check(text(manifest,L"publishedCommit")==publication&&text(manifest,L"publishedSha256")==published_sha&&
            text(manifest,L"owner")==owner_id&&text(manifest,L"otherOwner")==other_id,
        "Rename/prose manifest changed the authenticated publication or owners");
    constexpr std::array<std::string_view,6> names{"published.json","renamed.json","question.json",
        "interleaved.json","explanation.json","returned.json"};
    const auto stages=manifest.GetNamedArray(L"stages");
    check(stages.Size()==names.size(),"Rename/prose must retain all six publication stages");
    const auto hashes=manifest.GetNamedObject(L"sha256");
    const auto receipts=manifest.GetNamedObject(L"editorialReturnReceipts");
    check(receipts.Size()==names.size(),"Rename/prose needs one exact return receipt per stage");
    std::vector<std::string> return_receipts;
    std::vector<Lesson> full,metadata;
    std::vector<std::string> source;
    for(unsigned i=0;i<stages.Size();++i) {
        const auto name=winrt::to_string(stages.GetStringAt(i));check(name==names[i],"Wrong rename/prose stage order");
        auto bytes=read(inside(directory,name));
        check(state::sha256(bytes)==winrt::to_string(hashes.GetNamedString(winrt::to_hstring(name))),
            "A complete rename/prose fixture differs from its SHA-256");
        if(i==0)check(state::sha256(bytes)==published_sha,"Published U2L6 input is not authentic");
        full.push_back(parse_lesson_json(bytes,"es"));metadata.push_back(parse_lesson_progress_metadata(bytes,"es"));
        source.push_back(std::move(bytes));
        const auto receipt=receipts.GetNamedValue(winrt::to_hstring(name));
        if(i+1<names.size()) {
            check(receipt.ValueType()==JsonValueType::Null,
                "A pre-return stage unexpectedly authorizes a saved editorial receipt");
            return_receipts.emplace_back();
        }else {
            const auto expected_hashes=receipt.GetArray();
            const auto authored=JsonObject::Parse(winrt::to_hstring(source.back()));
            check(expected_hashes.Size()==authored.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Size(),
                "The return receipt must cover the complete authored editorial history");
            for(const auto& digest:expected_hashes) {
                const auto value=winrt::to_string(digest.GetString());
                check(value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){
                    return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),
                    "The independent editorial receipt contains an invalid proof hash");
            }
            return_receipts.push_back(winrt::to_string(expected_hashes.Stringify()));
        }
        const auto& before=full.front();
        for(const auto* model:{&full.back(),&metadata.back()}) {
            check(model->id==before.id&&model->state_layout.signatures==before.state_layout.signatures&&
                    model->state_layout.lineage==before.state_layout.lineage&&
                    model->state_layout.legacy_lineage==before.state_layout.legacy_lineage,
                "Rename/prose changed original identity, lineage or response signatures");
            for(const auto id:{owner_id,other_id}) {
                const auto& old=owner(before,id);const auto& current=owner(*model,id);
                check(old.address.page==current.address.page&&old.address.block==current.address.block,
                    "Rename/prose moved an original storage owner");
            }
        }
        if(i>=1) {
            const auto root=JsonObject::Parse(winrt::to_hstring(source.back()));
            const auto first=JsonObject::Parse(winrt::to_hstring(source[1]));
            check(root.GetNamedObject(L"stateLayout").GetNamedArray(L"identifierRenameRevisions").Stringify()==
                    first.GetNamedObject(L"stateLayout").GetNamedArray(L"identifierRenameRevisions").Stringify(),
                "Later prose rewrote the original rename packet");
            check(owner(full.back()).code==owner(full[1]).code,
                "Later prose changed renamed source bytes");
        }
        std::cout<<"PASS both Windows loaders: "<<name<<'\n';
    }
    check(owner(full[4]).question.find("cociente_convertido")!=std::string::npos&&
            owner(full[4]).explanation.find("cociente_con_operando_convertido")!=std::string::npos,
        "The current U2L6 prose did not use the newly authored names");
    check(owner(full[5]).raw_json==owner(full[1]).raw_json,
        "The explicit editorial return did not recover the complete renamed endpoint");
    verify_state(full,metadata,return_receipts,true);verify_state(full,metadata,return_receipts,false);
    const auto negatives=manifest.GetNamedArray(L"negative");
    check(negatives.Size()==16,"Rename/prose lost a hostile history fixture");
    for(const auto& item:negatives) {
        const auto name=winrt::to_string(item.GetString());const auto bytes=read(inside(directory,name));
        check(state::sha256(bytes)==winrt::to_string(hashes.GetNamedString(winrt::to_hstring(name))),
            "A hostile rename/prose fixture differs from its complete SHA-256");
        for(const bool progress:{false,true}) {
            bool rejected=false;
            try {if(progress)(void)parse_lesson_progress_metadata(bytes,"es");else(void)parse_lesson_json(bytes,"es");}
            catch(const std::exception&){rejected=true;}
            catch(const winrt::hresult_error&){rejected=true;}
            check(rejected,"A hostile rename/prose history passed a Windows loader: "+name);
        }
        std::cout<<"PASS rejected by both Windows loaders: "<<name<<'\n';
    }
    check(read(manifest_path)==manifest_bytes,"Windows rename/prose QA changed its manifest");
    for(std::size_t i=0;i<names.size();++i)
        check(read(inside(directory,names[i]))==source[i],"Windows QA rewrote an original fixture");
    std::cout<<"PASS authenticated U2L6 rename/prose: six stages, both loaders, correct/incorrect state, "
        <<"durable reopen, direct import and sixteen hostile histories. No audio playback or human listening claimed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL identifier rename/prose: "<<error.what()<<'\n';return 1;}
catch(const winrt::hresult_error& error){std::cerr<<"FAIL identifier rename/prose Windows: "
    <<winrt::to_string(error.message())<<'\n';return 1;}
