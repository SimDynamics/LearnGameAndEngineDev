#include "core/attempts.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
std::string active_case,active_step;

void check(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND lesson_control(HWND parent,int id){struct Search{int id;HWND found=nullptr;std::size_t matches=0;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& current=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=current.id)return TRUE;
        // Match LessonView's direct pane children, including overflow and
        // fullscreen hosts. Nested puzzle cards reuse these numeric IDs.
        wchar_t owner_class[96]{};GetClassNameW(GetParent(item),owner_class,96);
        if(std::wstring_view(owner_class)!=L"LearnGameAndEngineDev.LessonPane")return TRUE;
        current.found=item;++current.matches;return TRUE;},reinterpret_cast<LPARAM>(&search));
    check(search.matches<=1,"Ambiguous native lesson control: id="+std::to_string(id)+
        ", LessonPane matches="+std::to_string(search.matches));
    return search.found;
}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();}
std::string native_newlines(std::string_view value){std::string result;
    for(std::size_t index=0;index<value.size();++index){
        if(value[index]=='\r'){result+='\n';if(index+1<value.size()&&value[index+1]=='\n')++index;}
        else result+=value[index];
    }
    return result;
}
std::string crlf_lines(std::string_view value){
    check(value.find('\r')==value.npos,"The LF fixture unexpectedly contains a carriage return");
    std::string result;
    for(const auto character:value){if(character=='\n')result+='\r';result+=character;}
    return result;
}
state::Preferences attempt_preferences(const state::StateData& data){
    const auto found=data.preferences.find("attempt_reserve_seconds_test");
    return found==data.preferences.end()?state::Preferences{}:found->second;
}

enum class PredictionGroup { baseline, prepared_input };
struct Plan {
    const char* filename;
    const char* identity;
    const char* activity;
    bool exact;
    const char* internal_error;
    bool editorial_feedback = false;
    PredictionGroup group = PredictionGroup::baseline;
    const char* edition_identity = nullptr;
    const char* edition_answer = nullptr;
};
const std::array plans{
    Plan{"programas_y_ejecucion/lesson_primer_programa_y_salida.json","block_0001_0003","literal_independiente",true,nullptr},
    Plan{"repeticion_y_terminacion/lesson_recorridos_anidados.json","block_0009_0002","u4_anidados_revisadas_conserva_otro_equipo",true,nullptr},
    Plan{"repeticion_y_terminacion/lesson_recorridos_anidados.json","block_0011_0005","u4_anidados_paridad_equipo_sin_revision",true,nullptr},
    Plan{"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0011_0002",
        "cpp_fundamentals__datos_propios_y_estado_valido__transferir_invariantes_a_un_deposito__alias_rechazo_inicial_y_vista",true,nullptr},
    Plan{"datos_propios_y_estado_valido/lesson_transferir_invariantes_a_un_deposito.json","block_0014_0002",
        "cpp_fundamentals__datos_propios_y_estado_valido__transferir_invariantes_a_un_deposito__alias_copia_vista_y_otra_via",true,nullptr},
    Plan{"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json","block_0001_0004","dos_lineas",false,"Entrada Salida"},
    Plan{"archivos_y_programa_completo/lesson_ancho_relleno_y_alineacion_de_campos.json","block_0001_0001","",false,"[ 8]|[9]"},
    Plan{"programas_y_ejecucion/lesson_primer_programa_y_salida.json","block_0004_0003",
        "prediccion_zona_inicial",true,"Zonainicial"},
    Plan{"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json","block_0006_0003",
        "prediccion_informe_inicial",true,"Resumen Zona Oeste"},
    Plan{"programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json","block_0003_0005",
        "prediccion_revision_inicial",true,"Revisión inicial"},
    Plan{"objetos_y_estado/lesson_enteros_y_aproximaciones.json","block_0007_0005",
        "cpp_fundamentals__objetos_y_estado__enteros_y_aproximaciones__prediccion_reparto_entero",true,"Parte:4 m"},
    Plan{"texto_interpretacion_y_flujos/lesson_lineas_y_separadores_pendientes.json","block_0002_0001",
        "",false,"3",true},
    Plan{"texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json","block_0006_0002",
        "",false,"Error: Se esperaba ;",true},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0006_0004",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__etapas_lectura_y_actualizacion",true,"Rita 4\nRita: 4",true,PredictionGroup::prepared_input,
        "block_0006_0002","Rita 4\nRita: 7"},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0010_0005",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__registrar_tres_datos_y_total",true,"Iris 4 2 6",true,PredictionGroup::prepared_input,
        "block_0010_0001","Iris 4 2 22"},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0012_0002",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__distinguir_regla_cuatro",true,"Nora: 11",true,PredictionGroup::prepared_input,
        "block_0012_0002","Nora: 9"},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0012_0003",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__cero_paquetes_regla_cuatro",true,"Vega: 0",true,PredictionGroup::prepared_input,
        "block_0012_0003","Vega: 3"},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0012_0004",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__maximo_regla_cuatro",true,"Orion: 103",true,PredictionGroup::prepared_input,
        "block_0012_0004","Orion: 83"},
};

struct MultilinePlan {
    const char* filename;
    const char* lesson_id;
    const char* identity;
    const char* activity;
    const char* answer;
    const char* wrong_internal_space;
    std::size_t earlier_compilers;
    bool completes_page;
    bool editorial_feedback;
    const char* next_page_compiler=nullptr;
};
constexpr std::array multiline_plans{
    MultilinePlan{"edition_es_1__programas_y_ejecucion/lesson_texto_secuencia_y_formato.json",
        "cpp_fundamentals__edition_es_1__programas_y_ejecucion__texto_secuencia_y_formato",
        "block_0002_0013","p1u1l3_linea_vacia","Rojo\n\nVerde","Rojo\n \nVerde",0,true,false},
    MultilinePlan{"edition_es_1__programas_y_ejecucion/lesson_texto_secuencia_y_formato.json",
        "cpp_fundamentals__edition_es_1__programas_y_ejecucion__texto_secuencia_y_formato",
        "block_0005_0007","prediccion_informe_inicial","Resumen\nZona Oeste","Resumen\nZona  Oeste",0,true,true,"block_0006_0005"},
    MultilinePlan{"edition_es_1__objetos_y_estado/lesson_leer_una_entrada_conocida.json",
        "cpp_fundamentals__edition_es_1__objetos_y_estado__leer_una_entrada_conocida",
        "block_0006_0002","cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__etapas_lectura_y_actualizacion",
        "Rita 4\nRita: 7","Rita 4\nRita:7",0,false,true},
    MultilinePlan{"edition_es_1__objetos_y_estado/lesson_representar_procesar_y_recuperar.json",
        "cpp_fundamentals__edition_es_1__objetos_y_estado__representar_procesar_y_recuperar",
        "block_0008_0000","cpp_fundamentals__objetos_y_estado__representar_procesar_y_recuperar__recuperar_registro_con_otra_devolucion",
        "Modulo 02\nInicio: 4\nAhora: 9","Modulo 02\nInicio:4\nAhora: 9",2,false,true},
};

struct U2L9Publication {
    const char* label;
    const char* filename;
    const char* lesson_id;
    const char* laboratory;
    const char* reference;
    const char* boxes;
    const char* boxes_reference;
    bool active_edition;
};
constexpr U2L9Publication archived_u2l9_publication{
    "archived publication", "objetos_y_estado/lesson_leer_una_entrada_conocida.json",
    "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida",
    "block_0009_0003", "block_0010_0001", "p1u2l9_reparar_lectura_de_cajas", "block_0007_0001", false};
constexpr U2L9Publication active_u2l9_publication{
    "active edition", "edition_es_1__objetos_y_estado/lesson_leer_una_entrada_conocida.json",
    "cpp_fundamentals__edition_es_1__objetos_y_estado__leer_una_entrada_conocida",
    "block_0009_0004", "block_0009_0006", "block_0006_0004", "block_0006_0006", true};

const U2L9Publication& u2l9_publication(const curriculum::Lesson& lesson){
    const auto& publication=lesson.id==active_u2l9_publication.lesson_id
        ?active_u2l9_publication:archived_u2l9_publication;
    check(lesson.id==publication.lesson_id&&lesson.locale=="es"&&lesson.compiler_puzzles_required,
        "U2L9 QA loaded another publication or changed its mandatory compiler policy");
    if(publication.active_edition){
        check(lesson.course_edition&&*lesson.course_edition==
            curriculum::CourseEdition{"cpp_fundamentals","es","cpp_desde_cero_2026",1},
            "U2L9 QA did not load the authored active Spanish course edition");
    }else check(!lesson.course_edition,"The archived U2L9 publication unexpectedly became a new edition");
    return publication;
}

void require_active_u2l9_reference_layout(const curriculum::Lesson& lesson,const U2L9Publication& publication){
    if(!publication.active_edition)return;
    const auto target=curriculum::stable_block_position(lesson,publication.laboratory);
    const auto reference=curriculum::stable_block_position(lesson,publication.reference);
    check(target&&reference&&target->page==reference->page&&target->block<reference->block&&
        curriculum::reference_activity(lesson,lesson.pages.at(reference->page).blocks.at(reference->block))==target,
        "The active U2L9 reference must follow its own editor on the same page");
    for(std::size_t index=target->block+1;index<reference->block;++index){
        const auto type=lesson.pages.at(target->page).blocks.at(index).type;
        check(type==curriculum::BlockType::heading||type==curriculum::BlockType::paragraph||
            type==curriculum::BlockType::note||type==curriculum::BlockType::code||type==curriculum::BlockType::image,
            "An intervening exercise separated the active U2L9 editor from its reference");
    }
}

struct ArchivedMathPlan {
    const char* lesson;
    const char* identity;
    const char* response;
    bool matching;
    const char* expected;
};
constexpr const char* archived_deposit_lesson=
    "cpp_fundamentals__datos_propios_y_estado_valido__transferir_invariantes_a_un_deposito";
constexpr const char* archived_diagnostic_lesson=
    "cpp_fundamentals__programas_y_ejecucion__fuente_compilacion_y_diagnostico";
