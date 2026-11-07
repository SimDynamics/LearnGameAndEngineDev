#include "core/compiler_flow.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace std::chrono_literals;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);require(bool(input),"Missing owned U12 integration artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
state::StateData reopen_state(const state::StateStore& store) {
    // StateStore takes a profile directory but root() exposes its appended
    // state directory. Reopening root() directly would target state/state.
    state::StateStore reopened(store.root().parent_path());
    require(reopened.root()==store.root(),"The U12 QA reopen targeted another profile/state directory");
    return reopened.load();
}
struct Plan {const char* file;const char* editor;const char* complete;const char* partial=nullptr;};
constexpr std::array<Plan,18> formatting_plans{{
    {"representar_enteros_en_distintas_bases","block_0006_0002","block_0007_0005","block_0007_0001"},
    {"estado_del_formato_y_flujos_independientes","block_0006_0002","block_0007_0005"},
    {"precision_y_presentacion_de_double","block_0006_0002","block_0007_0005"},
    {"ancho_relleno_y_alineacion_de_campos","block_0005_0002","block_0007_0004"},
    {"ancho_relleno_y_alineacion_de_campos","block_0008_0003","block_0009_0001"},
    {"componer_informes_sin_cambiar_los_datos","block_0001_0002","block_0002_0001"},
    {"componer_informes_sin_cambiar_los_datos","block_0004_0002","block_0005_0005","block_0005_0001"},
    {"componer_informes_sin_cambiar_los_datos","block_0007_0003","block_0008_0001"},
    {"texto_con_campos_y_patrones_literales","block_0006_0002","block_0007_0006"},
    {"texto_con_campos_y_patrones_literales","block_0008_0003","block_0009_0001"},
    {"bases_y_anchuras_en_cada_campo","block_0006_0002","block_0007_0006"},
    {"bases_y_anchuras_en_cada_campo","block_0008_0003","block_0009_0001"},
    {"precision_por_campo_y_resumenes_formateados","block_0005_0003","block_0007_0006","block_0006_0001"},
    {"precision_por_campo_y_resumenes_formateados","block_0008_0003","block_0009_0001"},
    {"compartir_flujos_sin_acoplar_el_calculo","block_0003_0004","block_0004_0006"},
    {"compartir_flujos_sin_acoplar_el_calculo","block_0006_0002","block_0007_0001"},
    {"insertar_un_tipo_y_encadenar_la_salida","block_0003_0004","block_0005_0001"},
    {"extraer_un_tipo_sin_cambiarlo_en_fallo","block_0005_0005","block_0007_0001","block_0006_0001"}
}};
constexpr std::array<Plan,8> loading_state_plans{{
    {"leer_y_validar_una_linea_completa","block_0003_0003","block_0005_0001","block_0004_0001"},
    {"aceptar_registros_y_detectar_duplicados","block_0003_0003","block_0004_0001"},
    {"aceptar_registros_y_detectar_duplicados","block_0006_0002","block_0007_0001"},
    {"actualizar_todo_o_conservar_el_estado_anterior","block_0002_0003","block_0005_0001","block_0003_0001"},
    {"actualizar_todo_o_conservar_el_estado_anterior","block_0006_0002","block_0007_0001"},
    {"representar_presencia_y_cambiar_la_organizacion","block_0002_0004","block_0006_0001","block_0003_0001"},
    {"representar_presencia_y_cambiar_la_organizacion","block_0004_0003","block_0007_0001","block_0005_0001"},
    {"clasificar_caracteres_y_normalizar_una_copia","block_0009_0002","block_0010_0001"}
}};
struct Asset {Lesson lesson,metadata;std::filesystem::path path;std::string hash;};
const Block& at(const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);require(position.has_value(),"Missing stable U12 batch identity");
    return lesson.pages[position->page].blocks[position->block];
}
CompilerCompletionSnapshot snapshot(const Lesson& lesson,const state::StateData& data) {
    return compiler_completion_snapshot(lesson,[&](const Block& block){return compiler_puzzle_completed(data,lesson,block);});
}
void flow(const Asset& asset,const state::StateData& data) {
    const auto proofs=snapshot(asset.lesson,data),projected=snapshot(asset.metadata,data);
    require(proofs==projected,"Runtime and catalog disagree about fresh compiler completion/dependency chains");
    for(const auto& relation:asset.lesson.reference_after)for(const auto& identity:relation.blocks) {
        const auto position=stable_block_position(asset.lesson,identity);
        require(position.has_value(),"A U12 protected reference lost its actual stable block");
        const auto& block=asset.lesson.pages[position->page].blocks[position->block];
        const auto projected_position=stable_block_position(asset.metadata,identity);
        require(projected_position.has_value(),"Catalog omitted a protected reference owner");
        const auto& metadata=asset.metadata.pages[projected_position->page].blocks[projected_position->block];
        const auto available=compiler_reference_available(asset.lesson,position->page,block,proofs,
            [&](const Block& compiler){return compiler_puzzle_completed(data,asset.lesson,compiler);});
        const auto catalog_available=compiler_reference_available(asset.metadata,projected_position->page,metadata,projected,
            [&](const Block& compiler){return compiler_puzzle_completed(data,asset.metadata,compiler);});
        bool expected=proofs.at(relation.activity);
        for(const auto& prior:asset.lesson.pages[position->page].blocks) {
            if(&prior==&block)break;
            if(prior.type==BlockType::cpp_compiler)expected=expected&&compiler_puzzle_completed(data,asset.lesson,prior);
        }
        require(available==expected&&available==catalog_available,
            "A protected U12 reference bypassed its editor or catalog/runtime admission differs");
    }
    for(const auto& relation:asset.lesson.activity_after) {
        bool expected=true;for(const auto& identity:relation.prerequisites)expected=expected&&proofs.at(identity);
        require(compiler_activity_available(asset.lesson,at(asset.lesson,relation.activity),proofs)==expected
            &&compiler_activity_available(asset.metadata,at(asset.metadata,relation.activity),projected)==expected,
            "A dependent U12 activity bypassed its complete compiler chain or lost catalog parity");
    }
}
void preserve_values(const state::StateData& before,const state::StateData& after) {
    for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values)
        require(state::find(after,domain,key)&&*state::find(after,domain,key)==value,
            "A compiler result rewrote an unrelated response, draft, marker or previous achievement");
}
struct Capture {CppRunRequest request;CppRunResult result;FreshExecution execution;};
struct OrdinarySelection {std::optional<BlockType> type;std::string_view id;};
bool ordinary_selected(const Block& block,OrdinarySelection selection={}) {
    const auto kind=block.type;
    return (kind==BlockType::multiple_choice||kind==BlockType::output_prediction||kind==BlockType::fill_code||kind==BlockType::order_code)
        &&(!selection.type||*selection.type==kind)
        &&(selection.id.empty()||block.id==selection.id||block.stable_id==selection.id);
}
void ordinary_guides(const Asset& asset,state::StateStore& store,state::StateData& data,std::size_t expected,bool guided,OrdinarySelection selection={}) {
    using namespace winrt::Windows::Data::Json;
    std::size_t count=0;
    for(const auto& page:asset.lesson.pages)for(const auto& block:page.blocks) {
        if(!ordinary_selected(block,selection))continue;
        const auto raw=JsonObject::Parse(winrt::to_hstring(block.raw_json));
        const auto explanation=winrt::to_string(raw.GetNamedString(L"explanation"));
        const auto expected_guide=[&](std::optional<std::size_t> selected=std::nullopt){
            if(!raw.HasKey(L"reviewFeedback"))return std::string{};
            const auto value=raw.GetNamedValue(L"reviewFeedback");
            if(value.ValueType()==JsonValueType::String)return winrt::to_string(value.GetString());
            require(selected.has_value(),"A raw option guide needs its actual selected option");
            return winrt::to_string(value.GetArray().GetStringAt(static_cast<unsigned>(*selected)));
        };
        const auto preserve_other_answers=[&](const state::StateData& before,const state::StateData& after){
            auto protected_data=before;
            if(auto domain=protected_data.preferences.find("learning_progress");domain!=protected_data.preferences.end())
                domain->second.erase(state::response_storage_key(asset.lesson,block));
            preserve_values(protected_data,after);
        };
        const auto persist_rejection=[&](state::Response response,const AnswerEvaluation& evaluation,std::string_view guide){
            require(!evaluation.correct&&(!guided||!guide.empty()),"A wrong ordinary U12 answer succeeded or lost its authored guide");
            require(evaluation.feedback==guide,"A wrong U12 answer selected another raw authored guide");
            response.correct=false;response.raw_json=R"({"incorrectAttempts":2,"qaMetadata":"retained ordinary retry"})";
            state::mark_response_reviewed(response,block);const auto before=data;
            state::put_response(data,asset.lesson,block,response);preserve_other_answers(before,data);store.save(data);
            const auto saved=reopen_state(store);require(saved==data,"An ordinary rejected U12 answer changed while reopening");
            const auto restored=state::get_response(saved,asset.lesson,block);
            require(restored&&!restored->correct&&state::response_reviewed(*restored,block)
                &&restored->selected==response.selected&&restored->answer==response.answer&&restored->order==response.order
                &&state::choice_incorrect_feedback(*restored,block)==guide,
                "A U12 ordinary rejection lost its answer, review ownership or exact authored guide on reopen");
        };
        AnswerEvaluation correct,wrong;state::Response rejected,accepted;
        if(block.type==BlockType::multiple_choice) {
            const auto index=static_cast<std::size_t>(raw.GetNamedNumber(L"correctIndex"));
            require(block.correct_index==index&&raw.GetNamedArray(L"options").Size()==block.options.size(),"A U12 choice lost its raw authored options or correct index");
            correct=evaluate_choice(block,index);accepted.selected=static_cast<std::int64_t>(index);
            for(std::size_t option=0;option<block.options.size();++option)if(option!=index) {
                const auto verdict=evaluate_choice(block,option);
                state::Response selected;selected.selected=static_cast<std::int64_t>(option);
                persist_rejection(selected,verdict,expected_guide(option));
            }
        } else if(block.type==BlockType::output_prediction||block.type==BlockType::fill_code) {
            accepted.answer=winrt::to_string(raw.GetNamedArray(L"answers").GetStringAt(0));correct=evaluate_text_answer(block,accepted.answer);
            rejected.answer="__not_the_requested_U12_output__";wrong=evaluate_text_answer(block,rejected.answer);
            require(!evaluate_text_answer(block,"").correct,"An empty ordinary U12 answer succeeded");
            persist_rejection(rejected,wrong,expected_guide());
        } else {
            std::vector<std::size_t> order;for(const auto& value:raw.GetNamedArray(L"correctOrder"))order.push_back(static_cast<std::size_t>(value.GetNumber()));
            require(order==block.correct_order,"The native U12 order differs from the raw authored permutation");
            correct=evaluate_order(block,order);accepted.order.assign(order.begin(),order.end());
            auto candidate=order;bool found=false;
            for(std::size_t first=0;first<candidate.size()&&!found;++first)for(std::size_t second=first+1;second<candidate.size();++second) {
                std::swap(candidate[first],candidate[second]);const auto verdict=evaluate_order(block,candidate);
                if(!verdict.correct){wrong=verdict;rejected.order.assign(candidate.begin(),candidate.end());found=true;break;}
                std::swap(candidate[first],candidate[second]);
            }
            require(found,"The U12 order fixture lacks a genuinely incorrect full permutation");
            persist_rejection(rejected,wrong,expected_guide());
        }
        require(correct.correct&&correct.feedback==explanation&&!explanation.empty(),"A correct U12 answer lost its raw complete success explanation");
        const auto before=data;accepted.correct=true;accepted.raw_json=R"({"incorrectAttempts":2,"qaMetadata":"retained ordinary retry"})";
        state::put_response(data,asset.lesson,block,accepted);preserve_other_answers(before,data);store.save(data);
        const auto accepted_saved=reopen_state(store);
        const auto response=state::get_response(accepted_saved,asset.lesson,block);
        require(accepted_saved==data&&response&&response->correct&&state::choice_correct_feedback(*response,block)==explanation,
            "A correct U12 ordinary answer or its exact authored explanation was not preserved on reopen");
        ++count;
    }
    require(count==expected,"The focused ordinary U12 guide batch omitted or silently added an exercise");
}
std::string xml_text(std::string_view value) {
    std::string result;for(const auto character:value)switch(character){
        case '&':result+="&amp;";break;case '<':result+="&lt;";break;case '>':result+="&gt;";break;case '"':result+="&quot;";break;
        case '\r':result+="&#13;";break;case '\n':result+="&#10;";break;case '\t':result+="&#9;";break;default:result+=character;}
    return result;
}
void export_preferences(const std::filesystem::path& root,const state::StateData& data) {
    const auto directory=root/L"shared_prefs";std::filesystem::create_directories(state::native_io_path(directory));
    for(const auto& [domain,values]:data.preferences){
        std::string xml="<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for(const auto& [key,value]:values)std::visit([&](const auto& item){
            using T=std::decay_t<decltype(item)>;const auto name=" name=\""+xml_text(key)+"\"";
            if constexpr(std::is_same_v<T,std::string>)xml+="<string"+name+">"+xml_text(item)+"</string>";
            else if constexpr(std::is_same_v<T,bool>)xml+="<boolean"+name+" value=\""+(item?"true":"false")+"\"/>";
            else if constexpr(std::is_same_v<T,std::int64_t>)xml+="<long"+name+" value=\""+std::to_string(item)+"\"/>";
            else if constexpr(std::is_same_v<T,std::vector<std::string>>){xml+="<set"+name+">";
                for(const auto& entry:item)xml+="<string>"+xml_text(entry)+"</string>";xml+="</set>";}
            else throw std::runtime_error("Unexpected floating QA preference in the ordinary-guide fixture");
        },value);
        xml+="</map>";std::ofstream output(state::native_io_path(directory/(domain+".xml")),std::ios::binary|std::ios::trunc);
        output.write(xml.data(),static_cast<std::streamsize>(xml.size()));require(bool(output),"Could not export isolated ordinary-guide QA preferences");
    }
}
enum class OrdinaryBatch {formatting,late,final};
int ordinary_guides_only(OrdinaryBatch batch=OrdinaryBatch::formatting) {
    const bool late=batch==OrdinaryBatch::late,final=batch==OrdinaryBatch::final;
    struct GuidePlan {const char* file;std::size_t count;bool guided=true;OrdinarySelection selection={};};
    constexpr std::array<GuidePlan,9> formatting{{
        {"representar_enteros_en_distintas_bases",11},{"estado_del_formato_y_flujos_independientes",12},
        {"precision_y_presentacion_de_double",12},{"ancho_relleno_y_alineacion_de_campos",13},
        {"componer_informes_sin_cambiar_los_datos",4},{"texto_con_campos_y_patrones_literales",17},
        {"bases_y_anchuras_en_cada_campo",20},{"precision_por_campo_y_resumenes_formateados",18},
        {"compartir_flujos_sin_acoplar_el_calculo",7}
    }};
    constexpr std::array<GuidePlan,5> late_plans{{
        {"clasificar_caracteres_y_normalizar_una_copia",15},
        {"etiquetas_y_alcance_de_los_identificadores",6,false},
        {"leer_un_grupo_con_cierre_propio",6},{"componer_grupos_sin_mezclar_sus_datos",5},
        {"extraer_un_tipo_sin_cambiarlo_en_fallo",1,true,{BlockType::order_code,"orden_para_conservar_el_destino"}}
    }};
    constexpr std::array<GuidePlan,8> final_plans{{
        {"escribir_un_formato_que_pueda_leerse",5},{"comprobar_ida_y_vuelta_sin_perder_datos",6,false},
        {"construir_un_programa_de_carga_y_guardado",1,false,{BlockType::multiple_choice,"block_0001_0002"}},
        {"construir_un_programa_de_carga_y_guardado",1,false,{BlockType::multiple_choice,"block_0004_0004"}},
        {"construir_un_programa_de_carga_y_guardado",1,true,{BlockType::multiple_choice,"block_0004_0005"}},
        {"construir_un_programa_de_carga_y_guardado",1,false,{BlockType::output_prediction,"block_0009_0001"}},
        {"construir_un_programa_de_carga_y_guardado",1,false,{BlockType::output_prediction,"block_0009_0002"}},
        {"construir_un_programa_de_carga_y_guardado",1,false,{BlockType::multiple_choice,"block_0009_0003"}}
    }};
    const std::span<const GuidePlan> plans=final?std::span<const GuidePlan>(final_plans):
        (late?std::span<const GuidePlan>(late_plans):std::span<const GuidePlan>(formatting));
    const auto selected_by_any_plan=[&](std::string_view file,const Block& block){return std::ranges::any_of(plans,[&](const auto& plan){
        return file==plan.file&&ordinary_selected(block,plan.selection);});};
    const LaunchOptions options{ProfileId::Codex,(final?L"u12-final-ordinary-guides-":
        (late?L"u12-late-ordinary-guides-":L"u12-ordinary-guides-"))+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);auto data=store.load();require(data.preferences.empty(),"The private ordinary-guide profile contains old state");
    data.preferences["u12-batch-unrelated"]["opaque"]=std::string(" Preserve\t\r\n保存 ");
    narration::History history;
    for(unsigned index=0;index<3;++index)history=narration::begin(std::move(history),"qa-storage-pause-"+std::to_string(index),
        "qa-storage-owner-"+std::to_string(index),{0,static_cast<std::int64_t>(117+index),0,false},
        "storage-only source; no recording or listening evidence","qa-storage-page-"+std::to_string(index));
    narration::write_history(data,history);
    const auto root=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo";
    std::map<std::string,Asset> assets;std::set<std::string> selected_owners;std::size_t choices=0,predictions=0,fills=0,orders=0,total=0;
    for(const auto& plan:plans){
        if(!assets.contains(plan.file)){Asset asset;asset.path=root/(std::string("lesson_")+plan.file+".json");const auto bytes=read(asset.path);
            asset.hash=state::sha256(bytes);asset.lesson=parse_lesson_json(bytes);asset.metadata=parse_lesson_progress_metadata(bytes);
            state::prepare_lesson_state(data,asset.lesson);
            for(const auto& page:asset.lesson.pages)for(const auto& block:page.blocks)if(block.type==BlockType::cpp_compiler){
                state::Draft draft;draft.source=block.initial_code+"\n// isolated uncompiled guide-preservation draft\r\n";
                draft.input=" \t preserved QA input\r\n";draft.selection_start=2;draft.selection_end=7;
                draft.indentation_json=state::make_indentation_metadata(draft.source,{});
                state::put_draft(data,draft_key("programming",asset.lesson,block),draft);}
            assets.emplace(plan.file,std::move(asset));
        }
        const auto& asset=assets.at(plan.file);
        for(const auto& page:asset.lesson.pages)for(const auto& block:page.blocks){
            if(ordinary_selected(block,plan.selection)){
                require(selected_owners.insert(asset.lesson.id+'/'+block.stable_id).second,"A guide fixture evaluated the same ordinary owner twice");
                choices+=block.type==BlockType::multiple_choice;predictions+=block.type==BlockType::output_prediction;
                fills+=block.type==BlockType::fill_code;orders+=block.type==BlockType::order_code;
                if(!plan.guided)require(!winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(block.raw_json)).HasKey(L"reviewFeedback"),
                    "The selected no-guide activity acquired an unreviewed authored guide");
                const auto position=stable_block_position(asset.metadata,block.stable_id);
                require(position&&asset.metadata.pages[position->page].blocks[position->block].text_semantics==block.text_semantics,
                    "Runtime and progress metadata disagree about the selected ordinary field's exact semantic owners");
            }
        }
        total+=plan.count;
    }
    require((final?(total==17&&choices==12&&predictions==5&&fills==0&&orders==0):
        (late?(total==33&&choices==20&&predictions==10&&fills==2&&orders==1):
        (total==114&&choices==48&&predictions==59&&fills==7&&orders==0)))&&selected_owners.size()==total
        &&assets.size()==(final?3u:(late?5u:9u)),
        "The ordinary-guide fixture lost its independently selected lesson/type inventory");
    const auto untouched=data;store.save(data);
    for(std::size_t index=0;index<plans.size();++index){ordinary_guides(assets.at(plans[index].file),store,data,plans[index].count,plans[index].guided,plans[index].selection);
        std::cout<<"PASS "<<plans[index].file<<": "<<plans[index].count<<" ordinary activities ("
            <<(plans[index].guided?"authored retry guides":"no authored retry guide")<<") and exact state reopen\n";}
    preserve_values(untouched,data);require(narration::read_history(data)==history,"An ordinary answer altered one of the three storage-only pauses");
    const auto exported=environment.paths.root/L"ordinary-guide-export";export_preferences(exported,data);
    const auto preview=state::inspect_android_export(exported);require(preview.data==data,"Android XML inspection changed ordinary answers, drafts, attempts or typed metadata");
    const auto destination=environment.paths.root/L"ordinary-guide-import";state::commit_android_import(preview,destination);
    state::StateStore imported_store(destination);auto imported=imported_store.load();require(imported==data,"The isolated Android XML transaction changed ordinary guide state");
    std::size_t imported_answers=0,excluded_answers=0;
    for(const auto& [file,asset]:assets){
        state::prepare_lesson_state(imported,asset.lesson);state::validate_lesson_state(imported,asset.lesson);
        for(const auto& page:asset.lesson.pages)for(const auto& block:page.blocks){
            if(block.type==BlockType::cpp_compiler)require(!compiler_puzzle_completed(imported,asset.lesson,block),"Ordinary guide QA manufactured compiler execution credit");
            if(!selected_by_any_plan(file,block)){
                if(ordinary_selected(block)){
                    require(!state::get_response(imported,asset.lesson,block),"The filtered guide fixture manufactured an answer outside its selected activity IDs");
                    ++excluded_answers;
                }
                continue;
            }
            const auto response=state::get_response(imported,asset.lesson,block);
            const auto authored=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(block.raw_json));
            require(response&&response->correct&&state::choice_correct_feedback(*response,block)==winrt::to_string(authored.GetNamedString(L"explanation")),
                "An imported ordinary achievement lost its exact authored explanation");++imported_answers;}
        require(state::sha256(read(asset.path))==asset.hash,"Ordinary guide QA rewrote an active curricular asset");}
    require(imported_answers==total&&excluded_answers==(late?8u:0u),"The XML round trip verified the wrong selected/excluded ordinary activity set");
    require(imported==data&&narration::read_history(imported)==history,"Revalidating imported ordinary state modified unrelated progress or pause metadata");
    imported_store.save(imported);require(reopen_state(imported_store)==data,"Imported ordinary guide state changed on Windows reopening");
    for(const auto& file:preview.files)require(state::sha256(state::read_bytes(exported/file.relative_path))==file.sha256&&
        state::sha256(state::read_bytes(destination/L"state"/L"active"/L"android"/file.relative_path))==file.sha256,
        "The isolated XML transaction modified its source or archived original files");
    std::cout<<"PASS "<<(final?"17 ordinary U12L24-L26 activities (six authored guides, eleven legitimate guide absences)":
        (late?"32 ordinary U12L20-L23 activities plus only the one U12L15 order control (eight neighboring answers untouched)":
        "114 ordinary U12L5-L13 activities"))<<": per-option rejection, raw authored guides/explanations, exact StateStore/XML import, drafts and three storage-only pauses; 0 compiler programs, no UI/audio.\n";
    return 0;
}
}

