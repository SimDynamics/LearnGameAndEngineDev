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
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
std::string active_case,active_step;

void check(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
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
state::Preferences attempts_data(const state::StateData& data){const auto found=data.preferences.find("attempt_reserve_seconds_test");
    return found==data.preferences.end()?state::Preferences{}:found->second;}
std::vector<std::int64_t> stored_order(const std::vector<std::size_t>& order){return {order.begin(),order.end()};}

struct Plan {
    const char* filename;
    const char* identity;
    std::size_t old_pieces,pieces;
    // Independent reviewed allowances from the curricular request, rather than
    // an enumeration produced by the evaluator being tested.
    std::vector<std::vector<std::size_t>> equivalent_groups;
    std::vector<std::pair<std::size_t,std::size_t>> identical_closings;
    std::size_t reviewed_orders=0;
};
const std::array plans{
    Plan{"programas_y_ejecucion/lesson_primer_programa_y_salida.json","block_0003_0002",4,5,{},{}},
    Plan{"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json","block_0004_0002",4,6,{},{}},
    Plan{"objetos_y_estado/lesson_declarar_e_inicializar.json","block_0001_0004",5,6,{},{}},
    Plan{"objetos_y_estado/lesson_estado_asignacion_y_copia.json","block_0003_0002",6,8,{},{}},
    Plan{"objetos_y_estado/lesson_representar_procesar_y_recuperar.json","block_0002_0001",6,9,{{2,3}},{},2},
    Plan{"expresiones_y_decisiones/lesson_valores_expresiones_y_prioridad.json","block_0002_0004",5,8,{},{}},
    Plan{"expresiones_y_decisiones/lesson_alternativas_y_fronteras.json","block_0004_0001",4,10,{},{}},
    Plan{"repeticion_y_terminacion/lesson_acumuladores_contadores_y_trazas.json","block_0004_0001",4,9,{},{}},
    Plan{"colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json","block_0005_0004",7,11,{{2,3},{6,7}},{},4},
    Plan{"funciones_y_contratos/lesson_copia_y_ambito.json","block_0004_0001",4,10,{},{{4,8}}},
    Plan{"funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json","orden_validar_antes_de_calcular",3,8,{{2,3}},{},2},
    Plan{"excepciones_y_recuperacion/lesson_recuperar_por_operacion.json","block_0002_0003",5,19,{{3,4},{6,7},{17,18}},{{9,16}},8},
    Plan{"archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json","block_0004_0003",5,10,{{2,3,4}},{},6},
    Plan{"texto_interpretacion_y_flujos/lesson_una_tabla_persistente_de_nombres.json","block_0004_0002",3,5,{},{}},
};

void seed_compiler_prerequisites(state::StateData& data,const curriculum::Lesson& lesson){
    // These synthetic receipts ONLY isolate the ordering controls and preserve
    // the real activityAfter/compiler gates. They do not claim a compilation.
    // No ordering response, arrangement or achievement is seeded.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check)continue;
        windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
        request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
        for(const auto& item:block.output_check->cases){
            check(!item.outputs.empty(),"A compiler prerequisite has no declared stdout oracle");
            curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
            execution.executable=item.executable;
            if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="Synthetic QA prerequisite diagnostic";
            (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"Synthetic prerequisite only");
        }
        check(windows::compiler_puzzle_completed(data,lesson,block),"Cannot isolate ordering from its compiler prerequisites");
    }
}

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    curriculum::StatePosition position;
    state::Preferences unrelated;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,curriculum::StatePosition target):environment(make_environment()),lesson(std::move(model)),position(target){
        check(!std::filesystem::exists(environment.paths.root),"Curricular ordering QA requires a fresh private profile");
        initialize_profile_directories(environment.paths);
        state::StateData data;state::prepare_lesson_state(data,lesson);seed_compiler_prerequisites(data,lesson);
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["curricular_order_qa_unrelated"]["keep"]=std::string(" \t unrelated preference\r\n");
        unrelated=data.preferences.at("curricular_order_qa_unrelated");
        auto& reserve=data.preferences["attempt_reserve_seconds_test"];
        reserve["reserve_programming"]=8.;reserve["updated_at_programming"]=now_ms();
        reserve["recovery_interval_programming"]=attempts::recovery_millis;
        state::save_current_page(data,lesson,position.page);
        check(!state::get_response(data,lesson,block()),"The target ordering acquired a seeded response");
        state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden curricular ordering QA",WS_OVERLAPPEDWINDOW,
            0,0,1200,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create hidden curricular ordering host");open();
    }
    static WindowsEnvironment make_environment(){static unsigned serial=0;
        return resolve_windows_environment({ProfileId::Codex,L"curricular-order-view-"+std::to_wstring(GetCurrentProcessId())+L"-"
            +std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++serial),false});}
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    const curriculum::Block& block()const{return lesson.pages.at(position.page).blocks.at(position.block);}
    void open(){active_step="open/reopen current authored page";
        view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create current curricular ordering view");
        MoveWindow(view,0,0,1200,900,TRUE);pump();
        check(!IsWindowVisible(host),"Ordering QA exposed a desktop window");
        check(lesson_page_state(view).index==position.page,"The real prerequisite gates redirected the ordering's saved page");
        verify_rows();
    }
    void reopen(){check(flush_lesson_view(view),"Cannot save curricular ordering QA");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush curricular ordering QA");return read_lesson_state(view);}
    HWND control(int part)const{const auto item=descendant(view,control_id(position.block,part));
        check(item!=nullptr,"The target ordering control is missing");return item;}
    HWND row(std::size_t index)const{const auto item=GetDlgItem(control(5),100+static_cast<int>(index));
        check(item!=nullptr,"An independent line has no native row");return item;}
    std::string feedback()const{return edit_text(control(4));}
    std::vector<std::size_t> order()const{
        std::vector<std::pair<LONG,std::size_t>> rows;
        for(std::size_t index=0;index<block().fragments.size();++index){RECT bounds{};
            check(GetWindowRect(row(index),&bounds)!=FALSE,"Cannot inspect actual row geometry");rows.emplace_back(bounds.top,index);}
        std::ranges::sort(rows);std::vector<std::size_t> result;for(const auto& item:rows)result.push_back(item.second);return result;
    }
    void verify_rows()const{
        for(std::size_t index=0;index<block().fragments.size();++index){
            check(block().fragments[index].find_first_of("\r\n")==std::string::npos,"An adapted physical card still combines multiple source lines");
            check(edit_text(row(index))==block().fragments[index],"An independent native row lost source characters or indentation");
        }
        check(GetDlgItem(control(5),100+static_cast<int>(block().fragments.size()))==nullptr,"An unrequested physical source card was added");
    }
    void retained(){const auto data=saved();
        check(data.preferences.at("curricular_order_qa_unrelated")==unrelated,"Ordering changed an unrelated preference");
        // All target responses belong to the refined representation. A new
        // exercise must not invent or overwrite an old grouped achievement.
        check(!state::find(data,"learning_progress",curriculum::exercise_key(lesson,block())),"A new line arrangement overwrote the grouped historical response slot");
        for(const auto& page:lesson.pages)for(const auto& item:page.blocks)
            if(item.type==curriculum::BlockType::cpp_compiler&&item.output_check)
                check(windows::compiler_puzzle_completed(data,lesson,item),"Ordering changed an independent compiler prerequisite receipt");
        check(!IsWindowVisible(host),"A curricular ordering action exposed the hidden QA host");verify_rows();
    }
    void key(std::size_t piece,WPARAM direction){SendMessageW(row(piece),WM_SYSKEYDOWN,direction,LPARAM{1}<<29);pump();}
    void arrange(const std::vector<std::size_t>& desired){active_step="arrange source rows with the accessible keyboard path";
        check(desired.size()==block().fragments.size(),"The UI regression requested an incomplete physical permutation");
        auto indices=desired;std::ranges::sort(indices);
        for(std::size_t index=0;index<indices.size();++index)check(indices[index]==index,"The UI regression requested a duplicate/unknown physical card");
        for(std::size_t target=0;target<desired.size();++target){
            auto current=order();auto found=static_cast<std::size_t>(std::ranges::find(current,desired[target])-current.begin());
            check(found<current.size(),"A physical source card disappeared");
            while(found>target){key(desired[target],VK_UP);--found;}
            check(order()[target]==desired[target],"Keyboard movement failed to reach the requested physical position");
        }
        check(order()==desired,"Keyboard ordering changed the requested reconstructed program");verify_rows();
    }
    static std::vector<std::size_t> adjacent(std::vector<std::size_t> order,std::size_t from,std::size_t to){
        const auto piece=order.at(from);order.erase(order.begin()+static_cast<std::ptrdiff_t>(from));
        order.insert(order.begin()+static_cast<std::ptrdiff_t>(to),piece);return order;
    }
    void each_piece_moves_alone(std::size_t& dragged){
        for(std::size_t index=0;index<block().fragments.size();++index){
            active_step="individual keyboard and sustained drag for piece "+std::to_string(index);
            const auto before=order();const auto from=static_cast<std::size_t>(std::ranges::find(before,index)-before.begin());
            const auto to=from+1<before.size()?from+1:from-1;const auto expected=adjacent(before,from,to);
            const auto handle=row(index);key(index,to>from?VK_DOWN:VK_UP);
            check(order()==expected&&row(index)==handle,"A keyboard move carried another source line or recreated its row");
            key(index,to>from?VK_UP:VK_DOWN);check(order()==before,"Reversing an individual keyboard move changed its neighbors");
            const auto destination=row(before[to]);RECT origin_bounds{},destination_bounds{};
            GetWindowRect(handle,&origin_bounds);GetWindowRect(destination,&destination_bounds);
            SendMessageW(handle,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));
            check(GetCapture()==handle,"The hidden native row did not capture its own drag");
            const auto ready=GetTickCount64()+20;while(GetTickCount64()<ready){MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT);pump();}
            const auto distance=destination_bounds.top-origin_bounds.top+8;
            SendMessageW(handle,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(12,distance));
            SendMessageW(handle,WM_LBUTTONUP,0,MAKELPARAM(12,distance));pump();
            check(order()==expected&&row(index)==handle,"A sustained native drag failed to move only its own source piece");
            check(GetCapture()!=handle&&!(GetWindowLongPtrW(handle,GWL_EXSTYLE)&WS_EX_LAYERED),"Completing a drag retained capture or transient source transparency");
            key(index,to>from?VK_UP:VK_DOWN);check(order()==before,"Restoring a dragged line altered another source piece");++dragged;
        }
        retained();
    }
    void check_incomplete(){active_step="untouched ordering remains unsubmitted";
        check(!IsWindowEnabled(control(3))&&feedback().empty(),"An untouched ordering exposed an active verdict/check");
        const auto before=saved();SendMessageW(control(3),BM_CLICK,0,0);pump();const auto after=saved();
        const auto response=state::get_response(after,lesson,block());
        check(!response||(!response->correct&&!state::response_reviewed(*response,block())),"An unsubmitted ordering became reviewed or solved");
        check(attempts_data(before)==attempts_data(after)&&feedback().empty(),"An unsubmitted ordering consumed a heart or produced a verdict");
    }
    void draft_reopens(){active_step="partial physical arrangement survives reopening";
        const auto before=order();key(before.front(),VK_DOWN);const auto draft=order();
        check(draft!=before&&feedback().empty(),"Moving one row did not create a distinct unreviewed draft");
        const auto response=state::get_response(saved(),lesson,block());
        check(response&&!response->correct&&!state::response_reviewed(*response,block())&&response->order==stored_order(draft),"The physical draft was evaluated or not captured");
        reopen();check(order()==draft&&feedback().empty(),"Reopening changed or reviewed the unsubmitted arrangement");retained();
    }
    void submit(bool correct){active_step=correct?"check a real reconstructed program":"reject a structurally invalid reconstruction";
        check(IsWindowEnabled(control(3)),"A physically edited ordering has no Check action");
        const auto before=saved();SendMessageW(control(3),BM_CLICK,0,0);pump();const auto after=saved();
        const auto response=state::get_response(after,lesson,block());
        check(response&&response->correct==correct&&response->order==stored_order(order()),"The actual Check action stored a different order or verdict");
        check(feedback().starts_with(correct?"✓ Correcto":"✗ Incorrecto"),"The reconstructed program received the wrong native feedback");
        if(correct)check(attempts_data(before)==attempts_data(after),"A correct reconstructed program consumed a heart");
        else check(state::response_reviewed(*response,block()),"An incorrect arrangement lost its reviewed retry state");
        const auto visible=order();const auto verdict=feedback();reopen();
        check(order()==visible&&feedback()==verdict,"Reopening changed the checked arrangement or feedback");
        const auto restored=state::get_response(saved(),lesson,block());check(restored&&restored->correct==correct,"Reopening changed an ordering achievement");retained();
    }
    void reset(){active_step="explicit Reset clears only the target arrangement";
        check(IsWindowEnabled(control(12)),"An edited ordering has no Reset action");
        SendMessageW(control(12),BM_CLICK,0,0);pump();reopen();
        std::vector<std::size_t> initial(block().fragments.size());std::iota(initial.begin(),initial.end(),0);
        check(order()==initial&&feedback().empty(),"Reset revived or changed the target's initial source-card arrangement");check_incomplete();retained();
    }
    void locked(){active_step="an accepted ordering stays locked until Reset";
        const auto before=order();const auto feedback_before=feedback();const auto first=before.front();key(first,VK_DOWN);
        SendMessageW(row(first),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,8));SendMessageW(row(first),WM_LBUTTONUP,0,MAKELPARAM(12,8));pump();
        check(order()==before&&feedback()==feedback_before&&!IsWindowEnabled(control(3)),"A completed ordering moved or lost its verdict without Reset");
    }
};