constexpr std::array archived_math_plans{
    ArchivedMathPlan{archived_deposit_lesson,"block_0000_0004","estados",false,
        R"({"agregar_tres":"1","tras_tres":"5","agregar_una":"0","tras_una":"5","retirar_cinco":"1","tras_cinco":"0","retirar_una":"0","tras_retirar_una":"0"})"},
    ArchivedMathPlan{archived_deposit_lesson,"block_0005_0004","estados",false,
        R"({"primero":"5","segundo":"3","tercero":"3","cuarto":"6","quinto":"6"})"},
    ArchivedMathPlan{archived_deposit_lesson,"block_0009_0005","estados",false,
        R"({"d_retiro":"1","d_ingreso":"0","d_a":"1","d_b":"3","d_foto":"3","d_vista":"1","m_retiro":"1","m_ingreso":"1","m_a":"5","m_b":"5","m_foto":"3","m_vista":"5"})"},
    ArchivedMathPlan{archived_deposit_lesson,"block_0012_0003","estados",false,
        R"({"a1":"1","b1":"3","a2":"2","b2":"3","a3":"0","b3":"5","a4":"1","b4":"5","a5":"1","b5":"5","retiros":"2","ingresos":"3","foto":"3","vista":"1"})"},
    ArchivedMathPlan{archived_diagnostic_lesson,"p1u1l4_p0_relacionar_herramientas","relaciones",true,
        R"({"editar":"editor","traducir":"compilador","reunir":"enlazador","ejecutar":"ejecucion"})"},
    ArchivedMathPlan{archived_diagnostic_lesson,"p1u1l4_p4_elegir_siguiente_accion","relaciones",true,
        R"({"salida_distinta":"comprobar_comportamiento","solo_fuente":"preparar_ejecucion","construccion_rechazada":"revisar_diagnostico"})"},
};
struct ArchivedMathPrerequisite {
    const ArchivedMathPlan* plan=nullptr;
    std::string source,fingerprint,responses;
};

ArchivedMathPrerequisite archived_math_prerequisite(const curriculum::Lesson& lesson,const curriculum::Block& block){
    using namespace winrt::Windows::Data::Json;
    const auto plan=std::ranges::find_if(archived_math_plans,[&](const auto& item){
        return lesson.id==item.lesson&&block.stable_id==item.identity;
    });
    check(!lesson.course_edition&&lesson.locale=="es"&&block.type==curriculum::BlockType::math_puzzle&&
        plan!=archived_math_plans.end(),"An unaudited mathematical prerequisite entered archived prediction QA: "+block.stable_id);
    ArchivedMathPrerequisite result;result.plan=&*plan;result.source=curriculum::math_evaluation_source(block);
    result.fingerprint=state::sha256(result.source);
    const auto original=JsonObject::Parse(wide(result.source));const auto declarations=original.GetNamedArray(L"responses");
    check(original.GetNamedNumber(L"schemaVersion")==1&&declarations.Size()==1,
        "The archived mathematical prerequisite changed its audited schema: "+block.stable_id);
    const auto specification=declarations.GetObjectAt(0),validator=specification.GetNamedObject(L"validator");
    const auto expected=JsonObject::Parse(wide(plan->expected));
    check(specification.GetNamedString(L"id")==wide(plan->response)&&
        specification.GetNamedString(L"type")== (plan->matching?L"math_matching_drag":L"math_multi_input")&&
        validator.GetNamedString(L"type")== (plan->matching?L"matching":L"fields")&&validator.Size()==2,
        "The archived mathematical prerequisite changed its response or validator type: "+block.stable_id);
    const auto authored=validator.GetNamedObject(plan->matching?L"expected":L"fields");
    check(authored.Size()==expected.Size(),"The archived mathematical oracle acquired another pair/field: "+block.stable_id);
    for(const auto& value:expected){
        check(authored.HasKey(value.Key()),"An archived mathematical oracle lost a registered pair/field: "+block.stable_id);
        if(plan->matching)check(authored.GetNamedString(value.Key())==value.Value().GetString(),
            "The archived matching oracle changed: "+block.stable_id);
        else{
            const auto field=authored.GetNamedObject(value.Key());
            check(field.Size()==2&&field.GetNamedString(L"type")==L"numeric"&&
                field.GetNamedString(L"expected")==value.Value().GetString(),
                "The archived numeric oracle changed: "+block.stable_id);
        }
    }
    const auto definitions=specification.GetNamedArray(plan->matching?L"sources":L"fields");
    check(definitions.Size()==expected.Size(),"The archived mathematical control changed its fields: "+block.stable_id);
    for(const auto& value:expected){
        std::size_t matches=0;
        for(const auto& item:definitions){const auto definition=item.GetObject();
            if(definition.GetNamedString(L"id")==value.Key()&&
                (plan->matching||definition.GetNamedString(L"type")==L"numeric"))++matches;
        }
        check(matches==1,"The archived mathematical control lost a unique field identity: "+block.stable_id);
    }
    if(plan->matching){
        const auto targets=specification.GetNamedArray(L"targets");
        check(targets.Size()==expected.Size(),"The archived matching control changed its destinations");
        for(const auto& value:expected){
            std::size_t matches=0;
            for(const auto& item:targets)if(item.GetObject().GetNamedString(L"id")==value.Value().GetString())++matches;
            check(matches==1,"The archived matching control lost a unique destination identity");
        }
    }
    JsonObject answer,responses;answer.Insert(L"type",JsonValue::CreateStringValue(plan->matching?L"matching":L"fields"));
    answer.Insert(plan->matching?L"pairs":L"values",expected);responses.Insert(wide(plan->response),answer);
    result.responses=winrt::to_string(responses.Stringify());return result;
}

// Restricted preparation of earlier archived fixtures, not a mathematics
// worker test. Only these six independently pinned maps and their original
// evaluation sources are admitted; no other type or answer receives credit.
winrt::Windows::Data::Json::JsonObject archived_math_reply(const ArchivedMathPrerequisite& prerequisite,
        std::string_view original,std::string_view answers){
    using namespace winrt::Windows::Data::Json;
    const auto& plan=*prerequisite.plan;
    check(original==prerequisite.source&&state::sha256(original)==prerequisite.fingerprint,
        "Archived QA mathematical validation changed its exact source/fingerprint: "+std::string(plan.identity));
    const auto submitted=JsonObject::Parse(wide(answers));const auto identifier=wide(plan.response);
    const auto expected=JsonObject::Parse(wide(plan.expected));
    bool correct=submitted.Size()==1&&submitted.HasKey(identifier);
    if(correct){
        const auto candidate=submitted.GetNamedObject(identifier);
        const auto member=plan.matching?L"pairs":L"values";
        correct=candidate.Size()==2&&candidate.GetNamedString(L"type",L"")== (plan.matching?L"matching":L"fields")&&
            candidate.HasKey(member);
        if(correct){
            const auto values=candidate.GetNamedObject(member);correct=values.Size()==expected.Size();
            for(const auto& value:expected)if(!values.HasKey(value.Key())||
                values.GetNamedString(value.Key(),L"")!=value.Value().GetString())correct=false;
        }
    }
    JsonObject result;result.Insert(L"evaluable",JsonValue::CreateBooleanValue(true));
    result.Insert(L"correct",JsonValue::CreateBooleanValue(correct));
    result.Insert(L"contentFingerprint",JsonValue::CreateStringValue(wide(prerequisite.fingerprint)));
    result.Insert(L"feedback",JsonValue::CreateStringValue(correct?L"Prerrequisito de fixture verificado.":L"Prerrequisito de fixture incorrecto."));
    return result;
}

LessonViewHooks archived_math_hooks(const std::vector<ArchivedMathPrerequisite>& prerequisites,
        std::shared_ptr<std::atomic<std::size_t>> verified){
    LessonViewHooks hooks;
    hooks.validate_math=[prerequisites,verified](std::string_view original,std::string_view answers,
            std::string_view language,std::stop_token stop){
        windows::ensure_winrt_apartment();
        check(language=="es"&&!stop.stop_requested(),"Archived QA prerequisite validation changed locale or was cancelled");
        const auto found=std::ranges::find_if(prerequisites,[&](const auto& item){return item.source==original;});
        check(found!=prerequisites.end(),"Archived QA refuses an unregistered mathematical validation source");
        const auto reply=archived_math_reply(*found,original,answers);
        if(reply.GetNamedBoolean(L"correct"))verified->fetch_or(std::size_t{1}<<static_cast<std::size_t>(found-prerequisites.begin()));
        return winrt::to_string(reply.Stringify());
    };
    return hooks;
}

// Only earlier-page quizzes are prepared from their authored answers to
// isolate this UI scenario. This is QA setup, not evidence of quiz interaction.
// Each scenario prepares compiler evidence separately. The optional archived
// math maps belong only to the legacy baseline, never the active-newline mode.
void prepare_u2l9_prior_quizzes(state::StateData& data,const curriculum::Lesson& lesson,
        curriculum::StatePosition target,std::vector<ArchivedMathPrerequisite>* archived_math=nullptr) {
    using curriculum::BlockType;
    using namespace winrt::Windows::Data::Json;
    for(std::size_t p=0;p<target.page;++p) {
        for(const auto& block:lesson.pages[p].blocks) {
            state::Response response;
            if(block.type==BlockType::multiple_choice) {
                check(block.correct_index.has_value(),"An earlier QA choice has no authored answer");
                response.selected=static_cast<std::int64_t>(*block.correct_index);
                response.correct=curriculum::evaluate_choice(block,*block.correct_index).correct;
            } else if(block.type==BlockType::fill_code||block.type==BlockType::output_prediction) {
                check(!block.answers.empty(),"An earlier QA answer is missing");
                response.answer=block.answers.front();
                response.correct=curriculum::evaluate_text_answer(block,response.answer).correct;
            } else if(block.type==BlockType::inline_fill_code) {
                JsonArray answers;
                for(const auto& answer:block.answers)answers.Append(JsonValue::CreateStringValue(wide(answer)));
                JsonObject raw;raw.Insert(L"answers",answers);
                response.raw_json=winrt::to_string(raw.Stringify());
                response.correct=curriculum::evaluate_hole_answers(block,block.answers).correct;
            } else if(block.type==BlockType::drag_fill_code) {
                JsonArray slots;
                for(const auto slot:block.correct_order)slots.Append(JsonValue::CreateNumberValue(static_cast<double>(slot)));
                JsonObject raw;raw.Insert(L"slots",slots);
                response.raw_json=winrt::to_string(raw.Stringify());
                response.correct=curriculum::evaluate_drag_answers(block,block.correct_order).correct;
            } else if(block.type==BlockType::order_code&&archived_math) {
                for(const auto index:block.correct_order)response.order.push_back(static_cast<std::int64_t>(index));
                response.correct=curriculum::evaluate_order(block,block.correct_order).correct;
            } else if(block.type==BlockType::math_puzzle&&archived_math) {
                auto prerequisite=archived_math_prerequisite(lesson,block);
                const auto reply=archived_math_reply(prerequisite,prerequisite.source,prerequisite.responses);
                auto wrong=JsonObject::Parse(wide(prerequisite.responses));
                const auto changed=wrong.GetNamedObject(wide(prerequisite.plan->response)).GetNamedObject(
                    prerequisite.plan->matching?L"pairs":L"values");
                const auto first=changed.First().Current().Key();changed.Insert(first,JsonValue::CreateStringValue(L"unaudited-fixture-answer"));
                check(!archived_math_reply(prerequisite,prerequisite.source,winrt::to_string(wrong.Stringify())).GetNamedBoolean(L"correct"),
                    "The archived prerequisite hook accepted a changed pair/value: "+block.stable_id);
                JsonObject raw;raw.Insert(L"responses",JsonObject::Parse(wide(prerequisite.responses)));
                raw.Insert(L"correct",reply.GetNamedValue(L"correct"));raw.Insert(L"contentFingerprint",reply.GetNamedValue(L"contentFingerprint"));
                response.correct=reply.GetNamedBoolean(L"correct");response.raw_json=winrt::to_string(raw.Stringify());
                std::cout<<"ARCHIVED_QA prerequisite="<<block.stable_id<<" fixture_math="
                    <<(prerequisite.plan->matching?"exact-pairs":"exact-numeric-fields")
                    <<" fingerprint="<<prerequisite.fingerprint<<" worker_execution=0\n";
                archived_math->push_back(std::move(prerequisite));
            } else {
                check(block.type!=BlockType::order_code&&block.type!=BlockType::math_puzzle,
                    "A new earlier U2L9 exercise needs an explicitly audited QA preparation");
                continue;
            }
            check(response.correct,"An authored earlier-page QA response is invalid");
            state::put_response(data,lesson,block,response);
        }
        (void)state::mark_page_reached(data,lesson,p);
    }
}

