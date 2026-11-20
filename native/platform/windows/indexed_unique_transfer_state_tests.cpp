// Authentic Windows state migration for an opt-in independent owner witness.
// All editorial changes remain in memory. This is a receipt/loader test, not
// a substitute for executing a compiled student's ownership implementation.
#include "core/cpp_indexed_unique_transfer_probe.hpp"
#include "core/lesson.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <winrt/Windows.Data.Json.h>

namespace lgaed::test {
namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
void require(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(std::string(message));
}
const Block& owner(const Lesson& lesson,std::string_view identity) {
    const auto at=stable_block_position(lesson,identity);
    require(at.has_value(),"U6L5 stable owner missing");
    return lesson.pages[at->page].blocks[at->block];
}
state::StateData reopen(const state::StateStore& store) {
    state::StateStore actual(store.root().parent_path());
    return actual.load();
}
} // namespace

int indexed_unique_transfer_state_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    constexpr std::string_view identity="block_0021_0002";
    const auto path=content.parent_path()/
        "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
        "lesson_transferir_propiedad_entre_unique_ptr.json";
    const auto old=load_lesson(path);
    const auto& published=owner(old,identity);
    require(published.type==BlockType::cpp_compiler&&published.output_check&&
            published.output_check->cases.size()==3&&
            !published.indexed_unique_transfer_probe,
        "The published U6L5 source has an unexpected existing identity objective");
    const auto original_json=old.raw_json;
    const auto root=JsonObject::Parse(winrt::to_hstring(original_json));
    const auto declaration=JsonObject::Parse(winrt::hstring{LR"u6l5({
        "activity":"block_0021_0002",
        "signature":"bool Deposito::transferir(int origen, int destino)",
        "className":"Deposito",
        "firstMember":"primero_",
        "secondMember":"segundo_",
        "transferMethod":"transferir",
        "putMethod":"poner",
        "changeMethod":"cambiar",
        "clearMethod":"vaciar"
    })u6l5"});
    JsonArray declarations;
    declarations.Append(declaration);
    root.Insert(L"compilerIndexedUniqueTransferChecks",declarations);
    const auto reviewed_json=winrt::to_string(root.Stringify());
    const auto revised=parse_lesson_json(reviewed_json,"es");
    const auto metadata=parse_lesson_progress_metadata(reviewed_json,"es");
    const auto& actual=owner(revised,identity);
    const auto& compact=owner(metadata,identity);
    require(actual.indexed_unique_transfer_probe&&
            compact.indexed_unique_transfer_probe&&
            actual.indexed_unique_transfer_probe==compact.indexed_unique_transfer_probe&&
            actual.initial_code==published.initial_code&&
            actual.raw_json==published.raw_json&&
            actual.output_check&&
            actual.output_check->cases.size()==published.output_check->cases.size()&&
            actual.stable_id==published.stable_id&&
            actual.address.page==published.address.page&&
            actual.address.block==published.address.block&&
            actual.address.addition_namespace==published.address.addition_namespace&&
            actual.address.progress_page==published.address.progress_page&&
            revised.state_layout.signatures==old.state_layout.signatures&&
            metadata.state_layout.signatures==old.state_layout.signatures,
        "Native full/progress loaders changed the original owner or output contract");

    // A historical stdout-only completion is legitimate history, not proof of
    // the SAME unique_ptr<int> surviving a move. Model only the adapter/storage
    // boundary here. The separate GCC/Clang/MSVC workflow compiles the probe.
    const CppMemberBodyCheck guard{
        published.initial_code,"bool Deposito::transferir(int origen, int destino)"};
    const auto interval=cpp_member_body_span(guard);
    require(interval.has_value(),"Cannot locate the published editable U6L5 method");
    const auto source=published.initial_code.substr(0,interval->begin)+R"cpp(
    if (!indice_valido(origen) || !indice_valido(destino) ||
        origen == destino || !elegir(origen)) return false;
    elegir(destino) = std::move(elegir(origen));
    return true;
)cpp"+published.initial_code.substr(interval->end);
    require(cpp_member_body_preserves_source(guard,source)&&
            cpp_indexed_unique_transfer_probe_source(
                *actual.indexed_unique_transfer_probe,source).has_value(),
        "The corrected U6L5 body cannot be independently checked");

    CppRunRequest request;
    request.request_id=321;
    request.revision=42;
    request.source_utf8=source;
    request.stdin_utf8=published.output_check->cases[0].standard_input;
    FreshExecution result;
    result.source_revision=request.revision;
    result.compilation_succeeded=true;
    result.execution_succeeded=true;
    result.execution_completed=true;
    result.exit_code=0;
    result.standard_input=request.stdin_utf8;
    result.standard_output=published.output_check->cases[0].outputs[0];
    result.executable=published.output_check->cases[0].executable;
    state::StateData data;
    state::prepare_lesson_state(data,old);
    state::Draft draft;
    draft.source=source+"\n// Historical saved learner draft\n";
    draft.input=request.stdin_utf8;
    state::put_draft(data,draft_key("programming",old,published),draft);
    data.preferences["qa-unrelated"]["opaque"]=std::string("leave exact");
    require(record_compiler_puzzle_case(data,old,published,request,result,
            "Correcto: historical stdout-only proof"),
        "Could not seed the original output-only receipt");
    require(compiler_puzzle_completed(data,old,published),
        "The original U6L5 output receipt was not earned");
    const auto historical=data.preferences.at("native_windows");
    const auto historical_data=data;
    state::StateStore store(qa/"indexed-unique-transfer-state");
    require(store.load().preferences.empty(),"Private owner QA profile is not empty");
    store.save(data);
    data=reopen(store);
    require(data==historical_data,"Historical receipt changed on durable reopen");
    state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);
    require(!compiler_puzzle_completed(data,revised,actual),
        "An old stdout receipt was promoted into owner-identity success");
    const auto before_verification=data;
    request.indexed_unique_transfer_probe=actual.indexed_unique_transfer_probe;
    require(compiler_runtime_request_allowed(actual,request),
        "Current source failed the independent ownership prerequisite");
    require(!record_compiler_puzzle_case(data,revised,actual,request,result,"QA missing witness")&&
            data==before_verification,
        "A fresh correct stdout without private identity evidence changed progress");
    result.indexed_unique_transfer_probe_correct=true;
    result.indexed_unique_transfer_request_fingerprint="stale-or-forged";
    require(!record_compiler_puzzle_case(data,revised,actual,request,result,"QA forged witness")&&
            data==before_verification,
        "An unrelated private request fingerprint earned this owner's identity");
    result.indexed_unique_transfer_request_fingerprint=
        cpp_indexed_unique_transfer_request_fingerprint(request);
    require(!result.indexed_unique_transfer_request_fingerprint.empty()&&
            record_compiler_puzzle_case(data,revised,actual,request,result,
                "Correcto: current owner identity"),
        "A current, bound private witness did not accredit the original owner");
    require(compiler_puzzle_completed(data,revised,actual),
        "A newly witnessed identity cannot unlock the original editor");
    // The additional namespace is append-only: every old native preference
    // must retain its original type, value, feedback and completion seal.
    const auto& updated=data.preferences.at("native_windows");
    for(const auto& [key,value]:historical) {
        const auto found=updated.find(key);
        require(found!=updated.end()&&found->second==value,
            "Identity adoption rewrote an earlier output-only completion");
    }
    // A compiler receipt lives under its reference-objective SHA, not
    // directly under the exercise key. Use exactly the same fully qualified
    // identity as production base(lesson,block), including this SHA segment.
    const auto ownership_key=
        "compiler-puzzle/v1/"+revised.locale+"/"+revised.id+"/"+
        exercise_key(revised,actual)+"/"+
        state::sha256(reference_objective_frame(revised,actual))+
        "/indexed-unique-transfer/v1";
    const auto independent=updated.find(ownership_key);
    require(independent!=updated.end()&&
            std::get_if<std::string>(&independent->second)!=nullptr,
        "Independent owner completion did not create its typed state seal");
    require(updated.size()==historical.size()+1,
        "Identity adoption added or removed unrelated native receipt fields");
    require(state::get_draft(data,draft_key("programming",revised,actual),
                published.initial_code).source==draft.source&&
            state::string_value(data,"qa-unrelated","opaque")=="leave exact",
        "Identity adoption changed the learner draft or foreign preferences");
    // A tampered supplemental seal must never silently validate on import.
    // The accepted profile and the historical stdout proof remain unchanged.
    auto forged_state=data;
    forged_state.preferences["native_windows"][ownership_key]=std::string(64,'0');
    const auto forged_snapshot=forged_state;
    require(!compiler_puzzle_completed(forged_state,revised,actual),
        "A forged independent owner receipt was treated as completed");
    bool rejected_forged_import=false;
    try {state::validate_lesson_state(forged_state,revised);}
    catch(const std::runtime_error&) {rejected_forged_import=true;}
    require(rejected_forged_import&&forged_state==forged_snapshot,
        "A corrupted independent owner seal was accepted or import mutated state");
    auto missing_history=data;
    missing_history.preferences["native_windows"].erase(
        ownership_key);
    require(!compiler_puzzle_completed(missing_history,revised,actual),
        "A historical stdout-only answer was promoted without a private seal");
    store.save(data);
    const auto persisted=reopen(store);
    require(persisted==data&&compiler_puzzle_completed(persisted,revised,actual),
        "Bound owner identity did not survive native StateStore reopen");
    state::validate_lesson_state(persisted,revised);
    require(load_lesson(path).raw_json==original_json,
        "U6L5 identity-state regression rewrote the published source");
    std::cout<<"PASS U6L5 live owner state: full/progress loaders, original "
        "stdout-only receipt unpromoted, forged proof rejected, fresh private "
        "request seal appended, draft and historical bytes preserved on reopen.\n";
    return 0;
}
} // namespace lgaed::test