int main(int argc,char** argv) try {
    const bool loading_state=argc==2&&std::string_view(argv[1])=="--loading-state";
    const bool guides_only=argc==2&&std::string_view(argv[1])=="--ordinary-guides";
    const bool late_guides=argc==2&&std::string_view(argv[1])=="--late-ordinary-guides";
    const bool final_guides=argc==2&&std::string_view(argv[1])=="--final-ordinary-guides";
    require(argc==1||loading_state||guides_only||late_guides||final_guides,"Usage: u12_compiler_batch_tests [--loading-state|--ordinary-guides|--late-ordinary-guides|--final-ordinary-guides]");
    if(guides_only||late_guides||final_guides)return ordinary_guides_only(final_guides?OrdinaryBatch::final:
        (late_guides?OrdinaryBatch::late:OrdinaryBatch::formatting));
    const std::span<const Plan> plans=loading_state?std::span<const Plan>(loading_state_plans):std::span<const Plan>(formatting_plans);
    const LaunchOptions options{ProfileId::Codex,(loading_state?L"u12-loading-batch-":L"u12-batch-")+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    const auto source_root=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo";
    std::map<std::string,Asset> assets;
    state::StateStore store(environment.paths.root);auto data=store.load();require(data.preferences.empty(),"The private U12 profile contains old state");
    data.preferences["u12-batch-unrelated"]["opaque"]=std::string(" Preserve\t\r\n保存 ");
    for(const auto& plan:plans)if(!assets.contains(plan.file)) {
        Asset asset;asset.path=source_root/(std::string("lesson_")+plan.file+".json");
        const auto bytes=read(asset.path);asset.hash=state::sha256(bytes);asset.lesson=parse_lesson_json(bytes);
        asset.metadata=parse_lesson_progress_metadata(bytes);state::prepare_lesson_state(data,asset.lesson);
        state::save_current_page(data,asset.lesson,asset.lesson.pages.size()-1);
        assets.emplace(plan.file,std::move(asset));
    }
    require(assets.size()==(loading_state?5:11),"The selected U12 compiler batch changed its exact lesson inventory");
    for(const auto& plan:plans) {
        const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        require(editor.output_check&&editor.output_check->cases.size()==1&&editor.output_check->cases.front().standard_input.empty(),
            "A reviewed U12 editor changed from its single aggregate empty-input objective");
        if(loading_state)require(editor.project_files.empty()&&editor.source_files.empty()&&!editor.cmake&&
                !editor.output_check->cases.front().executable&&editor.output_check->cases.front().exit_code==0,
            "A loading-state capture requires its exact current single-source, empty-input, default-target process frame");
        state::Draft draft;draft.source=editor.initial_code+(loading_state?"":"\n// isolated personal QA draft\n");draft.input="";
        draft.selection_start=2;draft.selection_end=7;state::put_draft(data,draft_key("programming",asset.lesson,editor),draft);
    }
    store.save(data);
    const auto programs=environment.paths.projects/L"Código & (U12 integrado)";
    std::uint64_t sequence=0;unsigned runs=0,passed=0,failed=0;
    std::set<std::string> captured_loading_frames;
    std::set<std::filesystem::path> captured_run_directories;
    const auto test=[&](std::string_view name,const std::function<void()>& action) {
        try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    const auto run=[&](std::string source) {
        Capture capture;capture.request.request_id=++sequence;capture.request.revision=100+sequence;
        capture.request.source_utf8=std::move(source);capture.request.projects_directory=programs;
        capture.request.compile_timeout=30s;capture.request.execution_timeout=3s;
        if(loading_state)require(captured_loading_frames.insert(compiler_program_frame(capture.request)).second,
            "The reviewed loading-state plans no longer have twenty distinct source/project frames");
        ++runs;capture.result=run_cpp(capture.request);capture.execution=capture_cpp_execution(capture.request,capture.result);
        require(capture.result.error_utf8.empty()&&capture.result.compilation.succeeded()
            &&capture.execution.execution_completed&&capture.execution.execution_succeeded&&capture.execution.exit_code==0,
            "A U12 integration program failed to compile or complete normally: "+capture.result.error_utf8+
            capture.result.compilation.stdout_bytes+capture.result.compilation.stderr_bytes);
        require(captured_run_directories.insert(capture.result.run_directory).second,
            "A separate U12 compiler action reused another capture's run directory");
        require(read(capture.result.run_directory/L"main.cpp")==capture.request.source_utf8
            &&read(capture.result.run_directory/L"stdin.bin")==capture.request.stdin_utf8
            &&read(capture.result.run_directory/L"execution-stdout.bin")==capture.execution.standard_output
            &&read(capture.result.run_directory/L"execution-stderr.bin")==capture.execution.standard_error,
            "The adapter changed the captured U12 source, input or raw output channels");
        return capture;
    };
    // Preserve the old formatting batch's controlled empty probe. The loading
    // batch below instead captures its eight different published starters.
    std::optional<Capture> empty;
    if(!loading_state){empty=run("int main(){return 0;}\n");require(empty->execution.standard_output.empty(),"The empty U12 probe wrote output");}
    for(const auto& plan:plans)test(std::string(plan.file)+"/"+plan.editor,[&] {
        const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        const auto initial=data;flow(asset,data);
        require(!compiler_puzzle_completed(data,asset.lesson,editor),"An unexecuted U12 editor inherited another goal's achievement");
        // The new batch uses every published starter exactly. They have eight
        // different source/program frames, so none shares an execution capture.
        const auto starter=loading_state?run(editor.initial_code):*empty;
        require(evaluate_cpp_output_check(*editor.output_check,starter.execution,starter.request.revision).verdict==OutputVerdict::incorrect
            &&!record_compiler_puzzle_case(data,asset.lesson,editor,starter.request,starter.execution,"Incorrecto\nPublished starter output.")&&data==initial,
            "An empty successful program completed a nonempty U12 aggregate objective");
        const auto reject=[&](const Capture& capture) {
            require(evaluate_cpp_output_check(*editor.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::incorrect,
                "An incomplete U12 reference satisfied the complete aggregate output");
            require(record_compiler_puzzle_review(data,asset.lesson,editor,capture.request,capture.execution,"Incorrecto\nQA: incomplete aggregate output."),
                "An actual U12 rejection could not retain its review");
            preserve_values(initial,data);store.save(data);const auto restored=reopen_state(store);
            const auto feedback=compiler_puzzle_current_feedback(restored,asset.lesson,editor,capture.request);
            require(restored==data,"Reopening changed the exact saved U12 rejection state");
            require(feedback&&!feedback->correct&&!compiler_puzzle_completed(restored,asset.lesson,editor),
                "Reopening lost an actual U12 rejection or invented completion");flow(asset,restored);
        };
        reject(starter);
        const auto require_reference=[&](const Block& reference) {
            const auto owner=reference_activity(asset.lesson,reference);
            require(reference.type==BlockType::code&&owner&&
                asset.lesson.pages[owner->page].blocks[owner->block].stable_id==editor.stable_id,
                "The current U12 program is not a published reference protected by this exact editor");
        };
        if(plan.partial){const auto& partial=at(asset.lesson,plan.partial);require_reference(partial);reject(run(partial.code));}
        const auto& reference=at(asset.lesson,plan.complete);
        require_reference(reference);
        const auto capture=run(reference.code);
        const auto verdict=evaluate_cpp_output_check(*editor.output_check,capture.execution,capture.request.revision);
        require(verdict.verdict==OutputVerdict::correct&&verdict.stdout_correct&&verdict.exit_code_correct,
            "The complete real U12 reference did not satisfy its authored objective");
        const auto owned="compiler-puzzle/v1/"+asset.lesson.locale+'/'+asset.lesson.id+'/'+exercise_key(asset.lesson,editor)+'/';
        auto protected_initial=initial; // Only this editor's temporary rejection may be retired.
        if(auto domain=protected_initial.preferences.find("native_windows");domain!=protected_initial.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(owned+state::sha256(reference_objective_frame(asset.lesson,editor))+"/review/");});
        require(record_compiler_puzzle_case(data,asset.lesson,editor,capture.request,capture.execution,"Correcto\n"+verdict.feedback)
            &&compiler_puzzle_case_count(data,asset.lesson,editor)==1&&compiler_puzzle_completed(data,asset.lesson,editor),
            "A fresh complete U12 program did not earn its own aggregate achievement");
        preserve_values(protected_initial,data);require(compiler_puzzle_review(data,asset.lesson,editor,capture.request).empty(),
            "An actual complete U12 program retained obsolete rejected feedback");
        store.save(data);require(reopen_state(store)==data,"A real U12 achievement changed on reopen");
        flow(asset,data);
        if(loading_state&&std::string_view(plan.file)=="clasificar_caracteres_y_normalizar_una_copia") {
            // This batch excludes the already-tested first CRT editor. A real
            // receipt for its successor cannot manufacture that prerequisite.
            const auto& first=at(asset.lesson,"block_0005_0004");const auto proofs=snapshot(asset.lesson,data);
            require(!compiler_puzzle_completed(data,asset.lesson,first)&&!proofs.at(first.stable_id)
                &&!proofs.at(editor.stable_id),"The second L20 editor bypassed the unresolved first CRT prerequisite");
        }
        if(std::string_view(plan.file)=="compartir_flujos_sin_acoplar_el_calculo"&&std::string_view(plan.editor)=="block_0006_0002")
            require(read(capture.result.run_directory/L"balance_compartido_lgp.txt")=="Balance=5",
                "The real U12L13 round trip did not preserve its nine exact file bytes");
        if(std::string_view(plan.file)=="insertar_un_tipo_y_encadenar_la_salida")
            require(read(capture.result.run_directory/L"recuento_lgp.txt")=="[10;10]",
                "The real U12L14 battery did not replace the file with its final seven bytes");
    });
    if(loading_state)test("nineteen ordinary U12L16-L19 answers, exact guides and state reopen",[&] {
        ordinary_guides(assets.at("leer_y_validar_una_linea_completa"),store,data,5,true);
        ordinary_guides(assets.at("aceptar_registros_y_detectar_duplicados"),store,data,5,true);
        ordinary_guides(assets.at("actualizar_todo_o_conservar_el_estado_anterior"),store,data,5,true);
        ordinary_guides(assets.at("representar_presencia_y_cambiar_la_organizacion"),store,data,4,true);
    });
    else test("sixteen ordinary U12L14/L15 answers, exact guides and state reopen",[&] {
        ordinary_guides(assets.at("insertar_un_tipo_y_encadenar_la_salida"),store,data,7,false);
        ordinary_guides(assets.at("extraer_un_tipo_sin_cambiarlo_en_fallo"),store,data,9,true);
    });
    const auto achieved=data;
    for(std::size_t index=0;index<plans.size();++index)test(std::string("reset/")+plans[index].file+"/"+plans[index].editor,[&] {
        const auto& plan=plans[index];const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        require(compiler_puzzle_completed(achieved,asset.lesson,editor),"Missing actual U12 achievement before its isolated reset");
        auto reset=achieved,expected=achieved;
        const auto prefix="compiler-puzzle/v1/"+asset.lesson.locale+'/'+asset.lesson.id+'/'+exercise_key(asset.lesson,editor)+'/';
        if(auto domain=expected.preferences.find("native_windows");domain!=expected.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
        reset_compiler_puzzle(reset,asset.lesson,editor);
        require(reset==expected&&!compiler_puzzle_completed(reset,asset.lesson,editor)&&!snapshot(asset.lesson,reset).at(editor.stable_id),
            "Reset failed to restore the U12 prerequisite or changed later answers, drafts or other achievements");
        flow(asset,reset);
        state::StateStore reset_store(environment.paths.root/L"reset"/std::to_wstring(index));(void)reset_store.load();reset_store.save(reset);
        require(reopen_state(reset_store)==expected,"A reset U12 prerequisite or conserved state changed on reopen");
    });
    if(!loading_state)test("the new authored return-1 objective stays independent of these eighteen checks",[&] {
        const auto& asset=assets.at("compartir_flujos_sin_acoplar_el_calculo");
        const auto& intentional=at(asset.lesson,"block_0005_0002");
        require(has_compiler_objective(intentional)&&intentional.output_check&&intentional.output_check->cases.size()==1
            &&intentional.output_check->cases.front().exit_code==1
            &&intentional.output_check->cases.front().outputs==std::vector<std::string>{"Entrada invalida|9:4"}
            &&intentional.initial_code.find("return 1;")!=std::string::npos,
            "The integrated U12L13 objective lost its exact output or intentional process result");
        require(!compiler_puzzle_completed(achieved,asset.lesson,intentional)&&!snapshot(asset.lesson,achieved).at(intentional.stable_id)
            &&compiler_puzzle_case_count(achieved,asset.lesson,intentional)==0,
            "The eighteen independent batch checks manufactured credit for the new intentional-exit objective");
        for(const auto& [name,entry]:assets)require(state::sha256(read(entry.path))==entry.hash,
            "The headless U12 integration batch changed an active curricular asset");
        require(runs==23,"The focused U12 batch repeated or omitted a planned real compiler program");
    });
    else test("eight loading-state objectives and their captures stay exact and independent",[&] {
        require(runs==20&&captured_loading_frames.size()==20&&captured_run_directories.size()==20,
            "The loading-state batch repeated or omitted one of its eight starters, eight complete or four partial programs");
        const auto& asset=assets.at("clasificar_caracteres_y_normalizar_una_copia");
        const auto& first=at(asset.lesson,"block_0005_0004");
        require(!compiler_puzzle_completed(achieved,asset.lesson,first)&&compiler_puzzle_case_count(achieved,asset.lesson,first)==0,
            "The loading-state batch manufactured an execution receipt for the excluded first CRT objective");
        for(const auto& [name,entry]:assets)require(state::sha256(read(entry.path))==entry.hash,
            "The headless loading-state batch changed an active curricular asset");
    });
    std::cout<<"Headless U12 compiler batch: "<<passed<<" groups passed, "<<failed<<" failed; "<<runs
        <<" actual compiler programs, no UI or audio.\n";
    return failed?1:0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL U12 compiler batch setup: "<<error.what()<<'\n';return 1;
} catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL U12 compiler batch Windows setup: "<<error.code().value<<'\n';return 1;
} catch(...) {
    std::cerr<<"FAIL U12 compiler batch setup: unexpected exception\n";return 1;
}
