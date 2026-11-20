// Native WinRT authorization of the fourteen P3 ownership declarations.
// All opt-in changes are confined to memory: Work owns lesson publication.
// This checks real loader and progress projections, not running student code.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <set>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
void require(bool yes,std::string_view reason) {
    if(!yes)throw std::runtime_error(std::string(reason));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    require(file.good(),"P3 authoring source is missing");
    return {std::istreambuf_iterator<char>{file},std::istreambuf_iterator<char>{}};
}
struct Plan {std::string_view unit,relative,field;};
constexpr std::array<Plan,14> plans{{
    {"U6L5","estado_y_ciclo_de_vida_de_clases/lesson_transferir_propiedad_entre_unique_ptr.json","compilerIndexedUniqueTransferChecks"},
    {"U6L6","estado_y_ciclo_de_vida_de_clases/lesson_elegir_copia_y_movimiento_de_una_clase.json","compilerDefaultedMovePolicyChecks"},
    {"U6L7","estado_y_ciclo_de_vida_de_clases/lesson_definir_copia_independiente_y_conservar_el_destino.json","compilerIndependentCopyAssignmentChecks"},
    {"U6L8","estado_y_ciclo_de_vida_de_clases/lesson_conservar_invariantes_al_mover_una_clase.json","compilerMoveAssignmentInvariantChecks"},
    {"U6L10","estado_y_ciclo_de_vida_de_clases/lesson_gestionar_arrays_con_unique_ptr_y_cantidad.json","compilerArrayOwnerReadChecks"},
    {"U6L11","estado_y_ciclo_de_vida_de_clases/lesson_copiar_y_mover_arrays_sin_desajustar_la_cantidad.json","compilerArrayCopyMoveChecks"},
    {"U6L12","estado_y_ciclo_de_vida_de_clases/lesson_recibir_y_devolver_objetos_propietarios.json","compilerArrayReturnTakeChecks"},
    {"U8L5","composicion_y_herencia_publica/lesson_destruir_una_derivada_mediante_la_base.json","compilerPolymorphicDestructionChecks"},
    {"U8L6","composicion_y_herencia_publica/lesson_seguir_llamadas_virtuales_durante_construccion_y_destruccion.json","compilerVirtualLifetimeDispatchChecks"},
    {"U8L7","composicion_y_herencia_publica/lesson_conservar_recursos_si_falla_la_construccion_derivada.json","compilerDerivedPreparationChecks"},
    {"U8L8","composicion_y_herencia_publica/lesson_definir_una_interfaz_abstracta.json","compilerAbstractInterfaceChecks"},
    {"U8L9","composicion_y_herencia_publica/lesson_reunir_objetos_que_comparten_una_interfaz.json","compilerInterfaceCollectionChecks"},
    {"U8L10","composicion_y_herencia_publica/lesson_copiar_objetos_sin_perder_su_clase_concreta.json","compilerPolymorphicCloneChecks"},
    {"U10L1","clases_con_varias_bases/lesson_reunir_dos_bases_independientes_en_un_objeto.json","compilerDualInterfaceChecks"},
}};
const Block& owner(const Lesson& lesson,std::string_view id) {
    const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"P3 stable owner does not exist in the real lesson");
    return lesson.pages[at->page].blocks[at->block];
}
bool bound(const Block& block,std::string_view field) {
    if(field=="compilerIndexedUniqueTransferChecks")return block.indexed_unique_transfer_probe.has_value();
    if(field=="compilerDefaultedMovePolicyChecks")return block.defaulted_move_policy_probe.has_value();
    if(field=="compilerIndependentCopyAssignmentChecks")return block.independent_copy_assignment_probe.has_value();
    if(field=="compilerMoveAssignmentInvariantChecks")return block.move_assignment_invariant_probe.has_value();
    if(field=="compilerArrayOwnerReadChecks")return block.array_owner_read_probe.has_value();
    if(field=="compilerArrayCopyMoveChecks")return block.array_copy_move_probe.has_value();
    if(field=="compilerArrayReturnTakeChecks")return block.array_return_take_probe.has_value();
    if(field=="compilerPolymorphicDestructionChecks")return block.polymorphic_destruction_probe.has_value();
    if(field=="compilerVirtualLifetimeDispatchChecks")return block.virtual_lifetime_probe.has_value();
    if(field=="compilerDerivedPreparationChecks")return block.derived_preparation_probe.has_value();
    if(field=="compilerAbstractInterfaceChecks")return block.abstract_interface_probe.has_value();
    if(field=="compilerInterfaceCollectionChecks")return block.interface_collection_probe.has_value();
    if(field=="compilerPolymorphicCloneChecks")return block.polymorphic_clone_probe.has_value();
    if(field=="compilerDualInterfaceChecks")return block.dual_interface_probe.has_value();
    return false;
}
bool rejects_both(const JsonObject& damaged) {
    const auto source=winrt::to_string(damaged.Stringify());
    bool full=false,metadata=false;
    try{(void)parse_lesson_json(source,"es");}
    catch(const std::exception&){full=true;}
    try{(void)parse_lesson_progress_metadata(source,"es");}
    catch(const std::exception&){metadata=true;}
    return full&&metadata;
}
bool same_address(const StateAddress& a,const StateAddress& b) {
    return a.page==b.page&&a.block==b.block&&
        a.addition_namespace==b.addition_namespace&&
        a.progress_page==b.progress_page;
}
}
int main() try {
    const auto root=std::filesystem::path(build::source_root);
    const std::string prefix=
        "app/src/main/assets/content/es/programming/cpp_language_memory_oop/";
    const auto contents=read(root/"docs/examples/p3_ownership_authoring.json");
    const auto manifest=JsonObject::Parse(winrt::to_hstring(contents));
    require(manifest.GetNamedNumber(L"schemaVersion")==1.0&&
            manifest.GetNamedString(L"goal")==
                L"P3_ACREDITACION_TRANSFERENCIA_DE_PROPIEDAD",
        "P3 authoring manifest version or identity changed");
    const auto entries=manifest.GetNamedArray(L"entries");
    require(entries.Size()==plans.size(),
        "P3 authoring manifest lost its fourteen distinct objectives");
    // Existing individual fixture tests check one profile per owner. The
    // entire course also needs a *shared* profile: fourteen historical drafts
    // must retain independent keys and survive activation of every opt-in.
    // This is model/state QA, not a fabricated claim of compiling student C++.
    state::StateData historical;
    historical.preferences["qa-p3-other-course"]["opaque"]=
        std::string("retain exactly");
    std::vector<Lesson> originals,revisions;
    std::vector<std::string> owners;
    std::vector<state::Draft> saved_drafts;
    std::vector<CppRunRequest> historical_requests;
    std::vector<FreshExecution> historical_executions;
    std::set<std::string> draft_keys;
    for(unsigned i=0;i<entries.Size();++i) {
        const auto item=entries.GetObjectAt(i);
        const auto unit=winrt::to_string(item.GetNamedString(L"unit"));
        const auto path=winrt::to_string(item.GetNamedString(L"lesson"));
        const auto field=winrt::to_string(item.GetNamedString(L"rootField"));
        const auto spec=item.GetNamedObject(L"declaration");
        const auto identity=winrt::to_string(spec.GetNamedString(L"activity"));
        require(unit==plans[i].unit&&
                path==prefix+std::string(plans[i].relative)&&
                field==plans[i].field,
            "P3 declaration does not match its authentic fixed lesson address");
        const auto file=root/path;
        const auto original_bytes=read(file);
        const auto before=load_lesson(file);
        const auto& old=owner(before,identity);
        require(before.state_layout.version==2&&
                old.type==BlockType::cpp_compiler&&old.output_check&&
                !old.output_check->cases.empty(),
            "P3 declaration lacks an authentic original compiler owner");
        auto authored=JsonObject::Parse(winrt::to_hstring(before.raw_json));
        if(authored.HasKey(winrt::to_hstring(field))) {
            const auto present=authored.GetNamedArray(winrt::to_hstring(field));
            require(present.Size()==1&&
                    present.GetObjectAt(0).Stringify()==spec.Stringify(),
                "The Work-published P3 objective differs from the reviewed contract");
        }
        JsonArray checks;checks.Append(spec);
        authored.Insert(winrt::to_hstring(field),checks);
        const auto overlay=winrt::to_string(authored.Stringify());
        const auto after=parse_lesson_json(overlay,"es");
        const auto metadata=parse_lesson_progress_metadata(overlay,"es");
        const auto& current=owner(after,identity);
        const auto& compact=owner(metadata,identity);
        require(bound(current,field)&&bound(compact,field)&&
                before.id==after.id&&before.id==metadata.id&&
                before.state_layout.signatures==after.state_layout.signatures&&
                before.state_layout.signatures==metadata.state_layout.signatures&&
                before.pages.size()==after.pages.size()&&
                current.stable_id==old.stable_id&&
                same_address(current.address,old.address)&&
                current.initial_code==old.initial_code&&
                current.output_check&&
                current.output_check->cases.size()==old.output_check->cases.size()&&
                JsonObject::Parse(winrt::to_hstring(current.raw_json)).Stringify()==
                    JsonObject::Parse(winrt::to_hstring(old.raw_json)).Stringify(),
            "A native loader lost the opt-in or changed the original P3 editor");
        const auto damaged=JsonObject::Parse(winrt::to_hstring(overlay));
        damaged.GetNamedArray(winrt::to_hstring(field)).GetObjectAt(0)
            .Insert(L"activity",JsonValue::CreateStringValue(L"block_9999_9999"));
        require(rejects_both(damaged),
            "Native P3 full/progress loader accepted a forged owner");
        require(read(file)==original_bytes,
            "Native P3 authoring QA modified the actual lesson asset");

        state::prepare_lesson_state(historical,before);
        state::Draft draft;
        draft.source=old.initial_code+
            "\n// Isolated P3 shared-profile draft before adoption: "+unit+"\n";
        draft.input=old.output_check->cases.front().standard_input;
        const auto saved_key=draft_key("programming",before,old);
        require(draft_keys.insert(saved_key).second,
            "Two historical P3 editors share a draft storage key");
        state::put_draft(historical,saved_key,draft);
        // Synthetic saved-output *state* fixture, not a claim to execute
        // unfinished initialCode. Private C++ variants are tested separately.
        // The first public case alone is sufficient: optional cases do not
        // create extra mandatory work for the learner.
        const auto& first=old.output_check->cases.front();
        require(!first.outputs.empty(),"P3 lost its first public stdout oracle");
        CppRunRequest request;
        request.request_id=1000+i;
        request.revision=2000+i;
        request.source_utf8=old.initial_code;
        request.stdin_utf8=first.standard_input;
        FreshExecution execution;
        execution.source_revision=request.revision;
        execution.compilation_succeeded=true;
        execution.execution_succeeded=true;
        execution.execution_completed=true;
        execution.exit_code=first.exit_code;
        execution.standard_input=first.standard_input;
        execution.standard_output=first.outputs.front();
        execution.executable=first.executable;
        require(record_compiler_puzzle_case(
                    historical,before,old,request,execution,
                    "Synthetic prior stdout-only receipt")&&
                compiler_puzzle_completed(historical,before,old),
            "P3 historical first-case receipt was not retained independently");
        historical_requests.push_back(std::move(request));
        historical_executions.push_back(std::move(execution));
        const auto loaded_draft=state::get_draft(historical,saved_key,
            old.initial_code,old.standard_input.value_or(""));
        require(loaded_draft.source==draft.source&&loaded_draft.input==draft.input,
            "Saving a P3 draft corrupted an earlier course owner");
        originals.push_back(before);
        revisions.push_back(after);
        owners.push_back(identity);
        saved_drafts.push_back(std::move(draft));
        std::cout<<"PASS native P3 "<<unit
                 <<": authentic owner, metadata and full WinRT loader, "
                 <<"unchanged source/IDs and rejected forged owner\n";
    }
    // Persist one mixed-course state, not fourteen separate state folders.
    // Fresh paths are created only under the disposable OS temp directory.
    // Existing profile directories are never removed or overwritten.
    const auto stamp=std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto profile=std::filesystem::temp_directory_path()/
        ("lgaed-p3-shared-qa-"+std::to_string(stamp));
    require(!std::filesystem::exists(profile),
        "P3 regression refuses to reuse a pre-existing state directory");
    state::StateStore store(profile);
    require(store.load().preferences.empty(),
        "An isolated P3 profile unexpectedly contained learner data");
    const auto initial=historical;
    // Archive every old receipt before opt-in, including its data type and
    // seal. New semantic checks may only append their own authenticated proof.
    const auto historical_receipts=historical.preferences.at("native_windows");
    store.save(historical);
    state::StateStore imported(profile);
    auto current=imported.load();
    require(current==initial,
        "A shared P3 profile lost original lessons or drafts on first reopen");
    for(std::size_t i=0;i<revisions.size();++i) {
        const auto& before=originals[i];
        const auto& after=revisions[i];
        const auto& old=owner(before,owners[i]);
        const auto& active=owner(after,owners[i]);
        require(draft_key("programming",before,old)==
                draft_key("programming",after,active),
            "Authoring a P3 semantic goal changed its historical draft key");
        state::prepare_lesson_state(current,after);
        state::validate_lesson_state(current,after);
        require(compiler_puzzle_completed(current,before,old)&&
                !compiler_puzzle_completed(current,after,active),
            "Historical P3 stdout was promoted into a new ownership objective");
        const auto untouched=current;
        // Replaying the old first case must never grant the newly authored
        // semantic goal without fresh source-bound independent evidence.
        require(!record_compiler_puzzle_case(
                    current,after,active,historical_requests[i],
                    historical_executions[i],"Unwitnessed historic replay")&&
                current==untouched,
            "Unwitnessed P3 replay changed the shared student profile");
        require(state::get_draft(current,
                    draft_key("programming",after,active),
                    active.initial_code,
                    active.standard_input.value_or("")).source==
                saved_drafts[i].source,
            "P3 semantic adoption lost an original draft in a shared profile");
        require(state::get_draft(current,
                    draft_key("programming",after,active),
                    active.initial_code,
                    active.standard_input.value_or("")).input==
                saved_drafts[i].input,
            "P3 semantic adoption lost original case input in a shared profile");
    }
    require(state::string_value(current,"qa-p3-other-course","opaque")==
                "retain exactly",
        "Enabling P3 owners modified an unrelated course preference");
    const auto& current_receipts=current.preferences.at("native_windows");
    for(const auto& [key,value]:historical_receipts) {
        const auto found=current_receipts.find(key);
        require(found!=current_receipts.end()&&found->second==value,
            "Enabling another P3 goal rewrote an old signed stdout receipt");
    }
    imported.save(current);
    state::StateStore final_reader(profile);
    const auto restored=final_reader.load();
    require(restored==current,
        "P3 new metadata and original drafts changed on durable reopen");
    for(std::size_t i=0;i<revisions.size();++i) {
        const auto& lesson=revisions[i];
        const auto& block=owner(lesson,owners[i]);
        const auto draft=state::get_draft(restored,
            draft_key("programming",lesson,block),block.initial_code,
            block.standard_input.value_or(""));
        require(draft.source==saved_drafts[i].source&&
                draft.input==saved_drafts[i].input,
            "One P3 adoption overwrote another lesson's historical draft");
        state::validate_lesson_state(restored,lesson);
        require(!compiler_puzzle_completed(restored,lesson,block)&&
                compiler_puzzle_completed(restored,originals[i],
                    owner(originals[i],owners[i])),
            "P3 durable reopen promoted or discarded a legacy stdout receipt");
    }
    std::cout<<"PASS P3 shared profile: fourteen first-case stdout receipts "
             <<"retained without semantic promotion, private replays refused, "
             <<"drafts and foreign preferences preserved through two reopens\n";
    std::cout<<"PASS fourteen P3 native authoring projections; "
             <<"no student code, lesson or progress modified\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL native P3 authoring: "<<error.what()<<'\n';
    return 1;
}
