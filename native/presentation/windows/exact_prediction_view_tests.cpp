#include "core/attempts.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
std::string active_case,active_step;

void check(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& current=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=current.id)return TRUE;current.found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));
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
state::Preferences attempt_preferences(const state::StateData& data){
    const auto found=data.preferences.find("attempt_reserve_seconds_test");
    return found==data.preferences.end()?state::Preferences{}:found->second;
}

struct Plan {
    const char* filename;
    const char* identity;
    const char* activity;
    bool exact;
    const char* internal_error;
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
    Plan{"archivos_y_programa_completo/lesson_ancho_relleno_y_alineacion_de_campos.json","block_1_1","",false,"[ 8]|[9]"},
    Plan{"programas_y_ejecucion/lesson_primer_programa_y_salida.json","block_0004_0003",
        "prediccion_zona_inicial",true,"Zonainicial"},
    Plan{"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json","block_0006_0003",
        "prediccion_informe_inicial",true,"Resumen Zona Oeste"},
    Plan{"programas_y_ejecucion/lesson_fuente_compilacion_y_diagnostico.json","block_0003_0005",
        "prediccion_revision_inicial",true,"Revisión inicial"},
    Plan{"objetos_y_estado/lesson_enteros_y_aproximaciones.json","block_0007_0005",
        "cpp_fundamentals__objetos_y_estado__enteros_y_aproximaciones__prediccion_reparto_entero",true,"Parte:4 m"},
};

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
            curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
            execution.executable=item.executable;
            if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA prerequisite diagnostic";
            (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"Synthetic QA prerequisite only");
        }
        check(windows::compiler_puzzle_completed(data,lesson,block),"Cannot isolate prediction from its compiler prerequisites");
    }
}

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    curriculum::StatePosition position;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,curriculum::StatePosition target)
        :environment(resolve_windows_environment({ProfileId::Codex,
            L"exact-prediction-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),
         lesson(std::move(model)),position(target){
        check(!std::filesystem::exists(environment.paths.root),"Literal prediction QA requires a fresh private profile");
        initialize_profile_directories(environment.paths);
        state::StateData data;state::prepare_lesson_state(data,lesson);seed_compiler_prerequisites(data,lesson);
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
    void open(){active_step="open/reopen the authored page";view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create the actual prediction lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Prediction QA exposed a desktop window");
        check(lesson_page_state(view).index==position.page,"The prediction did not restore its actual authored page");
    }
    void reopen(){check(flush_lesson_view(view),"Cannot save prediction QA");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush prediction QA");return read_lesson_state(view);}
    HWND control(int part)const{const auto result=descendant(view,control_id(position.block,part));
        check(result!=nullptr,"The authored prediction control is missing");return result;}
    std::string text(int part)const{return edit_text(control(part));}
    void replace(std::string_view value){active_step="replace the native answer";
        check(!curriculum::uses_single_line_answer(target())||value.find_first_of("\r\n")==value.npos,
            "The QA fixture must not force a multiline draft into a single-line control");
        const auto answer=control(2);check(IsWindowEnabled(answer),"Prediction answer is unexpectedly locked");
        CHARRANGE all{0,-1};SendMessageW(answer,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));const auto source=wide(value);
        SendMessageW(answer,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(source.c_str()));pump();
        // RichEdit presents CRLF paragraph breaks as LF. Both incoming forms
        // still add a real final break and must fail an exact no-break answer.
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
        if(reopen_after){reopen();check(text(4)==feedback&&text(2)==native_newlines(answer),"Reopening lost a rejected literal answer or its retry feedback");}
    }
    void accepted(std::string_view answer){
        replace(answer);check(text(4).empty(),"The corrected draft still shows the rejected verdict");
        const auto before=saved();submit();const auto feedback=text(4);
        check(feedback.starts_with("✓ Correcto")&&!IsWindowEnabled(control(2))&&!IsWindowEnabled(control(3)),"A valid literal prediction was rejected or did not lock");
        const auto after=saved();const auto response=state::get_response(after,lesson,target());
        check(response&&response->correct&&native_newlines(response->answer)==native_newlines(answer),"Correct prediction state changed the submitted answer");
        check(attempt_preferences(before)==attempt_preferences(after),"An accepted prediction consumed a heart");
        reopen();const auto restored=saved();const auto restored_answer=state::get_response(restored,lesson,target());
        check(text(4)==feedback&&text(2)==native_newlines(answer)&&!IsWindowEnabled(control(2))
            &&restored_answer&&restored_answer->correct,"A correct prediction did not retain its answer, lock and feedback on reopen");
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

void current_literal_predictions(){
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    std::size_t exact=0,trimmed=0,rejected=0,native_rejected=0,core_only_rejected=0;
    for(const auto& plan:plans){
        active_case=std::string(plan.filename)+" / "+plan.identity;active_step="load current JSON";
        const auto path=content/plan.filename;const auto original=state::read_bytes(path);const auto digest=state::sha256(original);
        const auto lesson=windows::load_lesson(path);const auto position=curriculum::stable_block_position(lesson,plan.identity);
        check(position.has_value(),"The current literal prediction's persistent identity is missing");
        const auto& block=lesson.pages.at(position->page).blocks.at(position->block);
        check(block.type==curriculum::BlockType::output_prediction&&block.id==plan.activity&&!block.answers.empty(),"The current target is not the registered authored prediction");
        check(block.output_comparison==(plan.exact?curriculum::TextComparison::exact:curriculum::TextComparison::trimmed),"The published prediction comparison policy changed");
        // Expected answers come only from the loaded lesson contract. This
        // test never copies an answer into product code or curricular assets.
        const auto expected=block.answers.front();check(!expected.empty()&&expected.back()!='\n',"The no-final-break regression oracle changed");
        const bool single_line=curriculum::uses_single_line_answer(block);
        std::cout<<"RUN "<<active_case<<" ["<<(single_line?"single-line":"multiline")<<"]\n"<<std::flush;
        View fixture(lesson,*position);const auto before=fixture.saved();
        const auto unrelated=before.preferences.at("exact_prediction_qa_unrelated");fixture.incomplete();
        fixture.draft(plan.internal_error?std::string(plan.internal_error):expected+' ');
        if(plan.exact){
            const std::vector<std::string> variants{expected+'\n',expected+"\r\n",expected+' ',expected+'\t',' '+expected,'\t'+expected};
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
            if(single_line)fixture.single_line_breaks(expected+' ');
            fixture.accepted(expected);++exact;
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
        for(const auto& page:lesson.pages)for(const auto& item:page.blocks)
            if(item.type==curriculum::BlockType::cpp_compiler&&item.output_check)
                check(windows::compiler_puzzle_completed(saved,lesson,item),"Answering a prediction changed a compiler prerequisite receipt");
        fixture.reset();fixture.incomplete();
        check(state::sha256(state::read_bytes(path))==digest,"Prediction QA changed authored lesson bytes or stateLayout");
        std::cout<<"PASS "<<(plan.exact?"exact ":"trimmed ")<<lesson.id<<" / "<<block.stable_id<<"\n";
    }
    check(exact==9&&trimmed==2&&rejected==60&&native_rejected==52&&core_only_rejected==8,
        "The registered nine-plus-two literal prediction matrix is incomplete");
    std::cout<<"PASS nine current exact predictions and two current trimmed predictions: 52 native character mutations plus 8 exact evaluator newline checks, unreviewed draft/reopen, single-line Enter preservation, retry/correct/reopen/Reset, significant internal formatting, incomplete/draft/correct heart conservation and untouched assets; compiler prerequisites explicitly synthetic\n";
}
}

int main(){const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{current_literal_predictions();}catch(const std::exception& error){std::cerr<<active_case<<" ["<<active_step<<"]: "<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