// Real prerequisite observations for the current and authenticated archived
// U2L9 publications. Only the first case earns its receipt; an independent
// boxes input is observed without turning it into another required case.
void prepare_u2l9_prerequisites(state::StateData& data,const curriculum::Lesson& lesson,
        curriculum::StatePosition target,const std::filesystem::path& projects) {
    const auto& publication=u2l9_publication(lesson);
    const auto source_at=[&](std::string_view identity)->std::string {
        const auto at=curriculum::stable_block_position(lesson,identity);
        check(at.has_value(),"An authored U2L9 prerequisite reference is missing");
        const auto& block=lesson.pages.at(at->page).blocks.at(at->block);
        check(!block.code.empty(),"An authored U2L9 prerequisite source is empty");
        return block.code;
    };
    static std::uint64_t request_id=90000;
    for(std::size_t p=0;p<=target.page;++p)
        for(std::size_t b=0;b<lesson.pages[p].blocks.size();++b) {
            if(p==target.page&&b>=target.block)break;
            const auto& owner=lesson.pages[p].blocks[b];
            if(owner.type!=curriculum::BlockType::cpp_compiler)continue;
            check(owner.output_check&&!owner.output_check->cases.empty(),
                "A preceding U2L9 compiler has no independent output contract");
            const bool boxes=owner.stable_id==publication.boxes||
                (!publication.active_edition&&owner.stable_id=="block_0006_0002");
            std::string source;
            if(boxes)source=source_at(publication.boxes_reference);
            else if(!publication.active_edition&&owner.stable_id=="block_0008_0008")source=owner.initial_code;
            else if(owner.stable_id==publication.laboratory)source=source_at(publication.reference);
            else if(!publication.active_edition&&owner.stable_id=="block_0011_0006")source=source_at("block_0012_0002");
            else check(false,"U2L9 QA must explicitly audit a newly added prerequisite");
            if(publication.active_edition&&boxes){
                const auto reference=curriculum::stable_block_position(lesson,publication.boxes_reference);
                check(reference&&owner.standard_input=="Rita 4"&&
                    owner.output_check->input_comparison==curriculum::TextComparison::tokens&&
                    owner.output_check->cases.size()==2&&
                    owner.output_check->cases[0].standard_input=="Rita 4"&&
                    owner.output_check->cases[0].outputs==std::vector<std::string>{"Rita: 7","Rita: 7\n"}&&
                    owner.output_check->cases[1].standard_input=="Leo 2"&&
                    owner.output_check->cases[1].outputs==std::vector<std::string>{"Leo: 5","Leo: 5\n"}&&
                    curriculum::reference_activity(lesson,lesson.pages.at(reference->page).blocks.at(reference->block))==
                        curriculum::stable_block_position(lesson,publication.boxes),
                    "The active boxes prerequisite lost its independent input/output or own reference");
            }
            check(windows::compiler_puzzle_case_count(data,lesson,owner)==0&&
                !windows::compiler_puzzle_completed(data,lesson,owner),
                "A supposedly fresh U2L9 prerequisite already has credit");
            const std::size_t observations=boxes?2:1;
            check(owner.output_check->cases.size()>=observations,
                "The independent boxes input disappeared from its original contract");
            for(std::size_t index=0;index<observations;++index) {
                const auto& item=owner.output_check->cases[index];
                windows::CppRunRequest request;
                request.request_id=++request_id;request.revision=request_id;
                request.project_key=curriculum::draft_key("programming",lesson,owner);
                request.projects_directory=projects;request.source_utf8=source;
                request.stdin_utf8=item.standard_input;
                request.compile_timeout=std::chrono::seconds(60);
                request.execution_timeout=std::chrono::seconds(8);
                check(windows::compiler_runtime_request_allowed(owner,request),
                    "The prerequisite source or input does not match its own U2L9 owner");
                const auto result=windows::run_cpp(request);
                const auto captured=windows::capture_cpp_execution(request,result);
                check(result.error_utf8.empty()&&result.compilation.succeeded()&&
                    result.execution.succeeded()&&result.execution.stderr_bytes.empty()&&
                    windows::evaluate_cpp_output_check(*owner.output_check,captured,request.revision).verdict==
                        curriculum::OutputVerdict::correct,
                    "A real U2L9 prerequisite failed its independently authored input/output");
                if(index==0)check(windows::record_compiler_puzzle_case(
                    data,lesson,owner,request,captured,"Real U2L9 prerequisite execution"),
                    "A real first prerequisite case did not produce its own receipt");
            }
            check(windows::compiler_puzzle_completed(data,lesson,owner)&&
                  windows::compiler_puzzle_case_count(data,lesson,owner)==1,
                "A U2L9 prerequisite required or credited more than its first manual case");
        }
}

state::Preferences u2l9_compiler_receipts(const state::StateData& data) {
    state::Preferences receipts;
    if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
        for(const auto& [key,value]:domain->second)
            if(key.starts_with("compiler-puzzle/v1/"))receipts.emplace(key,value);
    return receipts;
}

void seed_compiler_prerequisites(state::StateData& data,const curriculum::Lesson& lesson){
    // Synthetic receipts isolate the prediction controls on their actual later
    // pages. They are QA prerequisites only, and do not claim any compilation.
    // The target prediction never receives a synthetic response or verdict.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check)continue;
        windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
        request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
        for(const auto& item:block.output_check->cases){
            check(!item.outputs.empty(),"A prerequisite compiler has no authored stdout oracle");
            request.stdin_utf8=item.standard_input;
            curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
            execution.executable=item.executable;
            if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA prerequisite diagnostic";
            (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"Synthetic QA prerequisite only");
        }
        check(windows::compiler_puzzle_completed(data,lesson,block),
            "Cannot isolate prediction from its compiler prerequisite: "+block.stable_id);
    }
}
void require_prepared_compiler_credit(const state::StateData& data,const curriculum::Lesson& lesson,
        curriculum::StatePosition target){
    for(std::size_t p=0;p<lesson.pages.size();++p)
        for(std::size_t b=0;b<lesson.pages[p].blocks.size();++b){
            const auto& block=lesson.pages[p].blocks[b];
            if(block.type!=curriculum::BlockType::cpp_compiler)continue;
            const bool prior=p<target.page||(p==target.page&&b<target.block);
            check(windows::compiler_puzzle_case_count(data,lesson,block)==(prior?1u:0u)
                &&windows::compiler_puzzle_completed(data,lesson,block)==prior,
                "Prepared-input QA changed a real prerequisite or credited a later compiler");
        }
}

void execute_multiline_prerequisite(state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& owner,const std::filesystem::path& projects){
    struct Prior {const char* identity;const char* reference;const char* output;};
    constexpr std::array prior{
        Prior{"block_0004_0005","block_0004_0007","Puesto 03\nMesas: 6\nCable: 3.75 m"},
        Prior{"block_0007_0004","block_0007_0006","Modulo 02\nInicio: 7\nAhora: 10"},
    };
    const auto selected=std::ranges::find_if(prior,[&](const auto& item){return owner.stable_id==item.identity;});
    check(lesson.id=="cpp_fundamentals__edition_es_1__objetos_y_estado__representar_procesar_y_recuperar"&&
        selected!=prior.end()&&owner.type==curriculum::BlockType::cpp_compiler&&owner.output_check&&
        owner.output_check->input_comparison==curriculum::TextComparison::exact&&owner.output_check->cases.size()==1,
        "A new multiline prerequisite needs an explicitly audited real execution");
    const auto reference=curriculum::stable_block_position(lesson,selected->reference);
    check(reference.has_value(),"The multiline prerequisite lost its own reference");
    const auto& source=lesson.pages.at(reference->page).blocks.at(reference->block);
    const auto& first=owner.output_check->cases.front();
    check(source.type==curriculum::BlockType::code&&!source.code.empty()&&first.standard_input.empty()&&
        first.outputs==std::vector<std::string>{selected->output,std::string(selected->output)+'\n'}&&
        curriculum::reference_activity(lesson,source)==curriculum::stable_block_position(lesson,owner.stable_id),
        "The multiline prerequisite changed its reference or independent output contract");
    static std::uint64_t request_id=120000;
    windows::CppRunRequest request;request.request_id=++request_id;request.revision=request_id;
    request.project_key=curriculum::draft_key("programming",lesson,owner);
    request.projects_directory=projects;request.source_utf8=source.code;request.stdin_utf8=first.standard_input;
    request.project_files=owner.project_files;request.source_files=owner.source_files;request.cmake=owner.cmake;
    request.local_int_check=owner.local_int_check;request.local_string_check=owner.local_string_check;
    request.input_check=owner.input_check;request.local_role_check=owner.local_role_check;
    request.compile_timeout=std::chrono::seconds(60);request.execution_timeout=std::chrono::seconds(8);
    check(!windows::compiler_puzzle_completed(data,lesson,owner)&&
        windows::compiler_puzzle_case_count(data,lesson,owner)==0&&
        windows::compiler_runtime_request_allowed(owner,request),
        "The multiline prerequisite already has credit or its source/input is not allowed");
    const auto result=windows::run_cpp(request);
    const auto captured=windows::capture_cpp_execution(request,result);
    check(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
        result.execution.stderr_bytes.empty()&&
        (!owner.local_int_check||captured.local_int_check_correct)&&
        (!owner.local_string_check||captured.local_string_check_correct)&&
        (!owner.input_check||captured.input_check_correct)&&(!owner.local_role_check||captured.local_role_check_correct)&&
        windows::evaluate_cpp_output_check(*owner.output_check,captured,request.revision).verdict==curriculum::OutputVerdict::correct,
        "The genuine multiline prerequisite failed: "+result.error_utf8+result.compilation.stderr_bytes);
    check(windows::record_compiler_puzzle_case(data,lesson,owner,request,captured,
        "Multiline QA prerequisite: actual execution of its own published reference")&&
        windows::compiler_puzzle_completed(data,lesson,owner)&&windows::compiler_puzzle_case_count(data,lesson,owner)==1,
        "The actual first multiline prerequisite case did not earn its own receipt");
    state::Draft draft;draft.source=request.source_utf8;draft.input=request.stdin_utf8;draft.has_saved=true;
    state::put_draft(data,request.project_key,draft);
    std::cout<<"MULTILINE prerequisite="<<owner.stable_id<<" real_cases=1 reference="<<source.stable_id<<'\n';
}

