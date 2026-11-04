#include "core/attempts.hpp"
#include "core/compile_check_test_fixture.hpp"
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
#include <winrt/Windows.Data.Json.h>
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

enum class PredictionGroup { baseline, prepared_input };
struct Plan {
    const char* filename;
    const char* identity;
    const char* activity;
    bool exact;
    const char* internal_error;
    bool editorial_feedback = false;
    PredictionGroup group = PredictionGroup::baseline;
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
    Plan{"texto_interpretacion_y_flujos/lesson_lineas_y_separadores_pendientes.json","block_0002_0001",
        "",false,"3",true},
    Plan{"texto_interpretacion_y_flujos/lesson_una_gramatica_para_sumar_y_restar.json","block_0006_0002",
        "",false,"Error: Se esperaba ;",true},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0006_0004",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__etapas_lectura_y_actualizacion",true,"Rita 4\nRita: 4",true,PredictionGroup::prepared_input},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0010_0005",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__registrar_tres_datos_y_total",true,"Iris 4 2 6",true,PredictionGroup::prepared_input},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0012_0002",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__distinguir_regla_cuatro",true,"Nora: 11",true,PredictionGroup::prepared_input},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0012_0003",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__cero_paquetes_regla_cuatro",true,"Vega: 0",true,PredictionGroup::prepared_input},
    Plan{"objetos_y_estado/lesson_leer_una_entrada_conocida.json","block_0012_0004",
        "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida__maximo_regla_cuatro",true,"Orion: 103",true,PredictionGroup::prepared_input},
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
void require_no_compiler_credit(const state::StateData& data,const curriculum::Lesson& lesson){
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.type==curriculum::BlockType::cpp_compiler)
            check(windows::compiler_puzzle_case_count(data,lesson,block)==0
                &&!windows::compiler_puzzle_completed(data,lesson,block),
                "Prepared-input prediction QA invented a compiler receipt or achievement");
}

