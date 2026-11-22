// Native Windows regression for the exact published U5L13 C++ rename/card
// composition. The five JSON fixtures are authored and validated by Python
// under a disposable QA directory; no learner or curricular files are changed.
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

void require(bool okay,std::string_view why) {
    if(!okay)throw std::runtime_error(std::string(why));
}
const Block& owned(const Lesson& lesson,std::string_view name) {
    const auto at=stable_block_position(lesson,name);
    require(at.has_value(),"The original U5L13 owner was lost");
    return lesson.pages.at(at->page).blocks.at(at->block);
}
bool same_pages(const std::vector<StatePage>& a,
                const std::vector<StatePage>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].id!=b[i].id||a[i].blocks!=b[i].blocks)return false;
    return true;
}
std::string matching_answer(const Block& block) {
    const auto json=JsonObject::Parse(winrt::to_hstring(
        math_evaluation_source(block)));
    return winrt::to_string(json.GetNamedArray(L"responses").GetObjectAt(0).
        GetNamedObject(L"validator").GetNamedObject(L"expected").Stringify());
}
void reject_both(const JsonObject& document) {
    const auto source=winrt::to_string(document.Stringify());
    bool full=false,compact=false;
    try{(void)parse_lesson_json(source,"es");}
    catch(const std::exception&){full=true;}
    try{(void)parse_lesson_progress_metadata(source,"es");}
    catch(const std::exception&){compact=true;}
    require(full&&compact,"Forged mixed editorial archive was accepted by Windows");
}
void same_state_contract(const Lesson& original,const Lesson& revised,
                         const Lesson& metadata) {
    require(revised.id==original.id&&metadata.id==original.id&&
        revised.state_layout.signatures==original.state_layout.signatures&&
        metadata.state_layout.signatures==original.state_layout.signatures&&
        same_pages(revised.state_layout.origin,original.state_layout.origin)&&
        same_pages(metadata.state_layout.origin,original.state_layout.origin)&&
        revised.state_layout.identifier_rename_revisions.size()==6&&
        metadata.state_layout.identifier_rename_revisions.size()==6&&
        revised.state_layout.math_matching_text_revisions.size()==1&&
        metadata.state_layout.math_matching_text_revisions.size()==1,
        "The combined revision changed history, signatures or journal owners");
    constexpr std::array<std::string_view,6> code_owners{
        "block_0006_0002","recuperacion_entrada_editor",
        "block_0006_0009","block_0007_0002","block_0007_0004",
        "frecuencias_numericas_referencia_programa"
    };
    for(const auto id:code_owners) {
        const auto& old=owned(original,id);
        const auto& now=owned(revised,id);
        const auto& smaller=owned(metadata,id);
        const auto before=old.type==BlockType::cpp_compiler?
            std::string_view(old.initial_code):std::string_view(old.code);
        const auto after=now.type==BlockType::cpp_compiler?
            std::string_view(now.initial_code):std::string_view(now.code);
        const auto compact=smaller.type==BlockType::cpp_compiler?
            std::string_view(smaller.initial_code):std::string_view(smaller.code);
        require(now.address.page==old.address.page&&
            now.address.block==old.address.block&&
            smaller.address.page==old.address.page&&
            smaller.address.block==old.address.block&&
            old.type==now.type&&now.type==smaller.type&&
            before!=after&&after==compact&&
            after.find("frecuencia_maxima_encontrada")!=after.npos,
            "An accepted source rename changed its owner or was dropped");
    }
    const auto& historical=owned(original,"block_0007_0007");
    const auto& current=owned(revised,"block_0007_0007");
    const auto& compact=owned(metadata,"block_0007_0007");
    require(matching_answer(historical)==matching_answer(current)&&
        matching_answer(historical)==matching_answer(compact)&&
        !current.math_evaluation_raw_json.empty()&&
        current.math_evaluation_raw_json==compact.math_evaluation_raw_json&&
        current.prompt.find("frecuencia_maxima_encontrada")!=current.prompt.npos&&
        current.address.page==historical.address.page&&
        current.address.block==historical.address.block,
        "Matching prose corrupted old grading, card labels or storage");
}
void reopen_order(const Lesson& original,const Lesson& halfway,
                  const Lesson& latest,const Lesson& compact,
                  const std::filesystem::path& directory,std::string_view label) {
    state::StateStore store(directory/std::string(label));
    auto saved=store.load();
    state::prepare_lesson_state(saved,original);
    const auto& historical=owned(original,"block_0007_0007");
    const auto key=state::response_storage_key(original,historical);
    state::Response correct;correct.correct=true;
    correct.raw_json=R"({"responses":{"relaciones":{"acceso":"proteger_acceso","mismo_grupo":"agrupar_observaciones","supera_maximo":"reemplazar_candidatos","iguala_maximo":"conservar_empate"}},"opaque":"old-matching-receipt"})";
    state::put_response(saved,original,historical,correct);
    const auto prior_receipt=state::string_value(saved,"learning_progress",key);
    const auto& previous_cpp=owned(original,"recuperacion_entrada_editor");
    const auto draft_id=draft_key("programming",original,previous_cpp);
    state::Draft draft;draft.source=previous_cpp.initial_code+"\n// prior learner draft";
    draft.input="6 patio sala patio faro sala patio";
    draft.selection_start=2;draft.selection_end=4;
    state::put_draft(saved,draft_id,draft);
    const auto old_hash=state::sha256(historical.prompt);
    narration::Position heard{4,170,0,false};
    heard.heard_prefix=narration::HeardPrefix{old_hash,500000,1000000};
    narration::History listening;
    listening.entries.push_back({"qa-historical-reading","block_0007_0007",
                                heard,"page_0007"});
    narration::write_history(saved,listening);
    const auto previous_listening=narration::read_history(saved);
    saved.preferences["composition-qa"]["foreign"]=std::string("untouched");
    store.save(saved);
    state::StateStore reader(store.root().parent_path());
    auto progress=reader.load();
    state::prepare_lesson_state(progress,halfway);
    state::validate_lesson_state(progress,halfway);
    state::prepare_lesson_state(progress,latest);
    state::validate_lesson_state(progress,latest);
    state::validate_lesson_state(progress,compact);
    const auto& current_math=owned(latest,"block_0007_0007");
    const auto& current_cpp=owned(latest,"recuperacion_entrada_editor");
    const auto answer=state::get_response(progress,latest,current_math);
    const auto compact_answer=state::get_response(progress,compact,
        owned(compact,"block_0007_0007"));
    const auto current_draft=state::get_draft(progress,draft_id,
                                              current_cpp.initial_code);
    const auto new_hash=state::sha256(current_math.prompt);
    require(answer&&compact_answer&&answer->correct&&compact_answer->correct&&
        answer->raw_json==prior_receipt&&
        compact_answer->raw_json==prior_receipt&&
        state::response_storage_key(latest,current_math)==key&&
        state::response_storage_key(compact,
            owned(compact,"block_0007_0007"))==key&&
        state::string_value(progress,"learning_progress",key)==prior_receipt&&
        draft_key("programming",latest,current_cpp)==draft_id&&
        current_draft.source==draft.source&&current_draft.input==draft.input&&
        narration::read_history(progress)==previous_listening&&
        narration::heard_prefix_matches(heard,old_hash,1000000)&&
        !narration::heard_prefix_matches(heard,new_hash,1000000)&&
        state::string_value(progress,"composition-qa","foreign")=="untouched",
        "A composed revision attributed false credit or erased real history");
    reader.save(progress);
    state::StateStore reopened(reader.root().parent_path());
    const auto durable=reopened.load();
    state::validate_lesson_state(durable,latest);
    require(durable==progress&&
        state::string_value(durable,"learning_progress",key)==prior_receipt&&
        narration::read_history(durable)==previous_listening,
        "The signed composite lost its durable student state when reopened");
}
void assetcheck_p2_prompt_revision(const std::filesystem::path& folder,
                                   const std::filesystem::path& qa) {
    // A matching prompt in another course is a general authored-text edit.
    // A completed learner response and its source/target pairing stay owned
    // by the same puzzle, even if only the narration wording changes.
    constexpr auto identity="block_0002_0005";
    const auto original=load_lesson(folder/"assetcheck-p2-original.json");
    const auto revised=load_lesson(folder/"assetcheck-p2-revised.json");
    const auto compact=load_lesson_progress_metadata(
        folder/"assetcheck-p2-revised.json");
    const auto& old=owned(original,identity);
    const auto& current=owned(revised,identity);
    const auto& metadata=owned(compact,identity);
    require(old.type==BlockType::math_puzzle&&
        current.type==BlockType::math_puzzle&&
        old.prompt.find("Las barras inclinadas")!=std::string::npos&&
        current.prompt.find("barra inversa")!=std::string::npos&&
        current.prompt==metadata.prompt&&old.prompt!=current.prompt&&
        old.address.page==current.address.page&&
        old.address.block==current.address.block&&
        current.address.page==metadata.address.page&&
        current.address.block==metadata.address.block&&
        revised.state_layout.signatures==original.state_layout.signatures&&
        compact.state_layout.signatures==original.state_layout.signatures&&
        same_pages(revised.state_layout.origin,original.state_layout.origin)&&
        same_pages(compact.state_layout.origin,original.state_layout.origin)&&
        revised.state_layout.math_matching_text_revisions.size()==1&&
        compact.state_layout.math_matching_text_revisions.size()==1,
        "The P2 matching prompt changed its original owner or grading lineage");
    const auto old_source=JsonObject::Parse(winrt::to_hstring(
        math_evaluation_source(old)));
    const auto revised_source=JsonObject::Parse(winrt::to_hstring(
        math_evaluation_source(current)));
    const auto compact_source=JsonObject::Parse(winrt::to_hstring(
        math_evaluation_source(metadata)));
    require(old_source.GetNamedArray(L"responses").Stringify()==
                revised_source.GetNamedArray(L"responses").Stringify()&&
            old_source.GetNamedArray(L"responses").Stringify()==
                compact_source.GetNamedArray(L"responses").Stringify()&&
            matching_answer(old)==matching_answer(current)&&
            matching_answer(old)==matching_answer(metadata),
        "P2 text changed its source/target identities or expected matching answer");

    state::StateStore store(qa/"assetcheck-p2");
    auto saved=store.load();
    state::prepare_lesson_state(saved,original);
    state::Response answer;
    answer.correct=true;
    answer.raw_json=R"({"responses":{"relaciones":{"a":"cero","b":"uno","c":"dos_vacia","d":"dos"}},"opaque":"original-P2-response"})";
    state::put_response(saved,original,old,answer);
    const auto key=state::response_storage_key(original,old);
    const auto receipt=state::string_value(saved,"learning_progress",key);
    saved.preferences["p2-prompt-qa"]["foreign"]=std::string("unchanged");
    const auto original_position=stable_block_position(original,identity);
    require(original_position.has_value(),
        "The P2 original prompt lost its stable narration owner");
    const auto old_hash=state::sha256(old.prompt);
    const auto revised_hash=state::sha256(current.prompt);
    require(old_hash!=revised_hash,"A changed P2 prompt retained its old narration hash");
    narration::Position heard{4,170,0,false};
    heard.heard_prefix=narration::HeardPrefix{old_hash,500000,1000000};
    narration::History listening;
    listening.entries.push_back({"qa-p2-original-prompt",identity,heard,
        original.pages.at(original_position->page).stable_id});
    narration::write_history(saved,listening);
    const auto previous_listening=narration::read_history(saved);
    store.save(saved);
    state::StateStore reader(store.root().parent_path());
    auto progress=reader.load();
    state::prepare_lesson_state(progress,revised);
    state::validate_lesson_state(progress,revised);
    state::validate_lesson_state(progress,compact);
    const auto active=state::get_response(progress,revised,current);
    const auto projected=state::get_response(progress,compact,metadata);
    require(active&&projected&&active->correct&&projected->correct&&
        active->raw_json==answer.raw_json&&
        projected->raw_json==answer.raw_json&&
        state::response_storage_key(revised,current)==key&&
        state::response_storage_key(compact,metadata)==key&&
        state::string_value(progress,"learning_progress",key)==receipt&&
        narration::read_history(progress)==previous_listening&&
        narration::heard_prefix_matches(heard,old_hash,1000000)&&
        !narration::heard_prefix_matches(heard,revised_hash,1000000)&&
        state::string_value(progress,"p2-prompt-qa","foreign")=="unchanged",
        "A P2 prompt revision regraded or removed an already saved matching answer");
    reader.save(progress);
    state::StateStore reopened(reader.root().parent_path());
    const auto durable=reopened.load();
    state::validate_lesson_state(durable,revised);
    require(durable==progress&&
        narration::read_history(durable)==previous_listening,
        "P2 revised prompt lost its saved response or historical listening bookmark");

    // The complete and compact WinRT loaders reject the same forged history
    // and answer mapping; a cosmetic edit must not authorize either change.
    auto forged=JsonObject::Parse(winrt::to_hstring(revised.raw_json));
    forged.GetNamedObject(L"stateLayout").
        GetNamedArray(L"mathMatchingTextRevisions").GetObjectAt(0).
        Insert(L"afterSha256",JsonValue::CreateStringValue(
            winrt::to_hstring(std::string(64,'0'))));
    reject_both(forged);
    forged=JsonObject::Parse(winrt::to_hstring(revised.raw_json));
    const auto at=stable_block_position(revised,identity);
    require(at.has_value(),"The P2 matching answer was not indexed");
    forged.GetNamedArray(L"pages").GetObjectAt(at->page).
        GetNamedArray(L"blocks").GetObjectAt(at->block).
        GetNamedArray(L"responses").GetObjectAt(0).
        GetNamedObject(L"validator").GetNamedObject(L"expected").
        Insert(L"a",JsonValue::CreateStringValue(L"dos"));
    reject_both(forged);
    std::cout<<"PASS P2 AssetCheck authenticated matching prompt: exact "
        "source/target mapping, prior response, old-only listening credit, "
        "durable StateStore, full/compact loaders and forged-answer rejection\n";
}