std::size_t prepare_multiline_prior_state(state::StateData& data,const curriculum::Lesson& lesson,
        curriculum::StatePosition target,const MultilinePlan& plan,const std::filesystem::path& projects){
    using curriculum::BlockType;
    using namespace winrt::Windows::Data::Json;
    check(lesson.id==plan.lesson_id&&lesson.pages.at(target.page).blocks.at(target.block).stable_id==plan.identity&&
        lesson.compiler_puzzles_required,"Multiline QA preparation loaded another authored owner");
    std::size_t executions=0;
    // Earlier quizzes, including those before the focal field on its page,
    // are evaluated QA setup. No focal/later answer or compiler is credited.
    for(std::size_t p=0;p<=target.page;++p){
        for(std::size_t b=0;b<lesson.pages[p].blocks.size();++b){
            if(p==target.page&&b>=target.block)break;
            const auto& block=lesson.pages[p].blocks[b];state::Response response;
            switch(block.type){
            case BlockType::heading:case BlockType::paragraph:case BlockType::note:
            case BlockType::code:case BlockType::image:continue;
            case BlockType::cpp_compiler:
                execute_multiline_prerequisite(data,lesson,block,projects);++executions;continue;
            case BlockType::multiple_choice:
                check(block.correct_index.has_value(),"A prior multiline QA choice has no answer");
                response.selected=static_cast<std::int64_t>(*block.correct_index);
                response.correct=curriculum::evaluate_choice(block,*block.correct_index).correct;break;
            case BlockType::fill_code:case BlockType::output_prediction:
                check(!block.answers.empty(),"A prior multiline QA text exercise has no answer");
                response.answer=block.answers.front();
                response.correct=curriculum::evaluate_text_answer(block,response.answer).correct;break;
            case BlockType::order_code:
                for(const auto index:block.correct_order)response.order.push_back(static_cast<std::int64_t>(index));
                response.correct=curriculum::evaluate_order(block,block.correct_order).correct;break;
            case BlockType::inline_fill_code:{
                JsonArray answers;for(const auto& answer:block.answers)answers.Append(JsonValue::CreateStringValue(wide(answer)));
                JsonObject raw;raw.Insert(L"answers",answers);response.raw_json=winrt::to_string(raw.Stringify());
                response.correct=curriculum::evaluate_hole_answers(block,block.answers).correct;break;
            }
            case BlockType::drag_fill_code:{
                JsonArray slots;for(const auto index:block.correct_order)slots.Append(JsonValue::CreateNumberValue(static_cast<double>(index)));
                JsonObject raw;raw.Insert(L"slots",slots);response.raw_json=winrt::to_string(raw.Stringify());
                response.correct=curriculum::evaluate_drag_answers(block,block.correct_order).correct;break;
            }
            default:check(false,"A prior multiline QA activity needs an explicitly audited preparation");
            }
            check(response.correct,"A prior multiline QA answer failed its real evaluator");
            state::put_response(data,lesson,block,response);
        }
        if(p<target.page)(void)state::mark_page_reached(data,lesson,p);
    }
    if(plan.next_page_compiler){
        // The continuation visits this editor before returning. Prepare an
        // existing draft so navigation tests preservation, not first creation.
        const auto next=curriculum::stable_block_position(lesson,plan.next_page_compiler);
        check(plan.completes_page&&next&&next->page==target.page+1,
            "The independently saved continuation draft moved to another page");
        const auto& editor=lesson.pages.at(next->page).blocks.at(next->block);
        check(editor.type==BlockType::cpp_compiler&&!editor.initial_code.empty(),
            "The continuation draft no longer belongs to its authored compiler");
        const auto key=curriculum::draft_key("programming",lesson,editor);
        const auto input=editor.standard_input.value_or("");
        const auto existing=state::get_draft(data,key,editor.initial_code,input);
        if(existing.has_saved)check(existing.source==editor.initial_code&&existing.input==input,
            "The continuation fixture already has a different draft; refusing to overwrite it");
        else{
            const auto document=windows::predefined_code_document(editor.initial_code);
            check(document.text==editor.initial_code,"Preparing the continuation editor rewrote its authored source");
            state::Draft draft;draft.source=document.text;draft.input=input;draft.has_saved=true;
            draft.indentation_json=state::make_indentation_metadata(draft.source,document.indentation_starts);
            draft.selection_start=document.selection_start;draft.selection_end=document.selection_end;
            state::put_draft(data,key,draft);
        }
        check(!state::get_response(data,lesson,editor)&&!windows::compiler_puzzle_completed(data,lesson,editor)&&
            windows::compiler_puzzle_case_count(data,lesson,editor)==0,
            "Preparing a continuation draft credited a future compiler");
    }
    check(executions==plan.earlier_compilers&&!state::get_response(data,lesson,lesson.pages[target.page].blocks[target.block]),
        "Multiline QA invented a target answer or lost a required genuine compiler observation");
    require_prepared_compiler_credit(data,lesson,target);
    state::validate_lesson_state(data,lesson);
    return executions;
}

void preserve_multiline_other_state(const state::StateData& before,const state::StateData& after,
        const curriculum::Lesson& lesson,const curriculum::Block& target){
    check(u2l9_compiler_receipts(after)==u2l9_compiler_receipts(before)&&
        after.preferences.at("exact_prediction_qa_unrelated")==before.preferences.at("exact_prediction_qa_unrelated"),
        "Multiline editing changed a real compiler receipt or an unrelated preference");
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.stable_id==target.stable_id)continue;
        const auto old=state::get_response(before,lesson,block),current=state::get_response(after,lesson,block);
        check(old.has_value()==current.has_value(),"Multiline editing added or removed another exercise's progress");
        if(old)check(current->raw_json==old->raw_json&&current->correct==old->correct&&current->answer==old->answer&&
            current->selected==old->selected&&current->order==old->order,
            "Multiline editing changed another exercise's saved response or progress");
        if(block.type!=curriculum::BlockType::cpp_compiler)continue;
        const auto key=curriculum::draft_key("programming",lesson,block);
        const auto prior_draft=state::get_draft(before,key,block.initial_code,block.standard_input.value_or(""));
        const auto current_draft=state::get_draft(after,key,block.initial_code,block.standard_input.value_or(""));
        check(current_draft.has_saved==prior_draft.has_saved&&current_draft.source==prior_draft.source&&
            current_draft.input==prior_draft.input&&current_draft.indentation_json==prior_draft.indentation_json&&
            current_draft.selection_start==prior_draft.selection_start&&current_draft.selection_end==prior_draft.selection_end,
            "Multiline editing changed independent compiler draft "+block.stable_id+
            ": has_saved="+std::to_string(current_draft.has_saved)+" expected="+std::to_string(prior_draft.has_saved)+
            "; source_equal="+std::to_string(current_draft.source==prior_draft.source)+
            "; input_equal="+std::to_string(current_draft.input==prior_draft.input)+
            "; indentation_equal="+std::to_string(current_draft.indentation_json==prior_draft.indentation_json)+
            "; selection_equal="+std::to_string(current_draft.selection_start==prior_draft.selection_start&&
                current_draft.selection_end==prior_draft.selection_end));
    }
}

