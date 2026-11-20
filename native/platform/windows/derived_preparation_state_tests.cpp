// Windows StateStore QA for the original Programación 3 U8L7 editor.
// The semantic derived-owner probe runs separately: this verifies the
// lesson loader, first-case receipt and old state without mutating JSON.
#include "core/cpp_derived_preparation_check.hpp"
#include "core/lesson.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::test {
namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
void require(bool ok,std::string_view message) {
    if(!ok)throw std::runtime_error(std::string(message));
}
const Block& editor(const Lesson& lesson,std::string_view id) {
    const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"Original U8L7 compiler owner absent");
    return lesson.pages[at->page].blocks[at->block];
}
state::StateData reopen(const state::StateStore& store) {
    state::StateStore independent(store.root().parent_path());
    return independent.load();
}
} // namespace

int derived_preparation_state_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    constexpr std::string_view id="block_0016_0002";
    const auto path=content.parent_path()/
        "cpp_language_memory_oop/composicion_y_herencia_publica/"
        "lesson_conservar_recursos_si_falla_la_construccion_derivada.json";
    const auto original=load_lesson(path);
    const auto& before=editor(original,id);
    require(before.type==BlockType::cpp_compiler&&before.output_check&&
            before.output_check->cases.size()==5&&!before.derived_preparation_probe,
        "Original U8L7 five cases or compiler source changed");
    const auto& first=before.output_check->cases.front();
    require(!first.outputs.empty(),"Original U8L7 first-case oracle missing");

    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto declaration=JsonObject::Parse(winrt::hstring{LR"contract({
        "activity":"block_0016_0002",
        "baseClass":"Medida",
        "derivedClass":"MedidaIdentificada",
        "balanceClass":"Balance",
        "resourceClass":"Recurso",
        "replacementFunction":"sustituir",
        "destinationParameter":"destino",
        "numberParameter":"numero",
        "codeParameter":"codigo",
        "modeParameter":"modo",
        "balanceParameter":"balance",
        "printMethod":"escribir",
        "resourceAlive":"recursos_vivos",
        "baseCompleted":"bases_completadas",
        "derivedCompleted":"derivadas_completadas",
        "resourceCompleted":"recursos_completados",
        "baseDestroyed":"bases_destruidas",
        "derivedDestroyed":"derivadas_destruidas",
        "resourceDestroyed":"recursos_destruidos"
    })contract"});
    JsonArray checks;
    checks.Append(declaration);
    root.Insert(L"compilerDerivedPreparationChecks",checks);
    const auto revised_source=winrt::to_string(root.Stringify());
    const auto revised=parse_lesson_json(revised_source,"es");
    const auto progress=parse_lesson_progress_metadata(revised_source,"es");
    const auto& active=editor(revised,id);
    const auto& compact=editor(progress,id);
    require(active.derived_preparation_probe&&compact.derived_preparation_probe&&
            active.derived_preparation_probe==compact.derived_preparation_probe,
        "Full and progress readers disagree on U8L7 ownership/exception policy");
    require(active.initial_code==before.initial_code&&
            active.stable_id==before.stable_id&&
            active.address.page==before.address.page&&
            active.address.block==before.address.block&&
            active.address.progress_page==before.address.progress_page&&
            active.address.addition_namespace==before.address.addition_namespace&&
            active.output_check&&
            active.output_check->cases.size()==before.output_check->cases.size()&&
            JsonObject::Parse(winrt::to_hstring(active.raw_json)).Stringify()==
                JsonObject::Parse(winrt::to_hstring(before.raw_json)).Stringify()&&
            revised.state_layout.signatures==original.state_layout.signatures&&
            progress.state_layout.signatures==original.state_layout.signatures,
        "U8L7 root authoring altered published source or saved identity");

    const auto& policy=*active.derived_preparation_probe;
    const auto spans=cpp_multi_body_detail::spans(
        policy.editable.original_source,policy.editable.bodies,false);
    require(spans&&spans->size()==1,"U8L7 original function body was not found");
    const std::array<std::string,1> replacements{{
        R"cpp(
    auto candidate=std::make_unique<MedidaIdentificada>(numero,codigo,modo,balance);
    destino=std::move(candidate);
)cpp"
    }};
    auto program=before.initial_code;
    for(std::size_t i=replacements.size();i!=0;) {
        --i;
        const auto span=spans->at(i);
        program.replace(span.begin,span.end-span.begin,replacements[i]);
    }
    require(cpp_derived_preparation_matches(policy,program)&&
            cpp_derived_preparation_private_driver(policy,program).has_value(),
        "A correct U8L7 source was rejected by the general protected-body gate");

    CppRunRequest request;
    request.request_id=807;
    request.revision=21;
    request.source_utf8=program;
    request.stdin_utf8=first.standard_input;
    FreshExecution observed;
    observed.source_revision=request.revision;
    observed.compilation_succeeded=true;
    observed.execution_succeeded=true;
    observed.execution_completed=true;
    observed.exit_code=0;
    observed.standard_input=request.stdin_utf8;
    observed.standard_output=first.outputs.front();
    observed.executable=first.executable;

    state::StateData data;
    state::prepare_lesson_state(data,original);
    state::Draft draft;
    draft.source=program;
    draft.input=request.stdin_utf8;
    state::put_draft(data,draft_key("programming",original,before),draft);
    data.preferences["qa-other-course"]["draft"]=std::string("untouched");
    require(record_compiler_puzzle_case(data,original,before,request,observed,
                "Old first-case public output")&&
            compiler_puzzle_completed(data,original,before),
        "U8L7 original stdout-only achievement was not registered");
    const auto old=data.preferences.at("native_windows");
    const auto snapshot=data;
    state::StateStore store(qa/"derived-preparation-state");
    require(store.load().preferences.empty(),"U8L7 isolated profile is not fresh");
    store.save(data);
    data=reopen(store);
    require(data==snapshot,"U8L7 original state changed on reopen");
    state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);
    require(!compiler_puzzle_completed(data,revised,active),
        "U8L7 historical stdout silently earned the new semantic proof");

    const auto unchanged=data;
    request.derived_preparation_probe=active.derived_preparation_probe;
    require(compiler_runtime_request_allowed(active,request),
        "A correct U8L7 member edit was denied by the Windows guard");
    require(!record_compiler_puzzle_case(data,revised,active,request,observed,
                "Transcript without proof")&&data==unchanged,
        "U8L7 public stdout alone credited a private owner-preparation goal");
    observed.derived_preparation_probe_correct=true;
    observed.derived_preparation_request_fingerprint="unrelated";
    require(!record_compiler_puzzle_case(data,revised,active,request,observed,
                "Forged capture")&&data==unchanged,
        "U8L7 unrelated private evidence modified old state");
    observed.derived_preparation_request_fingerprint=
        cpp_derived_preparation_request_fingerprint(request);
    require(!observed.derived_preparation_request_fingerprint.empty()&&
            record_compiler_puzzle_case(data,revised,active,request,observed,
                "Fresh private derived-owner preparation witness")&&
            compiler_puzzle_completed(data,revised,active),
        "U8L7 authentic source/revision binding was not credited");
    const auto& saved=data.preferences.at("native_windows");
    for(const auto& [key,value]:old) {
        const auto it=saved.find(key);
        require(it!=saved.end()&&it->second==value,
            "U8L7 private witness rewrote original output receipts");
    }
    const auto key="compiler-puzzle/v1/"+revised.locale+"/"+revised.id+"/"+
        exercise_key(revised,active)+"/"+
        state::sha256(reference_objective_frame(revised,active))+
        "/derived-preparation/v1";
    require(saved.size()==old.size()+1&&saved.contains(key)&&
            std::get_if<std::string>(&saved.at(key)),
        "U8L7 did not append one new signed private receipt");
    const auto retained=state::get_draft(data,
        draft_key("programming",revised,active),active.initial_code,
        active.standard_input.value_or(""));
    require(retained.source==draft.source&&retained.input==draft.input&&
            state::string_value(data,"qa-other-course","draft")=="untouched",
        "U8L7 discarded a saved draft or unrelated course preferences");

    auto forged=data;
    forged.preferences["native_windows"][key]=std::string(64,'0');
    const auto frozen=forged;
    bool rejected=false;
    try {state::validate_lesson_state(forged,revised);}
    catch(const std::runtime_error&) {rejected=true;}
    require(rejected&&forged==frozen&&
            !compiler_puzzle_completed(forged,revised,active),
        "Forged U8L7 receipt was accepted or mutated the profile");
    auto missing=data;
    missing.preferences["native_windows"].erase(key);
    require(!compiler_puzzle_completed(missing,revised,active),
        "Missing U8L7 receipt was inherited from historical stdout");
    store.save(data);
    const auto persisted=reopen(store);
    require(persisted==data&&compiler_puzzle_completed(persisted,revised,active),
        "Verified U8L7 receipt was lost on StateStore reopen");
    state::validate_lesson_state(persisted,revised);
    require(load_lesson(path).raw_json==original.raw_json,
        "U8L7 QA changed the actual lesson JSON");
    std::cout<<"PASS U8L7 source-bound StateStore: five cases preserved, "
        "full/progress loading, old receipts and drafts untouched, "
        "forged evidence rejected, first case accredited only with a new witness.\n";
    return 0;
}
} // namespace lgaed::test