void prepare_visual_profile(std::string_view scenario,std::wstring_view token){
    const bool choice=scenario=="u11l11-choice";
    const bool stages=scenario=="u2l9-stages";
    const bool variants=scenario=="u2l9-variants";
    const bool prepared=stages||variants;
    check((scenario=="u11l2"||scenario=="u11l11"||choice||prepared)&&is_valid_qa_instance(token),
        "Invalid editorial visual QA scenario or unsafe instance token");
    const auto filename=prepared?"lesson_leer_una_entrada_conocida.json"
        :scenario=="u11l2"?"lesson_lineas_y_separadores_pendientes.json"
        :"lesson_una_gramatica_para_sumar_y_restar.json";
    const Plan* selected=nullptr;
    for(const auto& plan:plans)if(std::string_view(plan.filename).ends_with(filename)
        &&(!prepared||std::string_view(plan.identity)==(stages?"block_0006_0004":"block_0012_0002")))selected=&plan;
    check(selected&&selected->editorial_feedback,"The visual target has no current editorial regression");
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/selected->filename;
    const auto asset_hash=state::sha256(state::read_bytes(path));const auto lesson=windows::load_lesson(path);
    const std::string_view identity=choice?"block_0002_0002":selected->identity;
    const auto position=curriculum::stable_block_position(lesson,identity);
    check(position&&position->page==(variants?12u:scenario=="u11l2"||choice?2u:6u)&&position->block==(scenario=="u11l2"?1u:2u),
        "The selected visual activity moved outside its audited page");
    const auto& target=lesson.pages[position->page].blocks[position->block];
    check(choice?target.type==curriculum::BlockType::multiple_choice&&target.correct_index==0&&target.options.size()==3
        :target.type==curriculum::BlockType::output_prediction
            &&target.output_comparison==(prepared?curriculum::TextComparison::exact:curriculum::TextComparison::trimmed),
        "The selected visual activity lost its published contract");
    if(prepared)check(target.id==lesson.id+(stages?"__etapas_lectura_y_actualizacion":"__distinguir_regla_cuatro")
        &&curriculum::uses_single_line_answer(target)==variants&&!lesson.compiler_puzzles_required,
        "The current U2L9 prediction lost its independent exact answer contract");
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
    if(prepared)require_no_compiler_credit(data,lesson);
    state::save_current_page(data,lesson,position->page);auto& preferences=data.preferences["native_windows"];
    preferences["ui_language"]=std::string("es");preferences["content_language"]=std::string("es");
    preferences["catalog/locale"]=std::string("es");preferences["catalog/route"]=std::string("programming");
    preferences["catalog/course"]=std::string("cpp_fundamentals");
    const auto unit_end=lesson.id.rfind("__");check(unit_end!=std::string::npos,"The selected lesson has no catalog unit identity");
    preferences["catalog/unit"]=lesson.id.substr(0,unit_end);preferences["navigation/lesson"]=lesson.id;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        check(!state::get_response(data,lesson,block),"Preparing visual QA invented a quiz response or achievement");
    state::validate_lesson_state(data,lesson);store.save(data);const auto saved=store.load();
    check(saved==data&&state::current_page(saved,lesson)==position->page&&!state::get_response(saved,lesson,target),
        "The prepared visual QA profile did not preserve the unanswered activity");
    if(prepared)require_no_compiler_credit(saved,lesson);
    check(state::sha256(state::read_bytes(path))==asset_hash,"Preparing visual QA changed an authored asset");
    std::cout<<"profile=Codex\nqa_instance="<<utf8(token)<<"\ndata_root="<<utf8(environment.paths.root.native())
        <<"\nlesson="<<lesson.id<<"\ntarget="<<identity<<"\npage="<<position->page+1
        <<"\npage_index="<<position->page<<"\nblock="<<position->block+1
        <<"\nsynthetic_prerequisites="<<(scenario=="u11l11"?"block_0004_0003-only-not-a-real-execution":"none")
        <<"\nquiz_responses=none\ntarget_completed=0\nwindows_created=0\n";
}

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    curriculum::StatePosition position;
    bool editorial_feedback = false;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,curriculum::StatePosition target,bool verify_editorial=false,bool seed_prerequisites=true)
        :environment(resolve_windows_environment({ProfileId::Codex,
            L"exact-prediction-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),
         lesson(std::move(model)),position(target),editorial_feedback(verify_editorial){
        check(!std::filesystem::exists(environment.paths.root),"Literal prediction QA requires a fresh private profile");
        initialize_profile_directories(environment.paths);
        state::StateData data;state::prepare_lesson_state(data,lesson);
        if(seed_prerequisites)seed_compiler_prerequisites(data,lesson);
        else{
            check(!lesson.compiler_puzzles_required&&curriculum::activity_prerequisites(lesson,this->target()).empty(),
                "The prepared-input prediction acquired an unaudited compiler prerequisite");
            require_no_compiler_credit(data,lesson);
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
    void open(){active_step="open/reopen the authored page";view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create the actual prediction lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Prediction QA exposed a desktop window");
        check(lesson_page_state(view).index==position.page,"The prediction did not restore its actual authored page");
        if(editorial_feedback){auto question=target().question;std::erase(question,'`');
            check(text(0)==question&&text(1)==target().code,"The current editorial question or displayed program changed on open/reopen");}
    }
    void reopen(){check(flush_lesson_view(view),"Cannot save prediction QA");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush prediction QA");return read_lesson_state(view);}
    HWND control(int part)const{const auto result=descendant(view,control_id(position.block,part));
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
    void accepted(std::string_view answer){
        replace(answer);check(text(4).empty(),"The corrected draft still shows the rejected verdict");
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

void current_literal_predictions(PredictionGroup group){
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    std::size_t exact=0,trimmed=0,rejected=0,native_rejected=0,core_only_rejected=0;
    for(const auto& plan:plans){
        if(plan.group!=group)continue;
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
        View fixture(lesson,*position,plan.editorial_feedback,group!=PredictionGroup::prepared_input);const auto before=fixture.saved();
        if(group==PredictionGroup::prepared_input)require_no_compiler_credit(before,lesson);
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
        if(group==PredictionGroup::prepared_input)require_no_compiler_credit(saved,lesson);
        else for(const auto& page:lesson.pages)for(const auto& item:page.blocks)
                if(item.type==curriculum::BlockType::cpp_compiler&&item.output_check)
                    check(windows::compiler_puzzle_completed(saved,lesson,item),"Answering a prediction changed a compiler prerequisite receipt");
        fixture.reset();fixture.incomplete();
        if(group==PredictionGroup::prepared_input)require_no_compiler_credit(fixture.saved(),lesson);
        check(state::sha256(state::read_bytes(path))==digest,"Prediction QA changed authored lesson bytes or stateLayout");
        std::cout<<"PASS "<<(plan.exact?"exact ":"trimmed ")<<lesson.id<<" / "<<block.stable_id<<"\n";
    }
    if(group==PredictionGroup::prepared_input){
        check(exact==5&&trimmed==0&&rejected==35&&native_rejected==27&&core_only_rejected==8,
            "The registered five prepared-input exact predictions are incomplete");
        std::cout<<"PASS five current U2L9 exact predictions: 27 native character mutations plus 8 exact evaluator newline checks, authored question/source/full guide/success reading regions, unreviewed draft/reopen, single-line Enter preservation, retry/correct/reopen/Reset, response payload preservation, incomplete/draft/correct heart conservation and untouched assets; no compiler prerequisites, receipts or achievements\n";
    }else{
        check(exact==9&&trimmed==4&&rejected==62&&native_rejected==54&&core_only_rejected==8,
            "The registered nine-plus-four literal prediction matrix is incomplete");
        std::cout<<"PASS nine current exact predictions and four current trimmed predictions: 54 native character mutations plus 8 exact evaluator newline checks, current U11L2/U11L11 question/source/full guide/success reading regions, unreviewed draft/reopen, single-line Enter preservation, retry/correct/reopen/Reset, significant internal formatting, response payload preservation, incomplete/draft/correct heart conservation and untouched assets; compiler prerequisites explicitly synthetic\n";
    }
}
}

int main(int argc,char** argv){
    const bool prepared_input=argc==2&&std::string_view(argv[1])=="--prepared-input";
    const bool u11_precision=argc==2&&std::string_view(argv[1])=="--u11-precision";
    if(argc!=1&&!prepared_input&&!u11_precision){const bool shape=argc==4&&std::string_view(argv[1])=="--prepare-visual"
            &&(std::string_view(argv[2])=="u11l2"||std::string_view(argv[2])=="u11l11"||std::string_view(argv[2])=="u11l11-choice"||std::string_view(argv[2])=="u2l9-stages"||std::string_view(argv[2])=="u2l9-variants");
        const auto raw=shape?std::string_view(argv[3]):std::string_view{};const std::wstring token(raw.begin(),raw.end());
        if(!shape||!is_valid_qa_instance(token)){
            std::cerr<<"Usage: exact_prediction_view_tests [--prepared-input | --u11-precision | --prepare-visual <u11l2|u11l11|u11l11-choice|u2l9-stages|u2l9-variants> <safe-new-QA-token>]\n";return 2;}
        // Preparation performs JSON/state/file operations only. Native control
        // and apartment initialization belong solely to the normal regression.
        try{prepare_visual_profile(argv[2],token);return 0;}
        catch(const std::exception& error){std::cerr<<"Visual QA preparation failed: "<<error.what()<<'\n';return 1;}
    }
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{if(u11_precision)proposed_u11_recovery_explanation();
        else current_literal_predictions(prepared_input?PredictionGroup::prepared_input:PredictionGroup::baseline);}
    catch(const std::exception& error){std::cerr<<active_case<<" ["<<active_step<<"]: "<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
