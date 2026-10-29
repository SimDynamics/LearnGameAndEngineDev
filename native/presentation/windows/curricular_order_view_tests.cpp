#include "core/attempts.hpp"
#include "core/narration_state.hpp"
#include "core/view_preferences.hpp"
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
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <optional>
#include <map>
#include <set>
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

void seed_compiler_prerequisites(state::StateData& data,const curriculum::Lesson& lesson,
    const std::set<std::string>* selected=nullptr){
    // These synthetic receipts ONLY isolate the ordering controls and preserve
    // the real activityAfter/compiler gates. They do not claim a compilation.
    // No ordering response, arrangement or achievement is seeded.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check
            ||(selected&&!selected->contains(block.stable_id)))continue;
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


// Preparation and inspection never create a native window or dispatch a test
// action. Real ordering input is owned by the separately launched QA app.
std::size_t visual_case_index(std::string_view input){
    std::size_t index=0;const auto parsed=std::from_chars(input.data(),input.data()+input.size(),index);
    check(parsed.ec==std::errc{}&&parsed.ptr==input.data()+input.size()&&index<plans.size(),
        "The visual case index must be an integer from 0 through 13");return index;
}
std::filesystem::path visual_asset(const Plan& plan){
    return std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals"/plan.filename;
}
std::set<std::string> visual_prerequisites(const curriculum::Lesson& lesson,curriculum::StatePosition target){
    std::set<std::string> result;
    for(std::size_t page=0;page<=target.page;++page)for(const auto& block:lesson.pages.at(page).blocks){
        if(page<target.page&&curriculum::has_compiler_objective(block)
            &&(lesson.compiler_puzzles_required||curriculum::compiler_is_puzzle_prerequisite(lesson,block)))result.insert(block.stable_id);
        // Restoring a later page checks all of its declared activity owners,
        // including the ones used by neighboring puzzles on that same page.
        for(const auto at:curriculum::activity_prerequisites(lesson,block))
            result.insert(lesson.pages.at(at.page).blocks.at(at.block).stable_id);
    }
    for(bool changed=true;changed;){changed=false;const auto prior=result;
        for(const auto& identity:prior){const auto at=curriculum::stable_block_position(lesson,identity);
            check(at.has_value(),"A visual prerequisite lost its authored identity");
            const auto& block=lesson.pages.at(at->page).blocks.at(at->block);
            check(block.type==curriculum::BlockType::cpp_compiler&&block.output_check,
                "Visual preparation cannot invent an unimplemented compiler prerequisite");
            for(const auto dependency:curriculum::activity_prerequisites(lesson,block))
                changed=result.insert(lesson.pages.at(dependency.page).blocks.at(dependency.block).stable_id).second||changed;
        }
    }
    return result;
}
WindowsEnvironment visual_environment(std::wstring_view token){
    check(is_valid_qa_instance(token),"Unsafe visual ordering QA token");
    const auto environment=resolve_windows_environment({ProfileId::Codex,std::wstring(token),false});
    check(environment.paths.root.parent_path().filename()==L"qa"
        &&environment.paths.root.parent_path().parent_path().filename()==L"Codex"
        &&environment.paths.root.filename()==token,"Visual ordering must stay in its exact isolated Codex QA directory");
    state::require_safe_path(environment.paths.root);return environment;
}
void check_visual_contract(const curriculum::Lesson& lesson,const Plan& plan,curriculum::StatePosition at){
    const auto& target=lesson.pages.at(at.page).blocks.at(at.block);
    check(target.type==curriculum::BlockType::order_code&&target.order_refinement
        &&target.fragments.size()==plan.pieces&&target.order_refinement->original_fragments.size()==plan.old_pieces
        &&target.order_equivalence_groups==plan.equivalent_groups,"The visual ordering no longer matches its audited refinement");
    for(const auto& fragment:target.fragments)check(fragment.find_first_of("\r\n")==std::string::npos,
        "The visual ordering still contains a physical piece with multiple source lines");
}
void prepare_visual_profile(std::size_t index,std::wstring_view token){
    const auto& plan=plans.at(index);const auto asset=visual_asset(plan);const auto asset_hash=state::sha256(state::read_bytes(asset));
    const auto lesson=windows::load_lesson(asset);const auto at=curriculum::stable_block_position(lesson,plan.identity);
    check(at.has_value(),"The visual ordering target lost its stable identity");check_visual_contract(lesson,plan,*at);
    const auto prerequisites=visual_prerequisites(lesson,*at);const auto environment=visual_environment(token);
    check(!std::filesystem::exists(environment.paths.root),"Visual ordering refuses to reuse or overwrite an existing profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    check(std::filesystem::create_directory(environment.paths.root),"Another process reserved the visual QA profile");
    initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);
    check(store.load().preferences.empty(),"The newly reserved visual ordering QA already contains state");
    state::StateData data;state::prepare_lesson_state(data,lesson);seed_compiler_prerequisites(data,lesson,&prerequisites);
    state::save_current_page(data,lesson,at->page);auto& preferences=data.preferences["native_windows"];
    preferences["ui_language"]=std::string("es");preferences["content_language"]=std::string("es");
    preferences["catalog/locale"]=std::string("es");preferences["catalog/route"]=std::string("programming");
    preferences["catalog/course"]=std::string("cpp_fundamentals");
    const auto separator=lesson.id.rfind("__");check(separator!=lesson.id.npos,"The visual ordering has no catalog unit identity");
    preferences["catalog/unit"]=lesson.id.substr(0,separator);preferences["navigation/lesson"]=lesson.id;
    data.preferences["lesson_narration_settings"]["kokoro_advance_generation"]=false;
    auto& provenance=data.preferences["curricular_order_visual_qa"];
    provenance["version"]=std::int64_t{1};provenance["case_index"]=static_cast<std::int64_t>(index);provenance["asset_sha256"]=asset_hash;
    provenance["target"]=std::string(plan.identity);provenance["synthetic_prerequisites"]=std::vector<std::string>(prerequisites.begin(),prerequisites.end());
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        check(!state::get_response(data,lesson,block),"Visual preparation invented a quiz response");
        if(block.type==curriculum::BlockType::cpp_compiler){
            check(!state::get_draft(data,curriculum::draft_key("programming",lesson,block),"").has_saved,
                "Visual preparation invented a compiler draft");
            check(windows::compiler_puzzle_completed(data,lesson,block)==prerequisites.contains(block.stable_id),
                "Visual preparation credited an unrelated compiler objective");
        }
    }
    check(narration::read_history(data).entries.empty(),"Visual preparation invented a reading bookmark");
    state::validate_lesson_state(data,lesson);store.save(data);check(store.load()==data,"Saving visual ordering changed its prepared state");
    check(state::sha256(state::read_bytes(asset))==asset_hash,"Visual preparation modified the curricular asset");
    std::cout<<"profile=Codex\nqa_instance="<<utf8(token)<<"\ndata_root="<<utf8(environment.paths.root.native())
        <<"\ncase_index="<<index<<"\nlesson="<<lesson.id<<"\nasset_sha256="<<asset_hash<<"\ntarget="<<plan.identity
        <<"\npage="<<at->page+1<<"\npage_index="<<at->page<<"\nblock="<<at->block+1<<"\npieces="<<plan.pieces
        <<"\nsynthetic_prerequisites=";for(const auto& identity:prerequisites)std::cout<<identity<<',';
    std::cout<<"\ncompiler_executions=none\nquiz_responses=none\ndrafts=none\nreading_history=none\nwindows_created=0\n";
}
std::map<std::filesystem::path,std::string> visual_hashes(const std::filesystem::path& folder){
    state::require_safe_path(folder);check(std::filesystem::is_directory(folder),"The visual QA active state directory is absent");
    std::map<std::filesystem::path,std::string> result;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(state::native_io_path(folder))){
        state::require_safe_path(entry.path());if(entry.is_regular_file())
            result.emplace(std::filesystem::relative(entry.path(),state::native_io_path(folder)),state::sha256(state::read_bytes(entry.path())));
    }return result;
}
void verify_visual_profile(std::size_t index,std::wstring_view token){
    const auto environment=visual_environment(token);const auto& plan=plans.at(index);const auto asset=visual_asset(plan);
    const auto asset_hash=state::sha256(state::read_bytes(asset));const auto lesson=windows::load_lesson(asset);
    const auto at=curriculum::stable_block_position(lesson,plan.identity);check(at.has_value(),"The visual verifier lost its authored target");
    check_visual_contract(lesson,plan,*at);const auto active=environment.paths.root/L"state/active";
    const auto hashes_before=visual_hashes(active);check(hashes_before.contains("state.bin"),"The verifier requires an existing exact QA state");
    state::StateStore store(environment.paths.root);const auto data=store.load();
    check(state::integer_value(data,"curricular_order_visual_qa","version")==1
        &&state::integer_value(data,"curricular_order_visual_qa","case_index",-1)==static_cast<std::int64_t>(index)
        &&state::string_value(data,"curricular_order_visual_qa","asset_sha256")==asset_hash
        &&state::string_value(data,"curricular_order_visual_qa","target")==plan.identity,
        "The verifier refuses an unrelated QA profile or a changed authored asset");
    state::validate_lesson_state(data,lesson);const auto prerequisites=visual_prerequisites(lesson,*at);
    const auto stored=state::find(data,"curricular_order_visual_qa","synthetic_prerequisites");
    check(stored&&std::holds_alternative<std::vector<std::string>>(*stored)
        &&std::get<std::vector<std::string>>(*stored)==std::vector<std::string>(prerequisites.begin(),prerequisites.end()),
        "The verifier found different synthetic prerequisite provenance");
    const auto& target=lesson.pages.at(at->page).blocks.at(at->block);const auto response=state::get_response(data,lesson,target);
    check(state::current_page(data,lesson)==at->page,"The visual QA no longer restores the selected ordering page");
    check(!state::find(data,"learning_progress",curriculum::exercise_key(lesson,target)),
        "A new physical ordering overwrote its historical grouped response slot");
    std::size_t draft_count=0;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.stable_id!=target.stable_id)check(!state::get_response(data,lesson,block),"The isolated visual QA contains an unrelated quiz response");
        if(block.type!=curriculum::BlockType::cpp_compiler)continue;
        check(windows::compiler_puzzle_completed(data,lesson,block)==prerequisites.contains(block.stable_id),
            "The visual QA changed an independent compiler prerequisite or credited another objective");
        const auto draft=state::get_draft(data,curriculum::draft_key("programming",lesson,block),"");
        if(draft.has_saved){++draft_count;std::cout<<"compiler_draft="<<block.stable_id<<";source_sha256="<<state::sha256(draft.source)
            <<";input_sha256="<<state::sha256(draft.input)<<";input_bytes="<<draft.input.size()
            <<";selection="<<draft.selection_start<<','<<draft.selection_end<<";indentation_sha256="<<state::sha256(draft.indentation_json)<<'\n';}
    }
    check(store.load()==data&&visual_hashes(active)==hashes_before&&state::sha256(state::read_bytes(asset))==asset_hash,
        "The read-only visual verifier changed QA state, archive files or the authored asset");
    std::cout<<"PASS isolated visual ordering checkpoint\ncase_index="<<index<<"\nqa_instance="<<utf8(token)<<"\nlesson="<<lesson.id
        <<"\nasset_sha256="<<asset_hash<<"\nstate_sha256="<<hashes_before.at("state.bin")<<"\ntarget="<<plan.identity
        <<"\npage="<<at->page+1<<"\npieces="<<plan.pieces<<"\nresponse_status="
        <<(!response?"untouched":response->correct?"correct":state::response_reviewed(*response,target)?"incorrect-reviewed":"unreviewed-draft")
        <<"\nresponse_json="<<(response?response->raw_json:"absent")<<"\nresponse_sha256="<<(response?state::sha256(response->raw_json):"absent")
        <<"\norder=";if(response)for(const auto piece:response->order)std::cout<<piece<<',';
    std::cout<<"\ncompiler_drafts="<<draft_count<<"\nother_quiz_responses=0\nhistorical_grouped_slot=absent\nreadonly=1\nwindows_created=0\n";
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

