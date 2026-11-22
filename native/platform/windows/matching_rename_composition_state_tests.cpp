// Native Windows regression for the exact published U5L13 C++ rename/card
// composition. The five JSON fixtures are authored and validated by Python
// under a disposable QA directory; no learner or curricular files are changed.
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <winrt/Windows.Data.Json.h>
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
    std::cout<<"PASS U5L13 native Windows dual-order C++ and matching prose: "
        "six code owners, exact historical answer IDs, full/metadata loaders, "
        "reopened draft, old-only narration credit and forged-proof rejection\n";
    return 0;
}catch(const winrt::hresult_error& error){
    std::cerr<<winrt::to_string(error.message())<<'\n';return 1;
}catch(const std::exception& error){
    std::cerr<<error.what()<<'\n';return 1;
}
