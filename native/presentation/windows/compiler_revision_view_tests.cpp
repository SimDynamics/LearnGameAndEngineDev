#include "core/compiler_revision_test_fixture.hpp"
#include "core/attempts.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& search=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=search.id)return TRUE;search.found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));return search.found;}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,const state::StateData& initial):environment(resolve_windows_environment({ProfileId::Codex,
        L"compiler-revision-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"Compiler revision QA must own a new profile");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);(void)store.load();store.save(initial);
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Hidden compiler revision QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the hidden compiler revision host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create a compiler revision lesson");
        MoveWindow(view,0,0,1100,850,TRUE);pump();check(!IsWindowVisible(host),"Compiler revision QA exposed the desktop");}
    void reopen(){check(flush_lesson_view(view),"Cannot save compiler revision QA");DestroyWindow(view);view=nullptr;open();}
    void restore(const state::StateData& next){
        check(flush_lesson_view(view),"Cannot close the prior QA state");DestroyWindow(view);view=nullptr;
        state::StateStore store(environment.paths.root);(void)store.load();store.save(next);open();
    }
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush compiler revision QA");return read_lesson_state(view);}
    HWND control(std::size_t block,int part){const auto item=descendant(view,control_id(block,part));check(item!=nullptr,"Missing compiler revision control");return item;}
    std::string text(std::size_t block,int part){return edit_text(control(block,part));}
    void reset(std::size_t block){SendMessageW(control(block,12),BM_CLICK,0,0);pump();}
};
windows::CppRunRequest program(const curriculum::Block& block,std::string source){
    windows::CppRunRequest request;request.revision=1;request.source_utf8=std::move(source);
    request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;return request;
}
void seed(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
          const windows::CppRunRequest& request,std::size_t count,std::string_view feedback){
    // These synthetic receipts isolate UI/state adoption. Real reference and
    // counterexample programs are compiled separately; no execution is claimed here.
    for(std::size_t index=0;index<std::min(count,block.output_check->cases.size());++index){const auto& item=block.output_check->cases[index];
        curriculum::FreshExecution execution{request.revision,true,true,0,item.standard_input,item.outputs.front()};
        execution.executable=item.executable;
        if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA runtime diagnostic";
        (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,feedback);
    }
}
void compiler_revisions_keep_prior_progress(){
    struct Plan{const char* filename;const char* identity;bool append;};
    const Plan plans[]{
        {"lesson_construir_un_lector_de_entrada_preparada.json","block_0003_0005",true},
        {"lesson_tokens_con_categoria_y_valor.json","block_0004_0004",false},
        {"lesson_numeros_de_varios_digitos_y_limites.json","block_0004_0003",false},
        {"lesson_una_gramatica_para_sumar_y_restar.json","block_0004_0003",false},
        {"lesson_terminos_prioridad_y_division_segura.json","block_0004_0003",false},
        {"lesson_signos_unarios_parentesis_y_progreso_recursivo.json","block_0004_0004",false},
        {"lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json","block_0004_0004",false}};
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    std::size_t scenarios=0;
    for(const auto& plan:plans){const auto path=folder/plan.filename;const auto original_hash=state::sha256(state::read_bytes(path));
        const auto integrated=windows::load_lesson(path);
        const auto original=test::compiler_revision_before(integrated,plan.identity);
        const auto revised=test::revise_compiler_lesson(original,plan.identity);
        const auto position=curriculum::stable_block_position(original,plan.identity);check(position.has_value(),"The reviewed compiler identity is missing");
        const auto& old=original.pages[position->page].blocks[position->block];const auto& current=revised.pages[position->page].blocks[position->block];
        for(const std::string mode:{"empty","partial","complete"}){
            if(mode=="partial"&&old.output_check->cases.size()==1)continue;
            state::StateData data;state::prepare_lesson_state(data,original);
            data.preferences["native_windows"]["ui_language"]=std::string("es");
            data.preferences["independent_revision_preferences"]["untouched"]=std::string(" \t original metadata\r\n");
            for(const auto& page:original.pages)for(const auto& block:page.blocks)
                if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check&&block.stable_id!=old.stable_id)
                    seed(data,original,block,program(block,block.initial_code),block.output_check->cases.size(),"QA prerequisite receipt");
            const auto saved_source=old.initial_code+"\n// Programa personal QA conservado\n";
            const auto captured=program(old,saved_source);
            const auto old_count=mode=="empty"?0:mode=="partial"?std::max<std::size_t>(1,old.output_check->cases.size()/2):old.output_check->cases.size();
            seed(data,original,old,captured,old_count,"Correcto\nComprobación anterior QA conservada.");
            const auto old_complete=old_count==old.output_check->cases.size();
            state::Draft draft;draft.source=saved_source;draft.input=" \t dato personal\n";draft.selection_start=4;draft.selection_end=7;
            const auto draft_key=curriculum::draft_key("programming",original,old);state::put_draft(data,draft_key,draft);
            state::save_current_page(data,original,position->page);
            const auto original_preferences=data.preferences["independent_revision_preferences"];
            const auto available=attempts::balance(data,"programming",now_ms()).available;
            View fixture(revised,data);check(lesson_page_state(fixture.view).index==position->page,"Adoption moved the requested compiler bookmark");
            const auto verify=[&]{const auto saved=fixture.saved();
                check(!windows::compiler_puzzle_completed(saved,revised,current),"An old achievement silently completed the expanded objective");
                check(windows::compiler_puzzle_completed(saved,original,old)==old_complete,"Adoption rewrote the historical achievement");
                check(windows::compiler_puzzle_historical_case_count(saved,revised,current)==old_count,"The previous checks lost their provenance");
                check(windows::compiler_puzzle_case_count(saved,revised,current)==(plan.append?old_count:0),"The wrong checks were credited to the expanded objective");
                const auto retained=state::get_draft(saved,draft_key,"");
                check(retained.source==saved_source&&retained.input==draft.input,"Adoption lost personal source or prepared input");
                check(saved.preferences.at("independent_revision_preferences")==original_preferences,"Compiler adoption changed an unrelated preference");
                check(attempts::balance(saved,"programming",now_ms()).available==available,"Compiler adoption consumed an attempt");
                const auto summary=fixture.text(position->block,40);
                check(summary.empty()==(old_count==0),"The view lost or invented historical checks");
                check(fixture.text(position->block,4).empty(),"An old verdict was presented as the current objective verdict");
                if(old_count){check(summary.find("ampliación")!=summary.npos,"Historical credit lacks its pending expansion label");
                    bool found=false;for(const auto& region:lesson_reading_regions(fixture.view))if(region.source_text==summary){
                        found=true;check(!region.solved&&region.required_compiler,"Historical narration unlocked the current compiler");}
                    check(found,"The reader omitted the retained historical compiler achievement");}
            };
            verify();fixture.reopen();verify();
            auto fresh=fixture.saved();
            seed(fresh,revised,current,program(current,saved_source+"// Solución ampliada QA\n"),current.output_check->cases.size(),"Correcto\nNueva batería QA completa.");
            fixture.restore(fresh);const auto completed=fixture.saved();
            check(windows::compiler_puzzle_completed(completed,revised,current),"New checks did not complete their own expanded objective");
            check(fixture.text(position->block,40).empty()&&fixture.text(position->block,4).starts_with("✓ Correcto"),"The completed expansion kept a pending label or lost its own verdict");
            check(windows::compiler_puzzle_completed(completed,original,old)==old_complete,"Completing the expansion rewrote old achievement data");
            fixture.reset(position->block);fixture.reopen();const auto reset=fixture.saved();
            check(!windows::compiler_puzzle_completed(reset,revised,current)&&!windows::compiler_puzzle_completed(reset,original,old)
                &&windows::compiler_puzzle_case_count(reset,revised,current)==0&&fixture.text(position->block,40).empty(),
                "Explicit reset resurrected historical checks on reopening");
            check(state::get_draft(reset,draft_key,"").source==current.initial_code,"Reset did not restore the original exercise source");
            check(reset.preferences.at("independent_revision_preferences")==original_preferences,"Reset changed unrelated preferences");++scenarios;
        }
        check(state::sha256(state::read_bytes(path))==original_hash,"A compiler revision UI regression changed an authored asset");
    }
    std::cout<<"PASS "<<scenarios<<" hidden real compiler views: seven exact in-memory expansions, prior empty/partial/complete receipts, same-program projection, current gate, original drafts/preferences, reader history, completion, Reset and reopen (synthetic QA receipts, no compilation claimed)\n";
}
}
int main(){const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{compiler_revisions_keep_prior_progress();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