struct ScrollSnapshot {
    int width=0,height=0,pane_width=0,pane_height=0,position=0,extent=0,page=0;
    RECT order{},source{},matching{};
    std::array<RECT,3> matching_sources{};
};
ScrollSnapshot scroll_snapshot(HWND view,std::size_t block){
    const auto order=descendant(view,control_id(block,5));
    check(order!=nullptr,"The scroll probe lost its real ordering host");
    auto pane=GetParent(order);
    while(pane&&GetParent(pane)!=view)pane=GetParent(pane);
    check(pane&&pane!=view,"The scroll probe lost the lesson viewport ancestor");RECT area{},viewport{};GetClientRect(view,&area);GetClientRect(pane,&viewport);
    SCROLLINFO info{sizeof(info),SIF_ALL};check(GetScrollInfo(pane,SB_VERT,&info)!=FALSE,"Cannot inspect the lesson's native scroll range");
    ScrollSnapshot result;result.width=area.right;result.height=area.bottom;result.pane_width=viewport.right;result.pane_height=viewport.bottom;
    result.position=info.nPos;result.extent=info.nMax+1;result.page=static_cast<int>(info.nPage);
    const auto relative=[&](HWND item){RECT bounds{};if(item){GetWindowRect(item,&bounds);MapWindowPoints(nullptr,view,reinterpret_cast<POINT*>(&bounds),2);}return bounds;};
    result.order=relative(order);result.source=relative(GetDlgItem(order,100));const auto matching=descendant(view,control_id(block-1,5));result.matching=relative(matching);
    for(std::size_t index=0;index<result.matching_sources.size();++index)result.matching_sources[index]=relative(descendant(matching,106+static_cast<int>(index)*2));return result;
}
void print_scroll_snapshot(std::string_view label,const ScrollSnapshot& value){
    std::cout<<"SCROLL "<<label<<" view="<<value.width<<'x'<<value.height<<" pane="<<value.pane_width<<'x'<<value.pane_height
        <<" pos="<<value.position<<" extent="<<value.extent<<" page="<<value.page<<" order_top="<<value.order.top
        <<" source_top="<<value.source.top<<" matching_height="<<value.matching.bottom-value.matching.top<<'\n'<<std::flush;
}
void print_matching_text_geometry(HWND view,std::size_t block,std::string_view phase){
    const auto matching=descendant(view,control_id(block-1,5));
    std::cout<<"MATCHING "<<phase<<'\n';
    EnumChildWindows(matching,[](HWND item,LPARAM)->BOOL{wchar_t name[40]{};GetClassNameW(item,name,40);
        if(_wcsnicmp(name,L"RichEdit",8)!=0)return TRUE;
        RECT area{},format{};GetClientRect(item,&area);SendMessageW(item,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
        auto text=edit_text(item);std::ranges::replace(text,'\n',' ');std::ranges::replace(text,'\r',' ');
        std::cout<<"CELL id="<<GetDlgCtrlID(item)<<" client="<<area.right<<'x'<<area.bottom
            <<" format="<<format.left<<','<<format.top<<','<<format.right<<','<<format.bottom
            <<" lines="<<SendMessageW(item,EM_GETLINECOUNT,0,0)<<" text="<<text<<'\n';return TRUE;},0);
    std::cout<<std::flush;
}
struct ScrollConstructionTrace {
    inline static thread_local ScrollConstructionTrace* active=nullptr;
    struct Event {UINT message=0;ScrollSnapshot value;};std::array<Event,32> events{};std::size_t count=0;HHOOK hook=nullptr;bool inside=false;
    ScrollConstructionTrace(){check(active==nullptr,"Another scroll-construction probe is active");active=this;
        hook=SetWindowsHookExW(WH_CALLWNDPROCRET,observe,nullptr,GetCurrentThreadId());
        if(!hook){active=nullptr;throw std::runtime_error("Cannot observe zero-size lesson construction");}}
    ~ScrollConstructionTrace(){if(hook)UnhookWindowsHookEx(hook);active=nullptr;}
    static LRESULT CALLBACK observe(int code,WPARAM wp,LPARAM lp){auto* self=active;
        if(code>=0&&self&&!self->inside&&self->count<self->events.size()){
            const auto& call=*reinterpret_cast<CWPRETSTRUCT*>(lp);wchar_t name[80]{};GetClassNameW(call.hwnd,name,80);
            if(std::wstring_view(name)==L"LearnGameAndEngineDev.LessonView"&&(call.message==WM_CREATE||call.message==WM_SIZE)
                &&descendant(call.hwnd,control_id(5,5))){
                self->inside=true;self->events[self->count++]={call.message,scroll_snapshot(call.hwnd,5)};self->inside=false;
            }
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    void print()const{for(std::size_t i=0;i<count;++i)print_scroll_snapshot(events[i].message==WM_CREATE?"after-WM_CREATE":"after-WM_SIZE",events[i].value);}
};
struct MatchingMeasureTrace {
    inline static thread_local MatchingMeasureTrace* active=nullptr;
    struct Event {UINT message=0;RECT area{},format{},requested{};int lines=0,height=-1;};
    std::array<Event,96> events{};std::size_t count=0;HWND matching=nullptr;HHOOK hook=nullptr;bool inside=false;
    explicit MatchingMeasureTrace(HWND target):matching(target){check(active==nullptr,"Another matching measure trace is active");active=this;
        hook=SetWindowsHookExW(WH_CALLWNDPROC,observe,nullptr,GetCurrentThreadId());
        if(!hook){active=nullptr;throw std::runtime_error("Cannot observe the actual matching RichEdit measurement");}}
    ~MatchingMeasureTrace(){if(hook)UnhookWindowsHookEx(hook);active=nullptr;}
    static LRESULT CALLBACK observe(int code,WPARAM wp,LPARAM lp){auto* self=active;
        if(code>=0&&self&&!self->inside&&self->count<self->events.size()){
            const auto& call=*reinterpret_cast<CWPSTRUCT*>(lp);HWND source=nullptr;int height=-1;
            if(call.message==WM_NOTIFY&&call.hwnd==self->matching&&call.lParam){const auto* note=reinterpret_cast<NMHDR*>(call.lParam);
                if(note->code==EN_REQUESTRESIZE&&note->idFrom==106){source=note->hwndFrom;const auto* request=reinterpret_cast<REQRESIZE*>(call.lParam);height=request->rc.bottom-request->rc.top;}}
            else if(GetDlgCtrlID(call.hwnd)==106&&IsChild(self->matching,call.hwnd)
                &&(call.message==WM_SIZE||call.message==EM_SETRECTNP||call.message==EM_REQUESTRESIZE))source=call.hwnd;
            if(source){self->inside=true;auto& event=self->events[self->count++];event.message=call.message;event.height=height;
                GetClientRect(source,&event.area);SendMessageW(source,EM_GETRECT,0,reinterpret_cast<LPARAM>(&event.format));
                event.lines=static_cast<int>(SendMessageW(source,EM_GETLINECOUNT,0,0));
                if(call.message==EM_SETRECTNP&&call.lParam)event.requested=*reinterpret_cast<RECT*>(call.lParam);self->inside=false;}
        }return CallNextHookEx(nullptr,code,wp,lp);
    }
    void print()const{for(std::size_t i=0;i<count;++i){const auto& event=events[i];const auto label=event.message==WM_NOTIFY?"EN_REQUESTRESIZE":event.message==WM_SIZE?"WM_SIZE":event.message==EM_SETRECTNP?"EM_SETRECTNP":"EM_REQUESTRESIZE";
        std::cout<<"MEASURE cell106 "<<label<<" client="<<event.area.right<<'x'<<event.area.bottom<<" format="<<event.format.left<<','<<event.format.top<<','<<event.format.right<<','<<event.format.bottom
            <<" lines="<<event.lines<<" request_height="<<event.height<<" set_rect="<<event.requested.left<<','<<event.requested.top<<','<<event.requested.right<<','<<event.requested.bottom<<'\n';}std::cout<<std::flush;}
};
void current_order_scroll_restoration_case(int percentage,bool verbose){
    const auto& plan=plans.at(10);active_case=std::string(plan.filename)+" / scroll restoration";
    active_step="preserve the real U6L20 page position across zero-size construction";
    const auto asset=visual_asset(plan);const auto digest=state::sha256(state::read_bytes(asset));const auto lesson=windows::load_lesson(asset);
    const auto at=curriculum::stable_block_position(lesson,plan.identity);check(at.has_value(),"The scroll target lost its stable identity");
    View fixture(lesson,*at);const auto dpi=GetDpiForWindow(fixture.view);const auto px=[&](int value){return MulDiv(value,static_cast<int>(dpi),96);};
    update_lesson_preferences(fixture.view,[&](auto& data){appearance::save_course_text_scale(data,"cpp_fundamentals",false,percentage);});
    std::cout<<"SCROLL case dpi="<<dpi<<" text_scale="<<percentage<<" qa="<<utf8(fixture.environment.paths.root.filename().native())<<'\n'<<std::flush;
    const auto width=px(900),height=px(720);MoveWindow(fixture.view,0,0,width,height,TRUE);pump();
    fixture.arrange({3,2,0,4,5,6,7,1});
    for(const auto narrow:{100,330}){
        MoveWindow(fixture.view,0,0,px(narrow),height,TRUE);pump();MoveWindow(fixture.view,0,0,width,height,TRUE);pump();
        const auto first=scroll_snapshot(fixture.view,at->block);
        SendMessageW(fixture.view,WM_SIZE,SIZE_RESTORED,MAKELPARAM(width,height));pump();const auto settled=scroll_snapshot(fixture.view,at->block);
        check(first.extent==settled.extent&&EqualRect(&first.matching,&settled.matching)&&EqualRect(&first.order,&settled.order),
            "The first wide layout retained the narrow matching height until a later resize");
        check(std::ranges::equal(first.matching_sources,settled.matching_sources,[](const auto& a,const auto& b){return EqualRect(&a,&b)!=FALSE;}),
            "A source tile retained a stale narrow-width height until a second layout");
    }
    SendMessageW(fixture.view,WM_VSCROLL,SB_BOTTOM,0);pump();
    const auto before=scroll_snapshot(fixture.view,at->block);print_scroll_snapshot("before-close",before);if(verbose)print_matching_text_geometry(fixture.view,at->block,"before-close");
    check(before.position>px(500),"The authored matching/order page did not exercise a saved position near its end");
    const auto saved=fixture.saved();const auto response=state::get_response(saved,lesson,fixture.block());
    check(response&&!response->correct,"The scroll fixture lost its unreviewed real ordering draft");
    check(!state::get_response(saved,lesson,lesson.pages.at(at->page).blocks.at(at->block-1)),"The scroll fixture invented a matching answer");
    const auto raw=response->raw_json;const auto scroll_key="scroll/"+lesson.id+"/"+lesson.pages.at(at->page).stable_id;
    const auto saved_dp=state::integer_value(saved,"native_windows",scroll_key,-1);
    std::cout<<"SCROLL saved_dp="<<saved_dp<<" response_sha256="<<state::sha256(raw)<<" matching=unanswered\n"<<std::flush;
    check(std::abs(px(static_cast<int>(saved_dp))-before.position)<=1,"The initial save did not retain its DPI-normalized position");
    DestroyWindow(fixture.view);fixture.view=nullptr;
    ScrollSnapshot constructed,after;
    {
        std::optional<ScrollConstructionTrace> trace;if(verbose)trace.emplace();fixture.view=create_lesson_view(fixture.host,fixture.environment,lesson);
        constructed=scroll_snapshot(fixture.view,at->block);if(verbose)print_scroll_snapshot("after-create-zero-size",constructed);
        std::optional<MatchingMeasureTrace> measure;if(verbose)measure.emplace(descendant(fixture.view,control_id(at->block-1,5)));
        MoveWindow(fixture.view,0,0,width,height,TRUE);pump();after=scroll_snapshot(fixture.view,at->block);if(verbose){trace->print();measure->print();}
    }
    print_scroll_snapshot("after-real-layout",after);
    if(verbose)print_matching_text_geometry(fixture.view,at->block,"after-real-layout");
    SendMessageW(fixture.view,WM_SIZE,SIZE_RESTORED,MAKELPARAM(width,height));pump();
    const auto repeated=scroll_snapshot(fixture.view,at->block);
    if(verbose){print_scroll_snapshot("after-repeat-layout",repeated);print_matching_text_geometry(fixture.view,at->block,"after-repeat-layout");}
    check(repeated.extent==after.extent&&EqualRect(&repeated.order,&after.order)&&EqualRect(&repeated.matching,&after.matching),
        "A second identical layout changed the first restored matching/order geometry");
    const auto restored=read_lesson_state(fixture.view);const auto current=state::get_response(restored,lesson,fixture.block());
    check(current&&current->raw_json==raw&&fixture.order()==std::vector<std::size_t>({3,2,0,4,5,6,7,1}),"Scroll restoration altered its exact authored order payload");
    check(!state::get_response(restored,lesson,lesson.pages.at(at->page).blocks.at(at->block-1)),"Reopening invented a matching answer");
    check(lesson_page_state(fixture.view).index==at->page&&state::sha256(state::read_bytes(asset))==digest,"Scroll restoration changed the page or authored asset");
    check(after.width==before.width&&after.height==before.height&&after.pane_width==before.pane_width&&after.pane_height==before.pane_height,
        "The scroll fixture compared different final viewport sizes");
    const auto same_rect=[](const RECT& a,const RECT& b){return std::abs(a.left-b.left)<=1&&std::abs(a.top-b.top)<=1&&std::abs(a.right-b.right)<=1&&std::abs(a.bottom-b.bottom)<=1;};
    check(std::abs(after.position-before.position)<=1&&after.extent==before.extent&&after.page==before.page
        &&same_rect(after.order,before.order)&&same_rect(after.source,before.source)&&same_rect(after.matching,before.matching),
        "Reopening the current U6L20 matching/order page lost its saved scroll or final control geometry");
    check(std::ranges::equal(before.matching_sources,after.matching_sources,same_rect),
        "Reopening changed an individual matching source tile's measured rectangle");
    fixture.retained();std::cout<<"PASS current U6L20 saved scroll/extent, matching/order geometry, exact draft and unanswered matching across zero-size creation\n";
}
void current_order_scroll_restoration(bool verbose=false){
    struct DpiScope {DPI_AWARENESS_CONTEXT previous=nullptr;explicit DpiScope(DPI_AWARENESS_CONTEXT value):previous(SetThreadDpiAwarenessContext(value)){
        check(previous!=nullptr,"Cannot exercise the lesson scroll in both DPI contexts");}~DpiScope(){if(previous)SetThreadDpiAwarenessContext(previous);}};
    for(const auto context:{DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED,DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2}){
        DpiScope scope(context);for(const auto scale:{75,100,150})current_order_scroll_restoration_case(scale,verbose&&scale==100);
    }
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
        if(case_index==10)current_order_scroll_restoration();
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

int main(int argc,char* argv[]){
    if(argc>1&&(std::string_view(argv[1])=="--prepare-visual"||std::string_view(argv[1])=="--verify-visual")){
        if(argc!=4){std::cerr<<"Usage: curricular_order_view_tests [--prepare-visual|--verify-visual <case 0..13> <safe-QA-token>]\n";return 2;}
        try{const auto index=visual_case_index(argv[2]);const std::string_view raw(argv[3]);const std::wstring token(raw.begin(),raw.end());
            if(std::string_view(argv[1])=="--prepare-visual")prepare_visual_profile(index,token);else verify_visual_profile(index,token);return 0;}
        catch(const std::exception& error){std::cerr<<"Visual ordering QA: "<<error.what()<<'\n';return 1;}
    }
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{std::optional<std::size_t> selected;
        if(argc==2&&std::string_view(argv[1])=="--scroll-restore"){current_order_scroll_restoration(true);OleUninitialize();return 0;}
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
