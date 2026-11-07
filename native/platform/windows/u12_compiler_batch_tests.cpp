#include "core/compiler_flow.hpp"
#include "core/lesson_entry_policy.hpp"
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
constexpr std::array<Plan,12> final_compiler_plans{{
    {"etiquetas_y_alcance_de_los_identificadores","block_0003_0003","block_0005_0001","block_0004_0001"},
    {"leer_un_grupo_con_cierre_propio","block_0002_0003","block_0005_0001","block_0003_0001"},
    {"componer_grupos_sin_mezclar_sus_datos","block_0002_0004","block_0005_0001","block_0003_0001"},
    {"escribir_un_formato_que_pueda_leerse","block_0002_0004","block_0005_0001","block_0003_0001"},
    {"escribir_un_formato_que_pueda_leerse","block_0006_0002","block_0007_0001"},
    {"comprobar_ida_y_vuelta_sin_perder_datos","block_0002_0003","block_0005_0001","block_0003_0001"},
    {"comprobar_ida_y_vuelta_sin_perder_datos","block_0006_0002","block_0007_0001"},
    {"construir_un_programa_de_carga_y_guardado","block_0002_0003","block_0003_0005","block_0003_0001"},
    {"construir_un_programa_de_carga_y_guardado","block_0004_0008","block_0006_0001"},
    {"construir_un_programa_de_carga_y_guardado","block_0004_0012","block_0004_0014"},
    {"construir_un_programa_de_carga_y_guardado","block_0005_0007","block_0005_0009"},
    {"construir_un_programa_de_carga_y_guardado","block_0007_0007","block_0008_0006","block_0008_0002"}
}};
// Independent UTF-8 source hashes, in the plan order above: starter, complete,
// partial. Editorial prose may change; these 31 reviewed programs must not.
constexpr std::array<std::array<std::string_view,3>,12> final_source_hashes{{
    {"460b36d1fb4d21e44b302e6379fbbd675b2fbf4ed4fc4b18f6c5e684d5005182","92c99652a1470012dcc98afa38216cc985aac55610d9ce72df83a29ea8e3d549","b955af73b39a4e06e355a2aab7295f77397413cbe521d9d7ff611674c7670762"},
    {"b78f13612e8f0418b917a94927f141ebf773d0282d0e26bd017f246f6602986a","8ceb5791d9c16e20fcc6fd5268d538ed4c36c37e70af5c4187848fa4bcd6718a","3f4fa22f50b53774a5c0d72f2cf2bb218bb91039ffb46ce0d4e971eefd9d9ed2"},
    {"cbce515e3c82fe87568c63c2a550f18f852ac4d9fdb919d7c9709b18c81f722a","98568ffe2053151cb9b054b10a1196fda51c87b01de18a3b623d101e942982d8","38dc25987eb494fb0341a6319f82a84cf1d8ff59e72d28064437b4455901d298"},
    {"c2daf5d2adbabd44a6699ad028533d0dc9ad83b93d704255fc1de68fba7b0d8a","213b033721c497c29602452f75c9f642f05750fe48c28604b3e869eda0caabcb","8a2ff46ec86c18fe53076deead25fc2010a964eee5e3bd9c545cdf6d55b7e50b"},
    {"9a932ea330c67c88bba386c423ee3d2de4329925e453a40ba7d7cb2fac2c006f","e763f936c180d3dc321b2f345d2dc34a699c17e2686c7c619b8caa4943643940",""},
    {"d48f3d4a3623bcee2d3c19df56c1dce4652a1e0455c3ee566a9a68b8ed63f096","df768afdaefd594b93226c644771a495b870fb21980dbbef00bfc121c8102380","4b5a3fafe9d4385ff30c5eedfaf1267c6bfb1d8c786deefd5cdb01a89b734b5e"},
    {"db26afca66f61c5bac5f1690d1837f28de7f7bdc283b418cc238323aeeca50fd","63b0050bcf6a0780c0a67f3546fe5a6938f9e5133ba4906c34f0ceb5906b72ba",""},
    {"8ed7b904bf363f72a17b6b95706bb32df3f5b9a8a2089f0da185b0993398f385","82aae409dfd951cfb82366b2d8f759b0ef6bdeaaed4ae71fb4bdc29010873780","7fcdcf46922c8a6aebbfcfd900b60dec1cf924cddea6bc325037cab3f5f4c171"},
    {"f398a2f814b5c6fcc16c2280f18fe038adec5dc610aa29c5df5bb46562412188","9c59016f472e2e21bd20d4c063dbc245b661ff6fd8cf4c2876cfa9b4dff7ef2f",""},
    {"3faa2678fd497f129509ab0195a5a3a8e16fc3fb9cb2a9cb2f8c8dd7533c87ed","6f5dd0e916daa9e2dc4691343d54db1dfd75ecd862e0a20e78a17ad124a97ebf",""},
    {"9b481969cf1de55c4eb8bcfb1e9b7d40972435589f64ae64399adfcc7974cc2c","03975faa223c86c25969c72a25bfca420185f85c86e2bbebe8ea976bf2ea2414",""},
    {"4addcf758c7de8a36f8a546003c0266b6221e6885836238f3ac85b38cea91b0e","1b2ab03891aef9f8411df8ccab090b05690693ee0c6413a8ff7dff2d43d2dcc1","2b6067128b9f7d355917fff2231669b7d37abc85571b0ec0ed38b0f7fcdffec4"}
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
void final_compiler_flow(const Asset& asset,const state::StateData& data) {
    require(asset.lesson.reference_after.empty()&&asset.lesson.activity_after.empty()&&
        asset.metadata.reference_after.empty()&&asset.metadata.activity_after.empty(),
        "The final batch requires review for newly authored explicit dependencies");
    flow(asset,data);
    const auto proofs=snapshot(asset.lesson,data),catalog_proofs=snapshot(asset.metadata,data);
    std::vector<EntryRequirement> requirements,projected;
    for(std::size_t page=0;page<asset.lesson.pages.size();++page) {
        bool preceding_complete=true;
        for(const auto& block:asset.lesson.pages[page].blocks) {
            const auto position=stable_block_position(asset.metadata,block.stable_id);
            require(position&&position->page==page,"Final compiler metadata changed a current block's page owner");
            const auto& metadata=asset.metadata.pages[page].blocks[position->block];
            require(compiler_reference_available(asset.lesson,page,block,proofs,
                    [&](const Block& prior){return compiler_puzzle_completed(data,asset.lesson,prior);})==preceding_complete&&
                compiler_reference_available(asset.metadata,page,metadata,catalog_proofs,
                    [&](const Block& prior){return compiler_puzzle_completed(data,asset.metadata,prior);})==preceding_complete,
                "A final U12 block bypassed an unresolved preceding same-page compiler or lost catalog parity");
            if(block.type==BlockType::cpp_compiler) {
                const bool complete=compiler_puzzle_completed(data,asset.lesson,block);
                const bool catalog_complete=compiler_puzzle_completed(data,asset.metadata,metadata);
                require(complete==catalog_complete,"Final compiler completion differs between full lesson and metadata");
                requirements.push_back({page,complete?EntryCompletion::complete:EntryCompletion::incomplete});
                projected.push_back({page,catalog_complete?EntryCompletion::complete:EntryCompletion::incomplete});
                preceding_complete=preceding_complete&&complete;
            }
        }
    }
    // This isolates the production entry policy's compiler requirements. No
    // ordinary answers or page arrivals are synthesized to claim full entry.
    for(std::size_t requested=0;requested<asset.lesson.pages.size();++requested) {
        auto expected=requested;
        for(const auto& item:requirements)if(item.page<requested&&item.completion==EntryCompletion::incomplete)
            expected=std::min(expected,item.page);
        const auto actual=lesson_entry_decision(requested,asset.lesson.pages.size(),requirements);
        const auto catalog=lesson_entry_decision(requested,asset.metadata.pages.size(),projected);
        require(actual.page==expected&&catalog.page==expected&&!actual.pending_math&&!catalog.pending_math,
            "Final compiler-only page admission skipped an earlier unresolved compiler");
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
        {"escribir_un_formato_que_pueda_leerse",5},{"comprobar_ida_y_vuelta_sin_perder_datos",6},
        {"construir_un_programa_de_carga_y_guardado",1,true,{BlockType::multiple_choice,"block_0001_0002"}},
        {"construir_un_programa_de_carga_y_guardado",1,true,{BlockType::multiple_choice,"block_0004_0004"}},
        {"construir_un_programa_de_carga_y_guardado",1,true,{BlockType::multiple_choice,"block_0004_0005"}},
        {"construir_un_programa_de_carga_y_guardado",1,true,{BlockType::output_prediction,"block_0009_0001"}},
        {"construir_un_programa_de_carga_y_guardado",1,true,{BlockType::output_prediction,"block_0009_0002"}},
        {"construir_un_programa_de_carga_y_guardado",1,true,{BlockType::multiple_choice,"block_0009_0003"}}
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
    std::cout<<"PASS "<<(final?"17 ordinary U12L24-L26 activities with authored retry guides":
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
    const bool final_compilers=argc==2&&std::string_view(argv[1])=="--final-compiler-batch";
    const bool published_starters=loading_state||final_compilers;
    require(argc==1||loading_state||guides_only||late_guides||final_guides||final_compilers,"Usage: u12_compiler_batch_tests [--loading-state|--ordinary-guides|--late-ordinary-guides|--final-ordinary-guides|--final-compiler-batch]");
    if(guides_only||late_guides||final_guides)return ordinary_guides_only(final_guides?OrdinaryBatch::final:
        (late_guides?OrdinaryBatch::late:OrdinaryBatch::formatting));
    const std::span<const Plan> plans=final_compilers?std::span<const Plan>(final_compiler_plans):
        (loading_state?std::span<const Plan>(loading_state_plans):std::span<const Plan>(formatting_plans));
    const LaunchOptions options{ProfileId::Codex,(final_compilers?L"u12-final-compiler-batch-":(loading_state?L"u12-loading-batch-":L"u12-batch-"))+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    const auto source_root=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo";
    std::map<std::string,Asset> assets;
    state::StateStore store(environment.paths.root);auto data=store.load();require(data.preferences.empty(),"The private U12 profile contains old state");
    data.preferences["u12-batch-unrelated"]["opaque"]=std::string(" Preserve\t\r\n保存 ");
    narration::History storage_pauses;
    if(final_compilers){
        for(unsigned index=0;index<3;++index)storage_pauses=narration::begin(std::move(storage_pauses),
            "qa-final-storage-pause-"+std::to_string(index),"qa-final-storage-owner-"+std::to_string(index),
            {0,static_cast<std::int64_t>(117+index),0,false},"storage-only source; no listening evidence",
            "qa-final-storage-page-"+std::to_string(index));
        narration::write_history(data,storage_pauses);
    }
    for(const auto& plan:plans)if(!assets.contains(plan.file)) {
        Asset asset;asset.path=source_root/(std::string("lesson_")+plan.file+".json");
        const auto bytes=read(asset.path);asset.hash=state::sha256(bytes);asset.lesson=parse_lesson_json(bytes);
        asset.metadata=parse_lesson_progress_metadata(bytes);state::prepare_lesson_state(data,asset.lesson);
        if(!final_compilers)state::save_current_page(data,asset.lesson,asset.lesson.pages.size()-1);
        assets.emplace(plan.file,std::move(asset));
    }
    require(assets.size()==(final_compilers?6u:(loading_state?5u:11u)),"The selected U12 compiler batch changed its exact lesson inventory");
    for(std::size_t index=0;index<plans.size();++index) {
        const auto& plan=plans[index];
        const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        require(editor.output_check&&editor.output_check->cases.size()==1&&editor.output_check->cases.front().standard_input.empty(),
            "A reviewed U12 editor changed from its single aggregate empty-input objective");
        if(published_starters)require(editor.project_files.empty()&&editor.source_files.empty()&&!editor.cmake&&
                !editor.output_check->cases.front().executable&&editor.output_check->cases.front().exit_code==0,
            "A published-starter capture requires its exact current single-source, empty-input, default-target process frame");
        if(final_compilers){
            require(asset.lesson.id==std::string("cpp_fundamentals__archivos_y_programa_completo__")+plan.file&&
                state::sha256(editor.initial_code)==final_source_hashes[index][0]&&
                state::sha256(at(asset.lesson,plan.complete).code)==final_source_hashes[index][1]&&
                (plan.partial?state::sha256(at(asset.lesson,plan.partial).code)==final_source_hashes[index][2]:final_source_hashes[index][2].empty()),
                "An audited final U12 owner or exact starter/complete/partial source changed before execution");
        }
        state::Draft draft;draft.source=editor.initial_code+(published_starters?"":"\n// isolated personal QA draft\n");draft.input="";
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
        if(published_starters)require(captured_loading_frames.insert(compiler_program_frame(capture.request)).second,
            "The selected published-starter batch repeated a source/project frame");
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
    // Preserve the old formatting batch's controlled empty probe. The other
    // batches instead capture their distinct, unchanged published starters.
    std::optional<Capture> empty;
    if(!published_starters){empty=run("int main(){return 0;}\n");require(empty->execution.standard_output.empty(),"The empty U12 probe wrote output");}
    const auto check_flow=[&](const Asset& asset,const state::StateData& value){
        if(final_compilers)final_compiler_flow(asset,value);else flow(asset,value);
    };
    for(const auto& plan:plans)test(std::string(plan.file)+"/"+plan.editor,[&] {
        const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        const auto initial=data;check_flow(asset,data);
        auto protected_initial=initial;
        const auto current_draft_key=draft_key("programming",asset.lesson,editor);
        if(final_compilers)if(auto domain=protected_initial.preferences.find("cpp_lesson_drafts");domain!=protected_initial.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(current_draft_key+'/');});
        const auto edit_to_capture=[&](const Capture& capture){
            if(!final_compilers)return;
            auto draft=state::get_draft(data,current_draft_key,editor.initial_code,editor.standard_input.value_or(""));
            draft.source=capture.request.source_utf8;draft.input=capture.request.stdin_utf8;
            draft.indentation_json=state::make_indentation_metadata(draft.source,{});
            state::put_draft(data,current_draft_key,draft);
            invalidate_compiler_puzzle_attempt(data,asset.lesson,editor,capture.request);
            preserve_values(protected_initial,data);
        };
        const auto restored_draft_matches=[&](const state::StateData& restored,const Capture& capture){
            if(!final_compilers)return;
            const auto draft=state::get_draft(restored,current_draft_key,editor.initial_code,editor.standard_input.value_or(""));
            require(draft.has_saved&&draft.source==capture.request.source_utf8&&draft.input==capture.request.stdin_utf8&&
                draft.selection_start==2&&draft.selection_end==7&&
                draft.indentation_json==state::make_indentation_metadata(draft.source,{}),
                "A restored final U12 review was queried against a different source/input draft");
        };
        require(!compiler_puzzle_completed(data,asset.lesson,editor),"An unexecuted U12 editor inherited another goal's achievement");
        // Published-starter batches use distinct exact source/program frames;
        // none shares another program's execution capture.
        const auto starter=published_starters?run(editor.initial_code):*empty;
        require(evaluate_cpp_output_check(*editor.output_check,starter.execution,starter.request.revision).verdict==OutputVerdict::incorrect
            &&!record_compiler_puzzle_case(data,asset.lesson,editor,starter.request,starter.execution,"Incorrecto\nPublished starter output.")&&data==initial,
            "An empty successful program completed a nonempty U12 aggregate objective");
        const auto reject=[&](const Capture& capture) {
            edit_to_capture(capture);
            const auto evaluation=evaluate_cpp_output_check(*editor.output_check,capture.execution,capture.request.revision);
            require(evaluation.verdict==OutputVerdict::incorrect,
                "An incomplete U12 reference satisfied the complete aggregate output");
            const auto text=final_compilers?"Incorrecto"+(evaluation.feedback.empty()?std::string{}:"\n"+evaluation.feedback):
                std::string("Incorrecto\nQA: incomplete aggregate output.");
            require(record_compiler_puzzle_review(data,asset.lesson,editor,capture.request,capture.execution,text),
                "An actual U12 rejection could not retain its review");
            preserve_values(protected_initial,data);store.save(data);const auto restored=reopen_state(store);
            restored_draft_matches(restored,capture);
            const auto feedback=compiler_puzzle_current_feedback(restored,asset.lesson,editor,capture.request);
            require(restored==data,"Reopening changed the exact saved U12 rejection state");
            require(feedback&&!feedback->correct&&!compiler_puzzle_completed(restored,asset.lesson,editor),
                "Reopening lost an actual U12 rejection or invented completion");
            if(final_compilers)require(feedback->text==text&&feedback->source_path==evaluation.feedback_path,
                "A real final U12 rejection restored another authored guide or semantic source path");
            check_flow(asset,restored);
        };
        reject(starter);
        const auto require_reference=[&](const Block& reference) {
            if(final_compilers){
                const auto owner=stable_block_position(asset.lesson,plan.editor),position=stable_block_position(asset.lesson,reference.stable_id);
                require(reference.type==BlockType::code&&owner&&position&&
                    (position->page>owner->page||(position->page==owner->page&&position->block>owner->block))&&
                    !reference_activity(asset.lesson,reference),
                    "A closed final U12 program lost its exact later published code position");
                return;
            }
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
        edit_to_capture(capture);
        const auto owned="compiler-puzzle/v1/"+asset.lesson.locale+'/'+asset.lesson.id+'/'+exercise_key(asset.lesson,editor)+'/';
        // Only this editor's temporary rejection and explicitly edited QA
        // draft may change; all other drafts and receipts remain byte exact.
        if(auto domain=protected_initial.preferences.find("native_windows");domain!=protected_initial.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(owned+state::sha256(reference_objective_frame(asset.lesson,editor))+"/review/");});
        require(record_compiler_puzzle_case(data,asset.lesson,editor,capture.request,capture.execution,"Correcto\n"+verdict.feedback)
            &&compiler_puzzle_case_count(data,asset.lesson,editor)==1&&compiler_puzzle_completed(data,asset.lesson,editor),
            "A fresh complete U12 program did not earn its own aggregate achievement");
        preserve_values(protected_initial,data);require(compiler_puzzle_review(data,asset.lesson,editor,capture.request).empty(),
            "An actual complete U12 program retained obsolete rejected feedback");
        store.save(data);const auto reopened=reopen_state(store);
        require(reopened==data,"A real U12 achievement changed on reopen");
        restored_draft_matches(reopened,capture);
        if(final_compilers){
            const auto feedback=compiler_puzzle_current_feedback(reopened,asset.lesson,editor,capture.request);
            require(feedback&&feedback->correct&&feedback->text=="Correcto"+(verdict.feedback.empty()?std::string{}:"\n"+verdict.feedback)&&
                feedback->source_path==verdict.feedback_path,
                "The exact final U12 program lost its newly earned correct explanation on reopen");
        }
        check_flow(asset,data);
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
        if(final_compilers){
            const std::string_view file=plan.file,identity=plan.editor;
            const auto& directory=capture.result.run_directory;
            if(file=="escribir_un_formato_que_pueda_leerse"&&identity=="block_0006_0002")
                require(read(directory/L"conjunto_compuesto_lgp.txt")=="{ conjunto { grupo rojo 7[0;0] 2[3;1] } { grupo azul } }",
                    "The final L24 program changed the exact canonical file or appended a newline");
            if(file=="construir_un_programa_de_carga_y_guardado"){
                if(identity=="block_0002_0003")require(
                    read(directory/L"inspecciones_entrada.txt")=="{ conjunto { grupo rojo 0[1;0] }"&&
                    read(directory/L"inspecciones_salida.txt")=="{ conjunto { grupo rojo 7[1;0] } { grupo azul 7[2;1] } }",
                    "The first L26 battery overwrote the last valid output after its final invalid input");
                if(identity=="block_0004_0008")require(read(directory/L"ensayo_escritura.txt").empty()&&
                    read(directory/L"ensayo_cierre.txt")=="PRUEBA",
                    "The two real L26 failure demonstrations changed the write/close artifacts");
                if(identity=="block_0004_0012")require(read(directory/L"inspecciones_salida.txt")=="MARCA",
                    "The L26 preservation demonstration replaced its exact preexisting mark");
                if(identity=="block_0007_0007"){
                    const std::string maximum="{ conjunto { grupo rojo 0[10;0] 1[10;0] 2[10;0] 3[10;0] 4[10;0] 5[10;0] 6[10;0] 7[10;0] 8[10;0] 9[10;0] } }";
                    require(read(directory/L"inspecciones_entrada.txt")==maximum&&read(directory/L"inspecciones_salida.txt")==maximum,
                        "The final L26 maximum case did not retain its exact equal input/output files");
                }
            }
        }
    });
    if(loading_state)test("nineteen ordinary U12L16-L19 answers, exact guides and state reopen",[&] {
        ordinary_guides(assets.at("leer_y_validar_una_linea_completa"),store,data,5,true);
        ordinary_guides(assets.at("aceptar_registros_y_detectar_duplicados"),store,data,5,true);
        ordinary_guides(assets.at("actualizar_todo_o_conservar_el_estado_anterior"),store,data,5,true);
        ordinary_guides(assets.at("representar_presencia_y_cambiar_la_organizacion"),store,data,4,true);
    });
    else if(!final_compilers)test("sixteen ordinary U12L14/L15 answers, exact guides and state reopen",[&] {
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
        check_flow(asset,reset);
        state::StateStore reset_store(environment.paths.root/L"reset"/std::to_wstring(index));(void)reset_store.load();reset_store.save(reset);
        require(reopen_state(reset_store)==expected,"A reset U12 prerequisite or conserved state changed on reopen");
    });
    if(!published_starters)test("the new authored return-1 objective stays independent of these eighteen checks",[&] {
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
    else if(loading_state)test("eight loading-state objectives and their captures stay exact and independent",[&] {
        require(runs==20&&captured_loading_frames.size()==20&&captured_run_directories.size()==20,
            "The loading-state batch repeated or omitted one of its eight starters, eight complete or four partial programs");
        const auto& asset=assets.at("clasificar_caracteres_y_normalizar_una_copia");
        const auto& first=at(asset.lesson,"block_0005_0004");
        require(!compiler_puzzle_completed(achieved,asset.lesson,first)&&compiler_puzzle_case_count(achieved,asset.lesson,first)==0,
            "The loading-state batch manufactured an execution receipt for the excluded first CRT objective");
        for(const auto& [name,entry]:assets)require(state::sha256(read(entry.path))==entry.hash,
            "The headless loading-state batch changed an active curricular asset");
    });
    else test("twelve final compiler goals retain exact independent execution and excluded objectives",[&] {
        require(runs==31&&captured_loading_frames.size()==31&&captured_run_directories.size()==31,
            "The final compiler batch omitted or repeated a published starter, complete or partial source");
        std::size_t selected=0,excluded=0;
        for(const auto& [name,asset]:assets){
            require(state::sha256(read(asset.path))==asset.hash,"The final compiler batch changed an active curricular asset");
            for(const auto& page:asset.lesson.pages)for(const auto& editor:page.blocks){
                require(!ordinary_selected(editor)||!state::get_response(data,asset.lesson,editor),
                    "Compiler-only QA manufactured an ordinary curricular answer");
                if(editor.type!=BlockType::cpp_compiler)continue;
                const bool planned=std::ranges::any_of(plans,[&](const auto& plan){return name==plan.file&&editor.stable_id==plan.editor;});
                if(planned){
                    require(compiler_puzzle_completed(achieved,asset.lesson,editor)&&compiler_puzzle_case_count(achieved,asset.lesson,editor)==1,
                        "A final U12 program did not retain its own actual aggregate execution proof");++selected;
                }else{
                    require(name=="construir_un_programa_de_carga_y_guardado"&&editor.stable_id=="block_0004_0017"&&
                        editor.output_check&&editor.output_check->cases.size()==1&&editor.output_check->cases.front().exit_code==1&&
                        !compiler_puzzle_completed(achieved,asset.lesson,editor)&&compiler_puzzle_case_count(achieved,asset.lesson,editor)==0,
                        "The final batch omitted another goal or fabricated an excluded intentional-exit achievement");++excluded;
                }
            }
            final_compiler_flow(asset,achieved);
        }
        require(selected==12&&excluded==1&&narration::read_history(data)==storage_pauses&&reopen_state(store)==data,
            "The final batch changed its exact compiler inventory, independent state or three storage-only pauses");
        std::cout<<"PASS U12L21-L26: 12 goals, 12 rejected starters, 7 rejected partials, 12 complete references; "
            "31 distinct real executions and fresh run directories. Compiler-only page/same-page barriers, "
            "exact current drafts/guides on reopen and scoped resets; no ordinary answers, visits, listening or excluded return-1 credit.\n";
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