void prepare_visual_profile(std::string_view scenario,std::wstring_view token){
    const bool choice=scenario=="u11l11-choice";
    const bool active_edition=scenario=="u2l9-stages-active-edition"||scenario=="u2l9-variants-active-edition";
    const bool stages=scenario=="u2l9-stages"||scenario=="u2l9-stages-active-edition";
    const bool variants=scenario=="u2l9-variants"||scenario=="u2l9-variants-active-edition";
    const bool prepared=stages||variants;
    const auto& publication=active_edition?active_u2l9_publication:archived_u2l9_publication;
    check((scenario=="u11l2"||scenario=="u11l11"||choice||prepared)&&is_valid_qa_instance(token),
        "Invalid editorial visual QA scenario or unsafe instance token");
    const auto filename=prepared?"lesson_leer_una_entrada_conocida.json"
        :scenario=="u11l2"?"lesson_lineas_y_separadores_pendientes.json"
        :"lesson_una_gramatica_para_sumar_y_restar.json";
    const Plan* selected=nullptr;
    for(const auto& plan:plans)if(std::string_view(plan.filename).ends_with(filename)
        &&(!prepared||std::string_view(plan.identity)==(stages?"block_0006_0004":"block_0012_0002")))selected=&plan;
    check(selected&&selected->editorial_feedback,"The visual target has no current editorial regression");
    check(!active_edition||(selected->edition_identity&&selected->edition_answer),
        "The active visual target has no explicitly audited identity and answer");
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/
        (active_edition?publication.filename:selected->filename);
    const auto asset_hash=state::sha256(state::read_bytes(path));const auto lesson=windows::load_lesson(path);
    if(prepared){
        check(lesson.id==publication.lesson_id,"Visual QA loaded another U2L9 publication");
        (void)u2l9_publication(lesson);require_active_u2l9_reference_layout(lesson,publication);
    }
    const std::string_view identity=choice?"block_0002_0002":active_edition?selected->edition_identity:selected->identity;
    const auto position=curriculum::stable_block_position(lesson,identity);
    check(position&&position->page==(variants?12u:scenario=="u11l2"||choice?2u:6u)&&position->block==(scenario=="u11l2"?1u:2u),
        "The selected visual activity moved outside its audited page");
    const auto& target=lesson.pages[position->page].blocks[position->block];
    check(choice?target.type==curriculum::BlockType::multiple_choice&&target.correct_index==0&&target.options.size()==3
        :target.type==curriculum::BlockType::output_prediction
            &&target.output_comparison==(prepared?curriculum::TextComparison::exact:curriculum::TextComparison::trimmed),
        "The selected visual activity lost its published contract");
    if(prepared)check(target.id==selected->activity
        &&curriculum::uses_single_line_answer(target)==variants&&lesson.compiler_puzzles_required,
        "The current U2L9 prediction lost its independent exact answer contract");
    if(active_edition)check(selected->edition_answer&&target.answers==std::vector<std::string>{selected->edition_answer},
        "The active U2L9 visual prediction lost its independently audited answer");
    const auto prerequisites=curriculum::activity_prerequisites(lesson,target);
    check(scenario=="u11l2"||choice||prepared?prerequisites.empty():prerequisites.size()==1
        &&lesson.pages[prerequisites[0].page].blocks[prerequisites[0].block].stable_id=="block_0004_0003",
        "The selected visual activity's audited prerequisites changed");
    const auto environment=resolve_windows_environment({ProfileId::Codex,std::wstring(token),false});
    check(environment.paths.root.parent_path().filename()==L"qa"
        &&environment.paths.root.parent_path().parent_path().filename()==L"Codex"
        &&environment.paths.root.filename()==token,"Visual QA resolved outside the selected isolated Codex profile");
    state::require_safe_path(environment.paths.root);
    check(!std::filesystem::exists(environment.paths.root),"Visual QA refuses to reuse or overwrite an existing profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    check(std::filesystem::create_directory(environment.paths.root),"Another process already created this visual QA profile");
    initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);const auto empty=store.load();
    check(empty.preferences.empty(),"The newly reserved visual QA profile unexpectedly contains prior state");
    state::StateData data;state::prepare_lesson_state(data,lesson);
    // Both page-three activities precede their compiler and need no synthetic
    // completion. U11L11's page-seven prediction has a published activityAfter
    // requiring its one earlier compiler; this explicit QA receipt isolates
    // that prediction and never claims a real execution.
    if(scenario=="u11l11"){
        std::size_t compilers=0;for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
            if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
                check(block.stable_id=="block_0004_0003","The isolated U11L11 QA acquired an unrelated compiler objective");++compilers;
            }
        check(compilers==1,"The isolated U11L11 QA requires exactly its audited compiler prerequisite");
        seed_compiler_prerequisites(data,lesson);
    }
    if(prepared){
        prepare_u2l9_prior_quizzes(data,lesson,*position);
        prepare_u2l9_prerequisites(data,lesson,*position,environment.paths.projects);
        require_prepared_compiler_credit(data,lesson,*position);
    }
    state::save_current_page(data,lesson,position->page);auto& preferences=data.preferences["native_windows"];
    preferences["ui_language"]=std::string("es");preferences["content_language"]=std::string("es");
    preferences["catalog/locale"]=std::string("es");preferences["catalog/route"]=std::string("programming");
    preferences["catalog/course"]=std::string("cpp_fundamentals");
    const auto unit_end=lesson.id.rfind("__");check(unit_end!=std::string::npos,"The selected lesson has no catalog unit identity");
    preferences["catalog/unit"]=lesson.id.substr(0,unit_end);preferences["navigation/lesson"]=lesson.id;
    for(std::size_t p=0;p<lesson.pages.size();++p)for(const auto& block:lesson.pages[p].blocks)
        if(!prepared||p>=position->page)
            check(!state::get_response(data,lesson,block),"Visual QA answered the selected or a later quiz");
    state::validate_lesson_state(data,lesson);store.save(data);const auto saved=store.load();
    check(saved==data&&state::current_page(saved,lesson)==position->page&&!state::get_response(saved,lesson,target),
        "The prepared visual QA profile did not preserve the unanswered activity");
    if(prepared)require_prepared_compiler_credit(saved,lesson,*position);
    check(state::sha256(state::read_bytes(path))==asset_hash,"Preparing visual QA changed an authored asset");
    std::cout<<"profile=Codex\nqa_instance="<<utf8(token)<<"\ndata_root="<<utf8(environment.paths.root.native())
        <<"\nlesson="<<lesson.id<<"\ntarget="<<identity<<"\npage="<<position->page+1
        <<"\npage_index="<<position->page<<"\nblock="<<position->block+1
        <<"\nsynthetic_prerequisites="<<(scenario=="u11l11"?"block_0004_0003-only-not-a-real-execution":"none")
        <<"\nreal_u2l9_prerequisites="<<(prepared?"preceding-first-cases-only":"none")
        <<"\nquiz_responses="<<(prepared?"earlier-pages-authored-QA-setup":"none")
        <<"\ntarget_completed=0\nwindows_created=0\n";
}

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    curriculum::StatePosition position;
    bool editorial_feedback = false;
    std::size_t real_compiler_runs = 0;
    std::vector<ArchivedMathPrerequisite> archived_math;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,curriculum::StatePosition target,bool verify_editorial=false,bool seed_prerequisites=true,
        const MultilinePlan* multiline=nullptr)
        :environment(resolve_windows_environment({ProfileId::Codex,
            L"exact-prediction-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),
         lesson(std::move(model)),position(target),editorial_feedback(verify_editorial){
        check(!std::filesystem::exists(environment.paths.root),"Literal prediction QA requires a fresh private profile");
        initialize_profile_directories(environment.paths);
        state::StateData data;state::prepare_lesson_state(data,lesson);
        if(multiline)real_compiler_runs=prepare_multiline_prior_state(data,lesson,target,*multiline,environment.paths.projects);
        else if(seed_prerequisites){
            seed_compiler_prerequisites(data,lesson);
            prepare_u2l9_prior_quizzes(data,lesson,target,&archived_math);
            check(!state::get_response(data,lesson,this->target()),
                "Preparing archived prerequisites answered the focal prediction");
        }
        else{
            check(lesson.compiler_puzzles_required&&curriculum::activity_prerequisites(lesson,this->target()).empty(),
                "The prepared-input prediction acquired an unaudited compiler prerequisite");
            prepare_u2l9_prior_quizzes(data,lesson,target);
            prepare_u2l9_prerequisites(data,lesson,target,environment.paths.projects);
            require_prepared_compiler_credit(data,lesson,target);
        }
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["exact_prediction_qa_unrelated"]["keep"]=std::string(" \t unrelated preference\r\n");
        auto& reserve=data.preferences["attempt_reserve_seconds_test"];
        reserve["reserve_programming"]=8.;reserve["updated_at_programming"]=now_ms();
        reserve["recovery_interval_programming"]=attempts::recovery_millis;
        state::save_current_page(data,lesson,position.page);
        state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden literal prediction QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create hidden literal prediction host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    const curriculum::Block& target()const{return lesson.pages.at(position.page).blocks.at(position.block);}
    void open(){active_step="open/reopen the authored page";
        const auto verified=std::make_shared<std::atomic<std::size_t>>(0);
        check(archived_math.size()<=archived_math_plans.size(),"The archived QA hook acquired an unbounded prerequisite set");
        view=archived_math.empty()?create_lesson_view(host,environment,lesson):
            create_lesson_view(host,environment,lesson,archived_math_hooks(archived_math,verified));
        check(view!=nullptr,"Cannot create the actual prediction lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);pump();
        if(!archived_math.empty()){
            const auto all=(std::size_t{1}<<archived_math.size())-1;
            const auto deadline=GetTickCount64()+10000;
            while((verified->load()!=all||lesson_page_state(view).index!=position.page||
                !lesson_control(view,control_id(position.block,2)))&&GetTickCount64()<deadline){
                MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);pump();
            }
            if(verified->load()!=all||lesson_page_state(view).index!=position.page||
                    !lesson_control(view,control_id(position.block,2))){
                std::string unresolved;
                for(std::size_t i=0;i<archived_math.size();++i)if(!(verified->load()&(std::size_t{1}<<i)))
                    unresolved+=' '+std::string(archived_math[i].plan->identity);
                check(false,"Archived prediction entry remains unresolved: expected page="+std::to_string(position.page)+
                    ", actual page="+std::to_string(lesson_page_state(view).index)+", pending math="+
                    (unresolved.empty()?std::string("none; inspect the preceding quiz/compiler gate"):unresolved));
            }
        }
        check(!IsWindowVisible(host),"Prediction QA exposed a desktop window");
        check(lesson_page_state(view).index==position.page,"The prediction did not restore its actual authored page: expected="+
            std::to_string(position.page)+", actual="+std::to_string(lesson_page_state(view).index));
        if(editorial_feedback){auto question=target().question;std::erase(question,'`');
            check(text(0)==question&&text(1)==target().code,"The current editorial question or displayed program changed on open/reopen");}
    }
    void reopen(){check(flush_lesson_view(view),"Cannot save prediction QA");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush prediction QA");return read_lesson_state(view);}
    HWND control(int part)const{const auto result=lesson_control(view,control_id(position.block,part));
        check(result!=nullptr,"The authored prediction control is missing");return result;}
    std::string text(int part)const{return edit_text(control(part));}
    void verify_feedback(bool correct)const{
        if(!editorial_feedback)return;
        const auto explanation=correct?target().explanation:target().review_feedback;
        check(!explanation.empty(),"The current editorial prediction lost its authored explanation or retry guide");
        const auto expected=std::string(correct?"✓ Correcto":"✗ Incorrecto")+"\n\n"+explanation;
        check(text(4)==expected,"The editorial prediction omitted or mixed its full authored guide/success explanation");
        bool found=false;for(const auto& region:lesson_reading_regions(view))if(region.target==control(4)){
            found=true;check(region.feedback&&region.correct==correct&&region.source_text==expected,
                "The current editorial feedback lost its exact visible/narration source or verdict");}
        check(found,"The current editorial prediction has no matching feedback reading region");
    }
    void replace(std::string_view value){active_step="replace the native answer";
        check(!curriculum::uses_single_line_answer(target())||value.find_first_of("\r\n")==value.npos,
            "The QA fixture must not force a multiline draft into a single-line control");
        const auto answer=control(2);check(IsWindowEnabled(answer),"Prediction answer is unexpectedly locked");
        CHARRANGE all{0,-1};SendMessageW(answer,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));const auto source=wide(value);
        SendMessageW(answer,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(source.c_str()));pump();
        // RichEdit presents CRLF paragraph breaks as LF while preserving
        // every paragraph, including a final break, in the actual UI draft.
        check(text(2)==native_newlines(value),"RichEdit changed significant answer characters");
    }
    void submit(){active_step="submit the native answer";check(IsWindowEnabled(control(3)),"A complete authored prediction could not be submitted");
        SendMessageW(control(3),BM_CLICK,0,0);pump();check(!IsWindowVisible(host),"Submitting exposed the hidden QA host");}
    void incomplete(){
        for(const auto value:{""," \t "}){
            replace(value);check(!IsWindowEnabled(control(3)),"An incomplete literal prediction enabled Check");
            const auto before=saved();
            // Exercise the normal disabled-button path. Queued incomplete
            // commands also create a visible notice, outside this hidden QA.
            SendMessageW(control(3),BM_CLICK,0,0);pump();const auto after=saved();
            check(text(4).empty(),"An incomplete answer acquired a verdict");
            const auto response=state::get_response(after,lesson,target());
            check(!response||(!response->correct&&!state::response_reviewed(*response,target())),"An incomplete answer became reviewed");
            check(attempt_preferences(before)==attempt_preferences(after),"An incomplete answer consumed a heart");
        }
    }
    void draft(std::string_view answer){
        active_step="save/reopen an unreviewed native draft";
        const auto before=saved();replace(answer);
        const auto saved_draft=saved();const auto response=state::get_response(saved_draft,lesson,target());
        check(response&&!response->correct&&!state::response_reviewed(*response,target())&&
            response->answer==native_newlines(answer)&&text(4).empty(),
            "Saving an unsubmitted prediction changed its significant draft characters or granted a verdict");
        check(attempt_preferences(before)==attempt_preferences(saved_draft),"Saving an unsubmitted draft consumed a heart");
        reopen();active_step="verify the reopened unreviewed draft";
        const auto restored=saved();const auto restored_response=state::get_response(restored,lesson,target());
        check(restored_response&&!restored_response->correct&&!state::response_reviewed(*restored_response,target())&&
            restored_response->answer==native_newlines(answer)&&text(2)==native_newlines(answer)&&
            text(4).empty()&&IsWindowEnabled(control(2))&&IsWindowEnabled(control(3)),
            "Reopening changed an unreviewed prediction draft, locked it or invented feedback");
        check(attempt_preferences(before)==attempt_preferences(restored),"Reopening an unsubmitted draft consumed a heart");
        check(restored_response->raw_json==response->raw_json,"Reopening rewrote a saved unreviewed prediction payload");
    }
    void rejected(std::string_view answer,bool reopen_after){
        const auto previous_answer=text(2),previous_feedback=text(4);
        replace(answer);
        // LF and CRLF paste variants share one RichEdit paragraph spelling.
        // Replacing it with identical content need not invalidate its verdict.
        check(text(4).empty()||(text(2)==previous_answer&&text(4)==previous_feedback),
            "Editing changed the response but retained the previous answer's verdict");
        submit();
        const auto feedback=text(4);check(feedback.starts_with("✗ Incorrecto"),"Added or changed significant characters were accepted");
        const auto data=saved();const auto response=state::get_response(data,lesson,target());
        check(response&&!response->correct&&state::response_reviewed(*response,target())
            &&native_newlines(response->answer)==text(2),"Rejected prediction state lost its actual reviewed response");
        verify_feedback(false);
        if(reopen_after){reopen();check(text(4)==feedback&&text(2)==native_newlines(answer),"Reopening lost a rejected literal answer or its retry feedback");
            const auto restored=saved();const auto restored_response=state::get_response(restored,lesson,target());
            check(restored_response&&restored_response->raw_json==response->raw_json,"Reopening rewrote a reviewed rejected prediction payload");
            verify_feedback(false);}
    }
    void accepted_current(std::string_view answer){
        check(text(2)==native_newlines(answer)&&text(4).empty(),"The pending answer changed or retained a previous verdict");
        const auto before=saved();submit();const auto feedback=text(4);
        check(feedback.starts_with("✓ Correcto")&&!IsWindowEnabled(control(2))&&!IsWindowEnabled(control(3)),"A valid literal prediction was rejected or did not lock");
        const auto after=saved();const auto response=state::get_response(after,lesson,target());
        check(response&&response->correct&&native_newlines(response->answer)==native_newlines(answer),"Correct prediction state changed the submitted answer");
        check(attempt_preferences(before)==attempt_preferences(after),"An accepted prediction consumed a heart");
        verify_feedback(true);
        reopen();const auto restored=saved();const auto restored_answer=state::get_response(restored,lesson,target());
        check(text(4)==feedback&&text(2)==native_newlines(answer)&&!IsWindowEnabled(control(2))
            &&restored_answer&&restored_answer->correct,"A correct prediction did not retain its answer, lock and feedback on reopen");
        check(restored_answer->raw_json==response->raw_json,"Reopening rewrote a correct prediction payload");verify_feedback(true);
    }
    void accepted(std::string_view answer){replace(answer);accepted_current(answer);}
    void append_final_enter(){
        check(!curriculum::uses_single_line_answer(target()),"The final-Enter probe requires an actual multiline control");
        const auto initial=text(2);const auto before=saved();const auto answer=control(2);
        check(!initial.empty()&&initial.back()!='\n'&&text(4).empty(),"The final-Enter probe must start with an unreviewed complete draft");
        CHARRANGE end{-1,-1};SendMessageW(answer,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&end));SetFocus(answer);
        MSG enter{};enter.hwnd=answer;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;enter.lParam=1;
        check(windows::CodeEditor::handle_key_message(enter),"The actual multiline editor did not handle final Enter");
        enter.message=WM_KEYUP;(void)windows::CodeEditor::handle_key_message(enter);pump();
        const auto after=saved();const auto response=state::get_response(after,lesson,target());
        check(text(2)==initial+'\n'&&text(4).empty()&&response&&response->answer==initial+'\n'&&
            !response->correct&&!state::response_reviewed(*response,target())&&
            attempt_preferences(after)==attempt_preferences(before),
            "Final Enter changed the interior payload, submitted the answer or consumed an attempt");
        reopen();const auto restored=saved();const auto pending=state::get_response(restored,lesson,target());
        check(text(2)==initial+'\n'&&text(4).empty()&&pending&&pending->raw_json==response->raw_json&&
            !pending->correct&&!state::response_reviewed(*pending,target())&&
            attempt_preferences(restored)==attempt_preferences(after),
            "Reopening lost or evaluated the exact pending final-Enter draft");
    }
    void single_line_breaks(std::string_view incorrect){
        check(curriculum::uses_single_line_answer(target())&&!curriculum::evaluate_text_answer(target(),incorrect).correct,
            "The single-line key regression requires an ordinary incorrect draft");
        replace(incorrect);
        active_step="single-line LF/CRLF input preserves the draft";
        for(const auto input:{L"\n",L"\r\n"}){
            const auto answer=control(2);
            SendMessageW(answer,WM_KEYDOWN,VK_RETURN,1);
            for(const auto* character=input;*character;++character)SendMessageW(answer,WM_CHAR,*character,1);
            SendMessageW(answer,WM_KEYUP,VK_RETURN,LPARAM{1}<<31);pump();
            check(text(2)==incorrect&&text(4).starts_with("✗ Incorrecto"),
                "A single-line Enter inserted a line break or accepted the ordinary incorrect draft");
        }
        const auto after=saved();const auto response=state::get_response(after,lesson,target());
        check(response&&!response->correct&&response->answer==incorrect,
            "Single-line Enter saved a multiline or changed response");
        reopen();check(text(2)==incorrect&&text(4).starts_with("✗ Incorrecto"),
            "A draft preserved by single-line Enter could not reopen");
    }
    void reset(){check(IsWindowEnabled(control(12)),"A solved prediction has no Reset action");
        SendMessageW(control(12),BM_CLICK,0,0);pump();reopen();
        check(text(2).empty()&&text(4).empty()&&IsWindowEnabled(control(2))&&!IsWindowEnabled(control(3)),"Explicit Reset failed to restore an unanswered prediction");}
};

void optional_final_newline_predictions(){
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    std::size_t targets=0,accepted=0,rejected=0,physical_enters=0,raw_cr_rejections=0,real_prerequisites=0;
    for(const auto& plan:multiline_plans){
        active_case=std::string(plan.filename)+" / "+plan.identity;
        active_step="load the exact authored multiline prediction";
        const auto path=content/plan.filename;const auto original=state::read_bytes(path);
        const auto lesson=windows::load_lesson(path);
        check(lesson.id==plan.lesson_id&&lesson.locale=="es"&&lesson.course_edition&&*lesson.course_edition==
            curriculum::CourseEdition{"cpp_fundamentals","es","cpp_desde_cero_2026",1},
            "The final-newline regression did not load its explicit active publication");
        const auto position=curriculum::stable_block_position(lesson,plan.identity);
        check(position.has_value(),"The final-newline prediction lost its stable identity");
        const auto& block=lesson.pages.at(position->page).blocks.at(position->block);
        const std::string expected=plan.answer;
        check(block.type==curriculum::BlockType::output_prediction&&block.id==plan.activity&&!block.code.empty()&&
            block.output_comparison==curriculum::TextComparison::exact&&!curriculum::uses_single_line_answer(block)&&
            block.answers==std::vector<std::string>{expected}&&expected.find('\n')!=expected.npos&&expected.back()!='\n',
            "The audited exact multiline answer, program or input shape changed");
        check(curriculum::activity_prerequisites(lesson,block).empty(),
            "A new explicit prerequisite needs an audited final-newline preparation");
        std::cout<<"RUN optional final newline "<<lesson.id<<" / "<<block.stable_id<<'\n'<<std::flush;
        View fixture(lesson,*position,plan.editorial_feedback,false,&plan);
        real_prerequisites+=fixture.real_compiler_runs;
        const auto before=fixture.saved();
        const auto progress=[&](bool correct,bool reviewed_rejection){
            const auto current=fixture.saved();const auto response=state::get_response(current,lesson,block);
            // The persisted review identity records an evaluated rejection.
            // A correct response stores its verdict and clears that identity.
            const bool actual_correct=response&&response->correct;
            const bool actual_reviewed_rejection=response&&state::response_reviewed(*response,block);
            check(actual_correct==correct&&actual_reviewed_rejection==reviewed_rejection,
                "The multiline response acquired or lost progress before its actual Check action: correct="+
                std::to_string(actual_correct)+" expected="+std::to_string(correct)+
                "; reviewed_rejection="+std::to_string(actual_reviewed_rejection)+
                " expected="+std::to_string(reviewed_rejection));
            const auto page=lesson_page_state(fixture.view);
            check(page.index==position->page&&page.can_next==(correct&&plan.completes_page),
                "The multiline verdict lost its next-page gate or bypassed a still-pending activity");
            preserve_multiline_other_state(before,current,lesson,block);
            require_prepared_compiler_credit(current,lesson,*position);
            auto question=block.question;std::erase(question,'`');
            check(fixture.text(0)==question&&fixture.text(1)==block.code,
                "The multiline control changed its authored question or displayed program");
            if(correct){
                const auto feedback=std::string("✓ Correcto")+(block.explanation.empty()?"":"\n\n"+block.explanation);
                check(fixture.text(4)==feedback,"The multiline success lost its complete authored explanation");
                const auto regions=lesson_reading_regions(fixture.view);
                check(std::ranges::any_of(regions,[&](const auto& region){return region.target==fixture.control(4)&&
                    region.feedback&&region.correct&&region.source_text==feedback;}),
                    "The multiline success lost its exact feedback reading region");
            }
        };
        fixture.incomplete();progress(false,false);
        // A semantically acceptable trailing LF remains an unreviewed draft
        // until the native Check button is used, including after reopening.
        fixture.draft(expected+'\n');progress(false,false);
        auto missing_interior=expected;missing_interior.erase(missing_interior.find('\n'),1);
        auto extra_interior=expected;extra_interior.insert(extra_interior.find('\n'),1,'\n');
        const std::array<std::string,7> wrong{
            expected+"\n\n",crlf_lines(expected)+"\r\n\r\n",' '+expected,expected+' ',
            plan.wrong_internal_space,missing_interior,extra_interior,
        };
        for(const auto& value:wrong){
            check(!curriculum::evaluate_text_answer(block,value).correct,
                "A significant multiline space, interior row or second final line was accepted");
            fixture.rejected(value,true);progress(false,true);++rejected;
        }
        // RichEdit represents a pasted bare CR as a paragraph. Test its raw
        // evaluator contract separately instead of claiming those bytes survive UI input.
        auto bare_cr=expected;bare_cr[bare_cr.rfind('\n')]='\r';
        check(!curriculum::evaluate_text_answer(block,bare_cr).correct,
            "The exact multiline evaluator erased a significant bare carriage return");
        ++raw_cr_rejections;
        const std::array<std::string,4> accepted_forms{
            expected,expected+'\n',crlf_lines(expected),crlf_lines(expected)+"\r\n",
        };
        bool first=true;
        for(const auto& value:accepted_forms){
            if(!first){fixture.reset();progress(false,false);}first=false;
            check(curriculum::evaluate_text_answer(block,value).correct,
                "The exact multiline evaluator rejected LF/CRLF or its single optional terminator");
            fixture.accepted(value);progress(true,false);++accepted;
        }
        fixture.reset();progress(false,false);
        fixture.replace(expected);fixture.append_final_enter();progress(false,false);++physical_enters;
        // Check the actual Enter-created draft without replacing it first.
        fixture.accepted_current(expected+'\n');progress(true,false);++accepted;
        if(plan.completes_page){
            check(navigate_lesson_view(fixture.view,position->page+1),
                "An actually completed multiline page did not allow continuation");pump();
            check(lesson_page_state(fixture.view).index==position->page+1&&
                navigate_lesson_view(fixture.view,position->page),
                "The multiline continuation lost its destination or return route");pump();
            fixture.reopen();progress(true,false);
        }
        fixture.reset();fixture.incomplete();progress(false,false);
        check(state::read_bytes(path)==original,"Final-newline QA changed authored content or its state layout");
        ++targets;
        std::cout<<"PASS optional final newline "<<block.stable_id
            <<": 5 native accepted forms including physical Enter, 7 native rejections, exact drafts/reopen, "
            <<"progress and independent real receipts preserved\n";
    }
    check(targets==4&&accepted==20&&rejected==28&&physical_enters==4&&raw_cr_rejections==4&&real_prerequisites==2,
        "The four authored final-newline controls or their real prerequisite executions are incomplete");
    std::cout<<"PASS four authored exact multiline predictions: 20 native acceptances, 28 native rejections, "
        <<"4 real Enter insertions, 4 raw bare-CR evaluator rejections, 2 genuine MSVC prerequisite runs; "
        <<"significant spaces/interior rows, unreviewed drafts, reopen, scoped Reset, next-page gates and unchanged source assets\n";
}

constexpr std::string_view recovery_before_full="c646f1115a768fe8c66e97960f30082e01f3f656e1b393a7463697aba151612a";
constexpr std::string_view recovery_after_full="874ca9f94a235149ee8e93866c35cf7daed4aba3d0ad97adce4fcccfd1bce4a6";
curriculum::Lesson recovery_explanation_before(const curriculum::Lesson& live){
    using namespace winrt::Windows::Data::Json;
    // Independent full-block pins come from c570ceb and the one reviewed field
    // replacement. Work may integrate the final text without breaking this QA.
    auto validated=windows::parse_lesson_json(live.raw_json,live.locale);
    validated.source_path=live.source_path;
    const auto at=curriculum::stable_block_position(validated,"block_0006_0002");
    check(at&&at->page==6&&at->block==2,"The recovery fixture lost its authored identity");
    const auto root=JsonObject::Parse(winrt::to_hstring(validated.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block));
    const auto digest=test::compile_check_test_fingerprint(block);
    if(digest==recovery_before_full)return validated;
    check(digest==recovery_after_full,"The recovery fixture differs from its independent before/after payloads");
    const auto layout=root.GetNamedObject(L"stateLayout");
    JsonArray retained;unsigned removed=0;bool terminal=false;
    for(const auto& value:layout.GetNamedArray(L"editorial")){
        const auto proof=value.GetObject();
        if(proof.GetNamedString(L"id")==L"block_0006_0002"){
            terminal=winrt::to_string(proof.GetNamedString(L"beforeSha256"))==recovery_before_full
                &&winrt::to_string(proof.GetNamedString(L"afterSha256"))==recovery_after_full;
            if(terminal){++removed;continue;}
        }
        retained.Append(value);
    }
    check(removed==1&&terminal,"The integrated recovery explanation needs its exact terminal editorial edge");
    block.Insert(L"explanation",JsonValue::CreateStringValue(L"El lector consume ? y lanza. La recuperación descarta +9; como resto de la operación incorrecta; la siguiente operación produce 4."));
    check(test::compile_check_test_fingerprint(block)==recovery_before_full,"The complete original recovery prediction was not recovered");
    layout.Insert(L"editorial",retained);
    auto before=windows::parse_lesson_json(winrt::to_string(root.Stringify()),live.locale);
    before.source_path=live.source_path;return before;
}

void proposed_u11_recovery_explanation(){
    using namespace winrt::Windows::Data::Json;
    active_case="U11L14 / block_0006_0002 explanation proposal";
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json";
    const auto original_bytes=state::read_bytes(path);
    const auto original=recovery_explanation_before(windows::load_lesson(path));
    const auto at=curriculum::stable_block_position(original,"block_0006_0002");
    check(at&&at->page==6&&at->block==2,"The recovery prediction lost its authored identity");
    const auto& old=original.pages[at->page].blocks[at->block];
    check(old.type==curriculum::BlockType::output_prediction&&
        state::sha256(old.explanation)=="a90bdfb0e52d5a6cec6df305dc03ba2ce949bfff65683bc6e7812a091d605806",
        "The proposed recovery explanation no longer starts from its pinned original");
    const std::string explanation="El lector consume ? y lanza una excepción. La recuperación descarta +9; como resto de la operación incorrecta; la siguiente operación produce 4.";
    check(state::sha256(explanation)=="a9b2baed596a3e465940d3b425f065c17eed32115e2c4632b4d03e0ab56a2f90",
        "The recovery proposal differs from the exact reviewed explanation");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block));
    const auto before=test::compile_check_test_fingerprint(block);
    check(before==recovery_before_full,"The recovery proposal lost its independent complete original");
    block.Insert(L"explanation",JsonValue::CreateStringValue(winrt::to_hstring(explanation)));
    check(test::compile_check_test_fingerprint(block)==recovery_after_full,"The recovery proposal differs from its independently pinned final payload");
    JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(L"block_0006_0002"));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(before)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(block))));
    root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(proof);
    auto proposed=windows::parse_lesson_json(winrt::to_string(root.Stringify()),original.locale);
    proposed.source_path=original.source_path;
    const auto reconstructed=recovery_explanation_before(proposed);
    const auto original_proofs=JsonObject::Parse(winrt::to_hstring(original.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    const auto restored_proofs=JsonObject::Parse(winrt::to_hstring(reconstructed.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    check(restored_proofs.Stringify()==original_proofs.Stringify()
        &&reconstructed.pages[at->page].blocks[at->block].explanation==old.explanation,
        "An integrated recovery fixture lost its original editorial prefix or became an unchanged transition");
    // Reject a final-looking source without the authenticated reviewed edge.
    const auto missing=JsonObject::Parse(winrt::to_hstring(proposed.raw_json));
    const auto proofs=missing.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");proofs.RemoveAtEnd();
    auto unproven=proposed;unproven.raw_json=winrt::to_string(missing.Stringify());bool rejected=false;
    try{(void)recovery_explanation_before(unproven);}catch(const std::exception&){rejected=true;}
    check(rejected,"The recovery fixture accepted a final payload without its reviewed historical transition");
    const auto& current=proposed.pages[at->page].blocks[at->block];
    check(current.code==old.code&&current.answers==old.answers&&current.review_feedback==old.review_feedback
        &&current.output_comparison==old.output_comparison&&current.explanation==explanation
        &&curriculum::exercise_key(proposed,current)==curriculum::exercise_key(original,old),
        "The exact explanation proposal changed its program, answer, guide or state address");
    // Native fields supply both attempts. Only earlier compiler prerequisites
    // are explicitly synthetic; this does not claim a compiler execution.
    View view(original,*at,true);
    const std::string wrong="Resultado: 4";
    check(!curriculum::evaluate_text_answer(old,wrong).correct,"The recovery retry fixture is not incorrect");
    view.draft(wrong);view.rejected(wrong,true);view.accepted(old.answers.front());
    const auto previous=view.saved();const auto answer=state::get_response(previous,original,old);
    check(answer&&answer->correct,"The original recovery prediction has no actual accepted answer");
    view.lesson=std::move(proposed);view.reopen();view.verify_feedback(true);
    const auto restored=view.saved();const auto retained=state::get_response(restored,view.lesson,view.target());
    check(retained&&retained->correct&&retained->raw_json==answer->raw_json&&retained->answer==answer->answer
        &&attempt_preferences(restored)==attempt_preferences(previous)
        &&restored.preferences.at("exact_prediction_qa_unrelated")==previous.preferences.at("exact_prediction_qa_unrelated"),
        "The recovery explanation revision lost the accepted response, attempts or unrelated preferences");
    view.reset();view.draft(wrong);view.rejected(wrong,true);view.accepted(view.target().answers.front());
    check(state::read_bytes(path)==original_bytes,"Recovery explanation QA changed the authored asset");
    std::cout<<"PASS exact U11L14 recovery explanation proposal: real draft/retry/success, precise reader source, prior accepted payload preserved, scoped Reset and reopen; no asset edits or audio playback claimed\n";
}

void current_literal_predictions(PredictionGroup group,const std::filesystem::path& historical={},bool active_edition=false){
    check(!active_edition||(group==PredictionGroup::prepared_input&&historical.empty()),
        "Active-edition predictions cannot substitute another group or historical fixture");
    const auto& publication=active_edition?active_u2l9_publication:archived_u2l9_publication;
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    std::size_t exact=0,trimmed=0,rejected=0,native_rejected=0,core_only_rejected=0,multiline_forms=0;
    for(const auto& plan:plans){
        if(plan.group!=group)continue;
        check(!active_edition||(plan.edition_identity&&plan.edition_answer),
            "An active U2L9 prediction has no explicitly audited identity and answer");
        const auto identity=active_edition?plan.edition_identity:plan.identity;
        const auto filename=active_edition?publication.filename:plan.filename;
        active_case=std::string(filename)+" / "+identity;active_step="load authored JSON";
        const auto path=historical.empty()?content/filename:historical;
        const auto original=state::read_bytes(path);const auto digest=state::sha256(original);
        if(!historical.empty())check(group==PredictionGroup::prepared_input&&
            digest=="61ff67f148ad78c31036763889ebdb85e31ce315cb1103de516ee975819fd0e1",
            "The U2L9 history fixture is not the exact published d90d3156 source");
        const auto lesson=windows::load_lesson(path);
        if(group==PredictionGroup::prepared_input){
            check(lesson.id==publication.lesson_id,"Prediction QA loaded another U2L9 publication");
            (void)u2l9_publication(lesson);require_active_u2l9_reference_layout(lesson,publication);
        }
        const auto position=curriculum::stable_block_position(lesson,identity);
        check(position.has_value(),"The current literal prediction's persistent identity is missing");
        const auto& block=lesson.pages.at(position->page).blocks.at(position->block);
        check(block.type==curriculum::BlockType::output_prediction&&block.id==plan.activity&&!block.answers.empty(),"The current target is not the registered authored prediction");
        check(block.output_comparison==(plan.exact?curriculum::TextComparison::exact:curriculum::TextComparison::trimmed),"The published prediction comparison policy changed");
        if(active_edition)check(block.answers==std::vector<std::string>{plan.edition_answer},
            "The active U2L9 prediction lost its independently audited exact answer");
        // Native attempts use the loaded answers, independently pinned above
        // for the active edition. Product code and curricular assets stay untouched.
        const auto expected=block.answers.front();check(!expected.empty()&&expected.back()!='\n',"The no-final-break regression oracle changed");
        const bool single_line=curriculum::uses_single_line_answer(block);
        std::cout<<"RUN "<<active_case<<" ["<<(single_line?"single-line":"multiline")<<"]\n"<<std::flush;
        View fixture(lesson,*position,plan.editorial_feedback,group!=PredictionGroup::prepared_input);const auto before=fixture.saved();
        const auto compiler_receipts=u2l9_compiler_receipts(before);
        if(group==PredictionGroup::prepared_input)require_prepared_compiler_credit(before,lesson,*position);
        const auto unrelated=before.preferences.at("exact_prediction_qa_unrelated");fixture.incomplete();
        fixture.draft(plan.internal_error?std::string(plan.internal_error):expected+' ');
        if(plan.exact){
            const std::vector<std::string> variants{
                expected+(single_line?"\n":"\n\n"),
                single_line?expected+"\r\n":crlf_lines(expected)+"\r\n\r\n",
                expected+' ',expected+'\t',' '+expected,'\t'+expected};
            bool first=true;for(const auto& answer:variants){
                if(single_line&&answer.find_first_of("\r\n")!=answer.npos){
                    active_step="evaluate an exact boundary newline outside the single-line input contract";
                    check(!curriculum::evaluate_text_answer(block,answer).correct,"The exact evaluator accepted an added LF/CRLF");
                    ++core_only_rejected;
                }else{fixture.rejected(answer,first);first=false;++native_rejected;}
                ++rejected;
            }
            if(plan.internal_error){
                check(!curriculum::evaluate_text_answer(block,plan.internal_error).correct,
                    "The independently chosen internal mutation is no longer incorrect in the current lesson");
                fixture.rejected(plan.internal_error,true);++rejected;++native_rejected;
            }
            if(single_line){fixture.single_line_breaks(expected+' ');fixture.accepted(expected);}
            else{
                const std::array forms{expected,expected+'\n',crlf_lines(expected),crlf_lines(expected)+"\r\n"};
                bool first_form=true;for(const auto& form:forms){
                    if(!first_form)fixture.reset();first_form=false;
                    check(curriculum::evaluate_text_answer(block,form).correct,
                        "An authored multiline prediction rejected its optional LF/CRLF ending");
                    fixture.accepted(form);++multiline_forms;
                }
            }
            ++exact;
        }else{
            check(plan.internal_error!=nullptr,"Trimmed regression has no independently authored internal mutation");
            fixture.rejected(plan.internal_error,true);++rejected;++native_rejected;
            if(single_line){
                fixture.single_line_breaks(plan.internal_error);
                check(curriculum::evaluate_text_answer(block," \t"+expected+"\t \r\n").correct,
                    "The trimmed evaluator lost its boundary-newline tolerance");
            }
            fixture.accepted(" \t"+expected+(single_line?"\t ":"\t \r\n"));++trimmed;
        }
        const auto saved=fixture.saved();
        check(saved.preferences.at("exact_prediction_qa_unrelated")==unrelated,"Prediction QA altered an unrelated preference");
        if(group==PredictionGroup::prepared_input){
            require_prepared_compiler_credit(saved,lesson,*position);
            check(u2l9_compiler_receipts(saved)==compiler_receipts,
                "Answering a prediction rewrote a real prerequisite receipt");
        }
        else for(const auto& page:lesson.pages)for(const auto& item:page.blocks)
                if(item.type==curriculum::BlockType::cpp_compiler&&item.output_check)
                    check(windows::compiler_puzzle_completed(saved,lesson,item),"Answering a prediction changed a compiler prerequisite receipt");
        fixture.reset();fixture.incomplete();
        if(group==PredictionGroup::prepared_input){
            const auto reset=fixture.saved();
            require_prepared_compiler_credit(reset,lesson,*position);
            check(u2l9_compiler_receipts(reset)==compiler_receipts,
                "Resetting a prediction changed its independent compiler receipts");
        }
        check(state::sha256(state::read_bytes(path))==digest,"Prediction QA changed authored lesson bytes or stateLayout");
        std::cout<<"PASS "<<(plan.exact?"exact ":"trimmed ")<<lesson.id<<" / "<<block.stable_id<<"\n";
    }
    if(group==PredictionGroup::prepared_input){
        check(exact==5&&trimmed==0&&rejected==35&&native_rejected==27&&core_only_rejected==8&&multiline_forms==4,
            "The registered five prepared-input exact predictions are incomplete");
        std::cout<<"PASS five "<<(historical.empty()?publication.label:"authenticated archived")
            <<" U2L9 exact predictions: 27 native character mutations plus 8 exact evaluator newline checks, "
            <<"authored question/source/full guide/success reading regions, unreviewed draft/reopen, "
            <<"single-line Enter preservation, 4 accepted LF/CRLF multiline forms with optional final newline, "
            <<"retry/correct/reopen/Reset, response payload and heart conservation; "
            <<"real preceding compiler receipts remain byte-for-byte intact; no synthetic U2L9 credit or asset edits\n";
    }else{
        check(exact==9&&trimmed==4&&rejected==62&&native_rejected==54&&core_only_rejected==8&&multiline_forms==20,
            "The registered nine-plus-four literal prediction matrix is incomplete");
        std::cout<<"PASS nine current exact predictions and four current trimmed predictions: 54 native character mutations plus 8 exact evaluator newline checks, 20 accepted LF/CRLF multiline forms with optional final newline, current U11L2/U11L11 question/source/full guide/success reading regions, unreviewed draft/reopen, single-line Enter preservation, retry/correct/reopen/Reset, significant internal formatting, response payload preservation, incomplete/draft/correct heart conservation and untouched assets; compiler prerequisites explicitly synthetic\n";
    }
}
}

int main(int argc,char** argv){
    const bool prepared_input=argc==2&&std::string_view(argv[1])=="--prepared-input";
    const bool prepared_history=argc==3&&std::string_view(argv[1])=="--prepared-input-history";
    const bool prepared_active=argc==2&&std::string_view(argv[1])=="--prepared-input-active-edition";
    const bool optional_final_newline=argc==2&&std::string_view(argv[1])=="--optional-final-newline";
    const bool u11_precision=argc==2&&std::string_view(argv[1])=="--u11-precision";
    if(argc!=1&&!prepared_input&&!prepared_history&&!prepared_active&&!optional_final_newline&&!u11_precision){const bool shape=argc==4&&std::string_view(argv[1])=="--prepare-visual"
            &&(std::string_view(argv[2])=="u11l2"||std::string_view(argv[2])=="u11l11"||std::string_view(argv[2])=="u11l11-choice"||std::string_view(argv[2])=="u2l9-stages"||std::string_view(argv[2])=="u2l9-variants"
                ||std::string_view(argv[2])=="u2l9-stages-active-edition"||std::string_view(argv[2])=="u2l9-variants-active-edition");
        const auto raw=shape?std::string_view(argv[3]):std::string_view{};const std::wstring token(raw.begin(),raw.end());
        if(!shape||!is_valid_qa_instance(token)){
            std::cerr<<"Usage: exact_prediction_view_tests [--prepared-input | --prepared-input-history <authentic-json> | --prepared-input-active-edition | --optional-final-newline | --u11-precision | --prepare-visual <u11l2|u11l11|u11l11-choice|u2l9-stages|u2l9-variants|u2l9-stages-active-edition|u2l9-variants-active-edition> <safe-new-QA-token>]\n";return 2;}
        // U2L9 prepares real preceding compiler evidence, without answering
        // the selected prediction or creating a visible lesson view.
        try{winrt::init_apartment();prepare_visual_profile(argv[2],token);return 0;}
        catch(const std::exception& error){std::cerr<<"Visual QA preparation failed: "<<error.what()<<'\n';return 1;}
    }
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{if(optional_final_newline)optional_final_newline_predictions();else if(u11_precision)proposed_u11_recovery_explanation();
        else current_literal_predictions(prepared_input||prepared_history||prepared_active?PredictionGroup::prepared_input:PredictionGroup::baseline,
            prepared_history?std::filesystem::path(argv[2]):std::filesystem::path{},prepared_active);}
    catch(const std::exception& error){std::cerr<<active_case<<" ["<<active_step<<"]: "<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