std::vector<std::vector<std::size_t>> reviewed_variants(const curriculum::Block& block,const Plan& plan){
    std::vector<std::vector<std::size_t>> result{block.correct_order};
    for(auto group:plan.equivalent_groups){std::ranges::sort(group);std::vector<std::vector<std::size_t>> next;
        for(const auto& prior:result){auto permutation=group;
            do{auto variant=prior;for(std::size_t index=0;index<group.size();++index){
                    const auto position=std::ranges::find(prior,group[index]);check(position!=prior.end(),"A reviewed source piece no longer exists");
                    variant[static_cast<std::size_t>(position-prior.begin())]=permutation[index];
                }next.push_back(std::move(variant));
            }while(std::next_permutation(permutation.begin(),permutation.end()));
        }
        result=std::move(next);
    }
    return result;
}

void current_curricular_orderings(std::optional<std::size_t> selected_case){
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    std::size_t groups=0,pieces=0,dragged=0,reviewed=0,additional=0,identical=0,executed=0;
    for(std::size_t case_index=0;case_index<plans.size();++case_index){
        if(selected_case&&*selected_case!=case_index)continue;
        const auto& plan=plans[case_index];active_case=std::string(plan.filename)+" / "+plan.identity;active_step="load current JSON";
        const auto previous_dragged=dragged,previous_reviewed=reviewed,previous_additional=additional,previous_identical=identical;
        std::cout<<"RUN case "<<case_index<<": "<<active_case<<"\n"<<std::flush;
        const auto path=content/plan.filename;const auto digest=state::sha256(state::read_bytes(path));const auto lesson=windows::load_lesson(path);
        const auto position=curriculum::stable_block_position(lesson,plan.identity);check(position.has_value(),"The current ordering lost its registered persistent identity");
        const auto& block=lesson.pages.at(position->page).blocks.at(position->block);
        check(block.type==curriculum::BlockType::order_code&&block.order_refinement&&block.fragments.size()==plan.pieces
            &&block.order_refinement->original_fragments.size()==plan.old_pieces,"The fourteen current assets no longer match the reviewed 65-to-124 split");
        check(block.order_equivalence_groups==plan.equivalent_groups,"A reviewed ordering changed its exact independent-line allowances");
        View fixture(lesson,*position);fixture.check_incomplete();fixture.each_piece_moves_alone(dragged);fixture.draft_reopens();
        auto wrong=block.correct_order;std::swap(wrong.front(),wrong.back());fixture.arrange(wrong);fixture.submit(false);
        fixture.arrange(block.correct_order);check(fixture.feedback().empty(),"Correcting a rejected order retained its obsolete verdict");fixture.submit(true);fixture.locked();
        if(!plan.equivalent_groups.empty()){
            const auto variants=reviewed_variants(block,plan);reviewed+=variants.size();
            for(const auto& variant:variants){if(variant==block.correct_order)continue;
                fixture.reset();fixture.arrange(variant);fixture.submit(true);fixture.locked();++additional;
            }
        }
        for(const auto [first,second]:plan.identical_closings){
            check(block.fragments.at(first)==block.fragments.at(second),"A formerly identical closing source line changed");
            auto equivalent=block.correct_order;std::iter_swap(std::ranges::find(equivalent,first),std::ranges::find(equivalent,second));
            fixture.reset();fixture.arrange(equivalent);fixture.submit(true);fixture.locked();++identical;
        }
        fixture.reset();
        check(state::sha256(state::read_bytes(path))==digest,"Ordering QA changed authored JSON, identifiers or stateLayout");
        check(dragged-previous_dragged==plan.pieces&&reviewed-previous_reviewed==plan.reviewed_orders
            &&additional-previous_additional==(plan.reviewed_orders?plan.reviewed_orders-1:0)
            &&identical-previous_identical==plan.identical_closings.size(),"This activity's movement/equivalence matrix is incomplete");
        groups+=plan.old_pieces;pieces+=plan.pieces;
        ++executed;
        std::cout<<"PASS "<<plan.identity<<": "<<plan.pieces<<" individual keyboard/drag rows, actual draft/wrong/correct/Reset/reopen\n"<<std::flush;
    }
    check(executed==(selected_case?1:plans.size()),"The requested curricular ordering cases were not all executed");
    if(!selected_case)check(groups==65&&pieces==124&&dragged==124&&reviewed==22&&additional==17&&identical==2,
        "The fourteen-activity movement/equivalence matrix is incomplete");
    std::cout<<"PASS "<<executed<<" current intact lesson(s), "<<groups<<" old groups / "<<pieces<<" independent rows, "
        <<dragged<<" sustained native drags, "<<reviewed<<" reviewed orders ("<<additional<<" additional), "<<identical
        <<" identical-closing swaps, actual retry/draft/achievement/Reset persistence, preserved preferences and compiler prerequisites; prerequisites explicitly synthetic; no curricular listening or human visual acceptance claimed\n";
}
}

int main(int argc,char* argv[]){const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{std::optional<std::size_t> selected;
        if(argc!=1){
            check(argc==3&&std::string_view(argv[1])=="--case","Usage: curricular_order_view_tests [--case 0..13]");
            const std::string_view input(argv[2]);std::size_t index=0;
            const auto parsed=std::from_chars(input.data(),input.data()+input.size(),index);
            check(parsed.ec==std::errc{}&&parsed.ptr==input.data()+input.size()&&index<plans.size(),"The case index must be an integer from 0 through 13");
            selected=index;
        }
        current_curricular_orderings(selected);
    }catch(const std::exception& error){std::cerr<<active_case<<" ["<<active_step<<"]: "<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
