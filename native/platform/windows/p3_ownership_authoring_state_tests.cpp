// Native WinRT authorization of the fourteen P3 ownership declarations.
// All opt-in changes are confined to memory: Work owns lesson publication.
// This checks real loader and progress projections, not running student code.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
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
        std::cout<<"PASS native P3 "<<unit
                 <<": authentic owner, metadata and full WinRT loader, "
                 <<"unchanged source/IDs and rejected forged owner\n";
    }
    std::cout<<"PASS fourteen P3 native authoring projections; "
             <<"no student code, lesson or progress modified\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL native P3 authoring: "<<error.what()<<'\n';
    return 1;
}