void assetcheck_p2_published_revision(const std::filesystem::path& folder,
                                     const std::filesystem::path& qa) {
    // Once Work publishes the corrected prompt, inspect the actual signed
    // edition. No artificial second revision or archived source is installed.
    constexpr auto identity="block_0002_0005";
    const auto lesson=load_lesson(folder/"assetcheck-p2-published.json");
    const auto compact=load_lesson_progress_metadata(
        folder/"assetcheck-p2-published.json");
    const auto& current=owned(lesson,identity);
    const auto& compact_block=owned(compact,identity);
    const curriculum::MathMatchingTextRevision* journal=nullptr;
    for(const auto& item:lesson.state_layout.math_matching_text_revisions)
        if(item.id==identity) {
            require(!journal,"Duplicate published P2 matching text journal");
            journal=&item;
        }
    require(journal&&current.type==BlockType::math_puzzle&&
        current.prompt.find("barra inversa")!=current.prompt.npos&&
        current.prompt==compact_block.prompt&&
        current.address.page==compact_block.address.page&&
        current.address.block==compact_block.address.block&&
        compact.state_layout.signatures==lesson.state_layout.signatures,
        "The published P2 matching text or compact loader lost its owner");
    const auto old=JsonObject::Parse(winrt::to_hstring(journal->raw_json));
    const auto before=winrt::to_string(old.GetNamedString(L"prompt"));
    require(before.find("Las barras inclinadas")!=before.npos&&
        before!=current.prompt,
        "The P2 journal does not preserve the actual former prompt");
    const auto original_answers=old.GetNamedArray(L"responses").Stringify();
    const auto actual=JsonObject::Parse(winrt::to_hstring(
        math_evaluation_source(current)));
    const auto compact_actual=JsonObject::Parse(winrt::to_hstring(
        math_evaluation_source(compact_block)));
    require(original_answers==actual.GetNamedArray(L"responses").Stringify()&&
        original_answers==compact_actual.GetNamedArray(L"responses").Stringify(),
        "The published P2 prompt changed the original matching evaluator");
    const auto old_hash=state::sha256(before);
    const auto current_hash=state::sha256(current.prompt);
    require(old_hash!=current_hash,"A new P2 prompt inherited an old source hash");

    state::StateStore store(qa/"assetcheck-p2-published");
    auto initial=store.load();
    state::prepare_lesson_state(initial,lesson);
    state::Response answer;answer.correct=true;
    answer.raw_json=R"({"responses":{"relaciones":{"a":"cero","b":"uno","c":"dos_vacia","d":"dos"}},"opaque":"published-P2-qa"})";
    state::put_response(initial,lesson,current,answer);
    const auto key=state::response_storage_key(lesson,current);
    narration::Position heard{4,170,0,false};
    heard.heard_prefix=narration::HeardPrefix{old_hash,500000,1000000};
    narration::History prior;
    prior.entries.push_back({"qa-p2-published-listening",identity,heard,
        lesson.pages.at(current.address.page).stable_id});
    narration::write_history(initial,prior);
    const auto prior_listening=narration::read_history(initial);
    initial.preferences["p2-published-qa"]["unrelated"]=std::string("intact");
    store.save(initial);
    state::StateStore reader(store.root().parent_path());
    auto current_state=reader.load();
    state::prepare_lesson_state(current_state,lesson);
    state::validate_lesson_state(current_state,lesson);
    state::validate_lesson_state(current_state,compact);
    const auto response=state::get_response(current_state,lesson,current);
    const auto compact_response=state::get_response(current_state,compact,
        compact_block);
    require(response&&compact_response&&response->correct&&
        compact_response->correct&&response->raw_json==answer.raw_json&&
        compact_response->raw_json==answer.raw_json&&
        state::response_storage_key(compact,compact_block)==key&&
        narration::read_history(current_state)==prior_listening&&
        narration::heard_prefix_matches(heard,old_hash,1000000)&&
        !narration::heard_prefix_matches(heard,current_hash,1000000)&&
        state::string_value(current_state,"p2-published-qa","unrelated")=="intact",
        "The published P2 edition lost matching credit or miscredited old audio");
    reader.save(current_state);
    state::StateStore reopened(reader.root().parent_path());
    const auto durable=reopened.load();
    state::validate_lesson_state(durable,lesson);
    require(durable==current_state&&
        narration::read_history(durable)==prior_listening,
        "The published P2 StateStore was not durable");

    auto forged=JsonObject::Parse(winrt::to_hstring(lesson.raw_json));
    forged.GetNamedObject(L"stateLayout").
        GetNamedArray(L"mathMatchingTextRevisions").GetObjectAt(0).
        Insert(L"afterSha256",JsonValue::CreateStringValue(
            winrt::to_hstring(std::string(64,'0'))));
    reject_both(forged);
    std::cout<<"PASS published P2 AssetCheck matching prompt: signed "
        "historical evaluator, preserved response and old-only listening, "
        "durable WinRT StateStore and forged journal rejected\n";
}

