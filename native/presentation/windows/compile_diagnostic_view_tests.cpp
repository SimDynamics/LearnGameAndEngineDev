#include "core/compile_check_test_fixture.hpp"
#include "core/attempts.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,std::string_view text){if(!value)throw std::runtime_error(std::string(text));}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
std::string window_text(HWND window){const auto size=GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(size)+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));
    return utf8(value);
}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& search=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=search.id)return TRUE;search.found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));return search.found;}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::wstring wide(std::string_view value){return winrt::to_hstring(value).c_str();}
struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    View(curriculum::Lesson model,const state::StateData& initial):environment(resolve_windows_environment({ProfileId::Codex,
        L"compile-diagnostic-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"Diagnostic UI QA requires a new private profile");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);(void)store.load();store.save(initial);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden diagnostic objective QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create hidden diagnostic host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create diagnostic lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);pump();check(!IsWindowVisible(host),"Diagnostic UI QA exposed a desktop window");}
    void reopen(){check(flush_lesson_view(view),"Cannot save diagnostic QA");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush diagnostic QA");return read_lesson_state(view);}
    HWND control(std::size_t block,int part){const auto item=descendant(view,control_id(block,part));check(item!=nullptr,"Missing diagnostic editor control");return item;}
    std::string text(std::size_t block,int part){return edit_text(control(block,part));}
    void select(std::size_t block,LONG start,LONG end){CHARRANGE selection{start,end};SendMessageW(control(block,10),EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection));pump();}
    void replace(std::size_t block,std::string_view source){const auto edit=control(block,10);check(IsWindowEnabled(edit),"The diagnostic source is unexpectedly locked");
        CHARRANGE range{0,-1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));const auto value=wide(source);
        SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));pump();check(text(block,10)==source,"Editing changed supplied diagnostic source bytes");}
    void run(std::size_t block){const auto button=control(block,11);check(IsWindowEnabled(button),"Diagnostic compilation was already busy");
        check(window_text(button)=="Compilar y comprobar diagnóstico","Diagnostic goal uses an execution action label");
        SendMessageW(button,BM_CLICK,0,0);pump();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(125);
        while(!IsWindowEnabled(button)&&std::chrono::steady_clock::now()<deadline){MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);pump();}
        check(IsWindowEnabled(button),"Fresh diagnostic compilation did not finish");
        check(text(block,34).find("El programa no se ejecutó.")!=std::string::npos,"Diagnostic status lost its compilation-only outcome");
        check(!IsWindowVisible(host),"A compile operation activated the isolated QA host");
    }
    void reset(std::size_t block){SendMessageW(control(block,12),BM_CLICK,0,0);pump();}
};
void seed_other_objectives(state::StateData& data,const curriculum::Lesson& lesson){
    // These synthetic stdout receipts and prior quiz answers ONLY isolate the
    // diagnostic view. Every target diagnostic below must use real BM_CLICK
    // and the production compiler; no synthetic diagnostic proof is recorded.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
            windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
            for(const auto& item:block.output_check->cases){curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
                execution.executable=item.executable;if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA runtime diagnostic";
                (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"QA prerequisite stdout proof");}
        }
        if(block.type==curriculum::BlockType::multiple_choice||block.type==curriculum::BlockType::fill_code||block.type==curriculum::BlockType::output_prediction){
            state::Response answer;answer.correct=true;
            if(block.correct_index)answer.selected=static_cast<std::int64_t>(*block.correct_index);
            if(!block.answers.empty())answer.answer=block.answers.front();
            state::put_response(data,lesson,block,answer);
        }
    }
}
void reader_goal(HWND view,std::size_t index,bool solved){bool found=false;
    for(const auto& region:lesson_reading_regions(view))if(region.block_index==index&&!region.feedback&&region.is_exercise){
        found=true;check(region.required_compiler&&region.solved==solved,"Diagnostic reader gate has the wrong required/solved state");}
    check(found,"The reader omitted the diagnostic objective");
}
void explanation_region(HWND view,std::size_t index,std::string_view explanation,bool expected){bool found=false;
    for(const auto& region:lesson_reading_regions(view))if(region.source_text.find(explanation)!=std::string::npos){
        found=true;check(region.block_index==index&&region.feedback&&region.correct&&region.solved&&region.required_compiler,
            "A diagnostic success explanation entered a non-success reading region");
    }
    check(found==expected,expected?"The reader omitted the correct diagnostic feedback and explanation"
        :"The reader exposed a diagnostic success explanation before the actual rejection was proved");
}
std::string candidate(const curriculum::Block& block,std::string_view statement){auto source=block.initial_code;
    const auto position=source.rfind("    return 0;");check(position!=std::string::npos,"Reviewed diagnostic has no final main return");
    source.insert(position,"    "+std::string(statement)+"\n");return source;
}
void five_real_diagnostic_views(){
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido";
    std::size_t completed=0;
    for(const auto& plan:test::compile_check_cases){const auto path=folder/plan.filename;const auto asset_hash=state::sha256(state::read_bytes(path));
        // Integration and adoption have separate sources. Today's complete
        // authored explanation is checked independently; the transition still
        // begins with the exact immutable pre-contract source, never a no-op.
        const auto integrated=windows::load_lesson(path);
        const auto integrated_position=curriculum::stable_block_position(integrated,plan.identity);
        check(integrated_position.has_value(),"An integrated diagnostic lost its current owner");
        const auto& authored=integrated.pages[integrated_position->page].blocks[integrated_position->block];
        check(authored.compile_check&&authored.compile_check->diagnostic==plan.diagnostic
            &&!authored.compile_check->explanation.empty()&&!authored.output_check,
            "The current diagnostic contract or its complete authored explanation is missing");
        const auto original=test::load_pre_compile_check_lesson(plan.filename);
        const auto explanation=authored.compile_check->explanation;
        const auto lesson=test::with_compile_check(original,plan.identity,plan.diagnostic,explanation);
        const auto position=curriculum::stable_block_position(lesson,plan.identity);check(position.has_value(),"Reviewed diagnostic owner is missing");
        const auto& target=lesson.pages[position->page].blocks[position->block];
        check(*position==*integrated_position&&target.compile_check==authored.compile_check
            &&test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(target.raw_json)))
                ==test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(authored.raw_json)))
            &&curriculum::exercise_key(lesson,target)==curriculum::exercise_key(integrated,authored)
            &&curriculum::draft_key("programming",lesson,target)==curriculum::draft_key("programming",integrated,authored),
            "Historical adoption does not exactly reconstruct the current diagnostic block, explanation or storage identity");
        state::StateData data;state::prepare_lesson_state(data,original);seed_other_objectives(data,lesson);
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["diagnostic_qa_unrelated"]["keep"]=std::string(" \t preserved preference\r\n");
        state::Response legacy;legacy.correct=true;legacy.answer="Older opaque correctness is not diagnostic evidence";
        state::put_response(data,lesson,target,legacy);
        state::Draft draft;draft.source=target.initial_code;draft.input=" \t previous input draft\n";draft.selection_start=4;draft.selection_end=7;
        const auto draft_key=curriculum::draft_key("programming",lesson,target);state::put_draft(data,draft_key,draft);
        state::save_current_page(data,lesson,position->page);
        const auto untouched=data.preferences.at("diagnostic_qa_unrelated");const auto hearts=attempts::balance(data,"programming",now_ms()).available;
        View fixture(lesson,data);check(lesson_page_state(fixture.view).index==position->page,"Diagnostic restoration moved to another objective");
        const auto retained=[&]{const auto saved=fixture.saved();const auto restored=state::get_draft(saved,draft_key,"");
            check(saved.preferences.at("diagnostic_qa_unrelated")==untouched,"Diagnostic action changed an unrelated preference");
            check(restored.input==draft.input,"Compilation-only operation rewrote the hidden historical input draft");
            check(attempts::balance(saved,"programming",now_ms()).available==hearts,"A diagnostic attempt consumed a heart");
            for(const auto& page:lesson.pages)for(const auto& block:page.blocks)if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check)
                check(windows::compiler_puzzle_completed(saved,lesson,block),"Diagnostic action reset an independent stdout objective");
        };
        check(fixture.text(position->block,10)==target.initial_code,"Diagnostic adoption changed initial source");
        const auto initial_draft=state::get_draft(fixture.saved(),draft_key,"");
        check(initial_draft.selection_start==4&&initial_draft.selection_end==7,"Diagnostic adoption lost the saved source selection");
        check(!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&!lesson_page_state(fixture.view).can_next,"An old opaque correctness flag approved the diagnostic objective");
        check(descendant(fixture.view,control_id(position->block,15))==nullptr,"Compilation-only activity exposed a standard-input editor");
        check(!navigate_lesson_view(fixture.view,position->page+1),"Forward navigation bypassed a pending diagnostic objective");
        reader_goal(fixture.view,position->block,false);explanation_region(fixture.view,position->block,explanation,false);fixture.run(position->block);
        check(!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&fixture.text(position->block,4).starts_with("✗ Incorrecto"),"A valid unmodified program accredited a requested rejection or lost its retry guidance");
        check(fixture.text(position->block,4).find("aún falta obtener el rechazo solicitado.")!=std::string::npos,"The valid initial compilation lacks its pending-rejection guidance");
        check(fixture.text(position->block,34).starts_with("Compilación aceptada."),"The valid initial diagnostic program did not compile");
        explanation_region(fixture.view,position->block,explanation,false);retained();
        fixture.replace(position->block,"#error QA_UNRELATED_DIAGNOSTIC\n"+target.initial_code);fixture.run(position->block);
        check(fixture.text(position->block,14).find("QA_UNRELATED_DIAGNOSTIC")!=std::string::npos,"An unrelated build error lost its real diagnostic");
        check(!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&fixture.text(position->block,4).empty()&&!lesson_page_state(fixture.view).can_next,"An unrelated syntax/build error accredited or unlocked the diagnostic objective");
        reader_goal(fixture.view,position->block,false);explanation_region(fixture.view,position->block,explanation,false);retained();
        const auto solved_source=candidate(target,plan.statement);fixture.replace(position->block,solved_source);fixture.select(position->block,5,8);fixture.run(position->block);
        const auto verdict=fixture.text(position->block,4);
        check(verdict.starts_with("✓ Correcto")&&verdict.find(explanation)!=std::string::npos&&windows::compiler_puzzle_completed(fixture.saved(),lesson,target),"The fresh expected semantic rejection did not accredit its own objective");
        check(fixture.text(position->block,34).starts_with("Compilación rechazada.")&&fixture.text(position->block,14).find(plan.error)!=std::string::npos,"Accepted rejection lost its compiler-only status or original diagnostic");
        check(lesson_page_state(fixture.view).can_next,"A completed diagnostic objective still blocks this otherwise solved page");
        reader_goal(fixture.view,position->block,true);explanation_region(fixture.view,position->block,explanation,true);retained();
        check(navigate_lesson_view(fixture.view,position->page+1),"A completed diagnostic failed to release forward navigation");pump();
        check(navigate_lesson_view(fixture.view,position->page),"Cannot return to the completed diagnostic page");pump();fixture.reopen();
        check(fixture.text(position->block,10)==solved_source&&fixture.text(position->block,4)==verdict,"Reopening lost the diagnostic achievement or its actual draft");
        explanation_region(fixture.view,position->block,explanation,true);
        const auto solved_draft=state::get_draft(fixture.saved(),draft_key,"");
        check(solved_draft.selection_start==5&&solved_draft.selection_end==8,"Compilation or reopening lost the source selection");
        fixture.replace(position->block,target.initial_code);fixture.run(position->block);
        check(fixture.text(position->block,4)==verdict&&windows::compiler_puzzle_completed(fixture.saved(),lesson,target),"Editing and compiling revoked completed diagnostic credit");
        fixture.reopen();check(fixture.text(position->block,10)==target.initial_code&&fixture.text(position->block,4)==verdict,"A changed draft and sticky diagnostic credit were not preserved independently");
        fixture.reset(position->block);fixture.reopen();
        check(fixture.text(position->block,10)==target.initial_code&&fixture.text(position->block,4).empty()
            &&!windows::compiler_puzzle_completed(fixture.saved(),lesson,target)&&!lesson_page_state(fixture.view).can_next,"Explicit Reset did not restore this diagnostic's pending objective");
        reader_goal(fixture.view,position->block,false);explanation_region(fixture.view,position->block,explanation,false);retained();
        check(state::sha256(state::read_bytes(path))==asset_hash,"A diagnostic UI test changed authored lesson bytes");++completed;
    }
    std::cout<<"PASS "<<completed<<" hidden real U9 diagnostic editors: immutable pre-contract adoption exactly reconstructs each current authored block/explanation; fresh initial/error/expected rejection, no runtime, sticky credit/draft/selection, gates, scoped Reset, original preferences/hearts/assets; other stdout receipts explicitly synthetic\n";
}
}
int main(){const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);int result=0;
    try{five_real_diagnostic_views();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