void reject_forgery(std::string_view raw) {
    auto forged=JsonObject::Parse(winrt::to_hstring(raw));
    auto record=forged.GetNamedObject(L"stateLayout").
        GetNamedArray(L"identifierRenameRevisions").GetObjectAt(0);
    record.Insert(L"beforeSha256",JsonValue::CreateStringValue(
        winrt::to_hstring(std::string(64,'0'))));
    reject_both(forged);
    forged=JsonObject::Parse(winrt::to_hstring(raw));
    auto matching=forged.GetNamedObject(L"stateLayout").
        GetNamedArray(L"mathMatchingTextRevisions").GetObjectAt(0);
    matching.Insert(L"afterSha256",JsonValue::CreateStringValue(
        winrt::to_hstring(std::string(64,'0'))));
    reject_both(forged);
}
} // namespace

int main(int argc,char** argv)try{
    require(argc==3,"Expected isolated U5L13 fixture and QA storage directories");
    const auto folder=std::filesystem::path(argv[1]);
    const auto qa=std::filesystem::path(argv[2]);
    const auto original=load_lesson(folder/"baseline.json");
    const auto named_first=load_lesson(folder/"renamed-first.json");
    const auto cards_first=load_lesson(folder/"matching-first.json");
    const auto names_then_cards=load_lesson(folder/"names-then-cards.json");
    const auto cards_then_names=load_lesson(folder/"cards-then-names.json");
    const auto compact_names=load_lesson_progress_metadata(folder/"names-then-cards.json");
    const auto compact_cards=load_lesson_progress_metadata(folder/"cards-then-names.json");
    same_state_contract(original,names_then_cards,compact_names);
    same_state_contract(original,cards_then_names,compact_cards);
    require(owned(names_then_cards,"block_0007_0007").prompt==
            owned(cards_then_names,"block_0007_0007").prompt,
        "Two signed publication orders displayed different matching content");
    reopen_order(original,named_first,names_then_cards,compact_names,
                 qa,"names-then-cards");
    reopen_order(original,cards_first,cards_then_names,compact_cards,
                 qa,"cards-then-names");
    reject_forgery(names_then_cards.raw_json);
    reject_forgery(cards_then_names.raw_json);
    if(std::filesystem::exists(folder/"assetcheck-p2-original.json"))
        assetcheck_p2_prompt_revision(folder,qa);
    else {
        require(std::filesystem::exists(folder/"assetcheck-p2-published.json"),
            "P2 prompt fixture missing: neither preview nor published edition");
        assetcheck_p2_published_revision(folder,qa);
    }
    std::cout<<"PASS U5L13 native Windows dual-order C++ and matching prose: "
        "six code owners, exact historical answer IDs, full/metadata loaders, "
        "reopened draft, old-only narration credit and forged-proof rejection\n";
    return 0;
}catch(const winrt::hresult_error& error){
    std::cerr<<winrt::to_string(error.message())<<'\n';return 1;
}catch(const std::exception& error){
    std::cerr<<error.what()<<'\n';return 1;
}
