#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "core/choice_revision_test_fixture.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/attempts.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <oleacc.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND descendant(HWND parent,int id){struct Search{int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM data)->BOOL{auto& probe=*reinterpret_cast<Search*>(data);
        if(GetDlgCtrlID(item)!=probe.id)return TRUE;probe.found=item;return FALSE;},reinterpret_cast<LPARAM>(&search));return search.found;}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string script(HWND view){std::string text;for(const auto& region:lesson_reading_regions(view))text+=region.source_text+'\n';return text;}
struct HiddenLesson {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    HiddenLesson(curriculum::Lesson model,const state::StateData& initial):environment(resolve_windows_environment({ProfileId::Codex,
        L"choice-revision-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(model)){
        check(!std::filesystem::exists(environment.paths.root),"The choice revision QA profile must be isolated");
        initialize_profile_directories(environment.paths);state::StateStore store(environment.paths.root);
        check(store.load().preferences.empty(),"The choice revision QA profile must start empty");store.save(initial);
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Hidden choice revision QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the hidden choice revision host");open();
    }
    ~HiddenLesson(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create the real lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);pump();check(!IsWindowVisible(host),"A hidden choice revision test activated the desktop");}
    void reopen(){check(flush_lesson_view(view),"Cannot persist the choice revision view");DestroyWindow(view);view=nullptr;open();}
    HWND control(std::size_t block,int part)const{const auto result=descendant(view,control_id(block,part));check(result!=nullptr,"A real choice revision control is missing");return result;}
    void click(std::size_t block,int part){const auto target=control(block,part);check(IsWindowEnabled(target),"A disabled choice revision control was clicked");SendMessageW(target,BM_CLICK,0,0);pump();}
    std::string text(std::size_t block,int part)const{return edit_text(control(block,part));}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot flush the choice revision QA state");return read_lesson_state(view);}
};
void seed_required_compilers(state::StateData& data,const curriculum::Lesson& lesson){
    // Synthetic execution receipts isolate choice-view behavior. No program is
    // compiled here, and these receipts never leave this disposable QA profile.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
        windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
        request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
        for(const auto& item:block.output_check->cases){request.stdin_utf8=item.standard_input;
            curriculum::FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
            execution.executable=item.executable;
            if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="Synthetic QA diagnostic";
            (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"Synthetic QA receipt");
        }
        check(windows::compiler_puzzle_completed(data,lesson,block),"The isolated choice-view prerequisite could not be seeded");
    }
}
void reviewed_choices_preserve_the_displayed_response(){
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    std::size_t groups=0;
    for(const auto& plan:test::choice_revision_cases()){
        const auto source=folder/plan.filename;const auto hash=state::sha256(state::read_bytes(source));
        const auto original=windows::load_lesson(source);const auto revised=test::revise_choice_lesson(original,plan.identity);
        const auto position=curriculum::stable_block_position(original,plan.identity);check(position.has_value(),"The reviewed choice lost its historical address");
        const auto index=position->block;const auto& old=original.pages[position->page].blocks[index];const auto& current=revised.pages[position->page].blocks[index];
        const auto legacy=curriculum::exercise_key(original,old),sidecar=state::response_storage_key(revised,current);
        check(legacy!=sidecar,"A choice revision reused its old response slot");
        for(const std::string mode:{"empty","draft","wrong","correct"}){
            state::StateData data;state::prepare_lesson_state(data,original);seed_required_compilers(data,original);
            data.preferences["native_windows"]["ui_language"]=std::string("es");
            data.preferences["independent_choice_preferences"]["untouched"]=std::string(" \t student metadata \r\n");
            state::save_current_page(data,original,position->page);
            state::Response previous;previous.raw_json=R"({"incorrectAttempts":4,"opaque":{"caret":7}})";
            const auto wrong=plan.option==*old.correct_index?(*old.correct_index+1)%old.options.size():plan.option;
            previous.selected=static_cast<std::int64_t>(mode=="correct"?*old.correct_index:wrong);previous.correct=mode=="correct";
            if(mode=="wrong")state::mark_response_reviewed(previous,old);
            if(mode!="empty")state::put_response(data,legacy,previous);
            const auto original_json=state::string_value(data,"learning_progress",legacy);
            HiddenLesson view(revised,data);
            const auto preserved=[&]{const auto saved=view.saved();check(state::string_value(saved,"learning_progress",legacy)==original_json,
                "The revised view overwrote a historical choice, attempts or opaque metadata");
                check(state::string_value(saved,"independent_choice_preferences","untouched")==" \t student metadata \r\n",
                    "Choice interaction changed an unrelated preference");};
            const auto initial=view.saved();const auto available=attempts::balance(initial,"programming",now_ms()).available;
            check(lesson_page_state(view.view).index==position->page,"Restoring a choice revision moved the original bookmark");
            check(!state::find(initial,"learning_progress",sidecar),"Opening a revised choice manufactured a current attempt");
            for(std::size_t option=0;option<current.options.size();++option)
                check(SendMessageW(view.control(index,20+static_cast<int>(option)*2),BM_GETCHECK,0,0)==BST_UNCHECKED,
                    "An old selection was attributed to a current option");
            check(!IsWindowEnabled(view.control(index,3)),"A historical selection can be checked as a current one without choosing it");
            if(mode=="empty")check(!descendant(view.view,control_id(index,40)),"An empty profile fabricated a previous selection");
            else{
                check(view.text(index,40).find(old.options[static_cast<std::size_t>(previous.selected)])!=std::string::npos,
                    "The previous selection summary lost its original text");
                check(script(view.view).find(view.text(index,40))!=std::string::npos,"The reader omitted the historical choice summary");
            }
            const auto prior_script=script(view.view);
            if(mode=="wrong"){
                const auto old_guide=curriculum::incorrect_feedback(old,static_cast<std::size_t>(previous.selected));
                check(!old_guide.empty()&&prior_script.find(old_guide)!=std::string::npos,"The reader restored a current guide over an old rejection");
                if(plan.feedback&&old_guide!=plan.feedback)check(prior_script.find(plan.feedback)==std::string::npos,"A revised guide appeared before the student selected its option");
            }else if(mode=="correct")check(view.text(index,4).starts_with("✓ Correcto"),"The revised view lost a previous achievement");
            else check(view.text(index,4).empty(),"An unsubmitted selection received an invented verdict");
            preserved();view.reopen();preserved();
            check(attempts::balance(view.saved(),"programming",now_ms()).available==available,"Reopening a historical choice consumed an attempt");
            if(mode=="correct")view.click(index,12);
            view.click(index,20+static_cast<int>(wrong)*2);
            check(!state::get_response(view.saved(),revised,current)->historical_choice,"A new radio click kept historical provenance");
            if(const auto summary=descendant(view.view,control_id(index,40)))check(edit_text(summary).empty(),"A new selection retained the old selection summary");
            view.click(index,3);const auto after_wrong=view.saved();
            const auto response=state::get_response(after_wrong,revised,current);
            check(response&&!response->correct&&response->selected==static_cast<std::int64_t>(wrong),"A new distractor was not rejected in its own response slot");
            const auto guide=curriculum::incorrect_feedback(current,wrong);
            check(!guide.empty()&&script(view.view).find(guide)!=std::string::npos,"The current distractor lost its own reader feedback");
            preserved();view.click(index,3);
            check(attempts::balance(view.saved(),"programming",now_ms()).available==attempts::balance(after_wrong,"programming",now_ms()).available,
                "Repeating the same current rejection consumed another attempt");
            view.click(index,20+static_cast<int>(*current.correct_index)*2);view.click(index,3);
            check(view.text(index,4).starts_with("✓ Correcto"),"The current correct option lost its normal explanation or verdict");preserved();
            view.reopen();preserved();const auto solved=state::get_response(view.saved(),revised,current);
            check(solved&&solved->correct&&!solved->historical_choice&&!IsWindowEnabled(view.control(index,20)),"The current achievement did not survive reopening");
            view.click(index,12);view.reopen();preserved();const auto reset=state::get_response(view.saved(),revised,current);
            check(reset&&!reset->correct&&reset->selected<0&&!reset->historical_choice&&view.text(index,4).empty(),
                "Explicit reset resurrected the old achievement or changed its archive");++groups;
        }
        check(state::sha256(state::read_bytes(source))==hash,"A native choice revision test wrote the authored asset");
    }
    std::cout<<"PASS "<<groups<<" hidden real lesson-view cases: five choice revisions, previous text/guide, real new selection, preserved blobs/attempts, reader sources, reset and reopen\n";
}
void ordinary_choices_keep_their_own_controls(){
    std::string source=R"({"id":"qa_many_choice_options","title":"Opciones conservadas","pages":[{"blocks":[{"type":"multiple_choice","question":"Elige el primer inciso.","options":[)";
    for(int option=0;option<11;++option){if(option)source+=',';source+='"'+std::string("Inciso ")+std::to_string(option)+'"';}
    source+=R"(],"correctIndex":0,"explanation":"El primer inciso es el solicitado."}]}]})";
    state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");
    HiddenLesson view(windows::parse_lesson_json(source),data);const auto last=view.control(0,40);
    const auto label=[&]{Microsoft::WRL::ComPtr<IAccessible> accessible;
        check(SUCCEEDED(AccessibleObjectFromWindow(last,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(accessible.GetAddressOf()))),
            "The ordinary choice has no native accessibility object");
        VARIANT child{};child.vt=VT_I4;child.lVal=CHILDID_SELF;BSTR value=nullptr;
        check(SUCCEEDED(accessible->get_accName(child,&value)),"The ordinary choice has no accessible label");
        const auto text=value?utf8(std::wstring_view(value,SysStringLen(value))):std::string{};SysFreeString(value);return text;};
    check(label()=="Inciso 10","The ordinary eleventh choice has no native label");
    view.click(0,22);view.click(0,3);check(label()=="Inciso 10","Choice history overwrote an unrelated eleventh radio label");
    view.click(0,12);check(label()=="Inciso 10"&&script(view.view).find("Respuesta anterior")==std::string::npos,
        "Reset fabricated historical text or changed an ordinary choice");
    std::cout<<"PASS ordinary multiple-choice controls: eleven options retain labels and reset without revision history\n";
}
void archived_reading_keeps_choice_provenance(){
    const auto& plan=test::choice_revision_cases().front();
    const auto original=windows::load_lesson(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos"/plan.filename);
    const auto position=curriculum::stable_block_position(original,plan.identity);check(position.has_value(),"The archive reading choice is missing");
    const auto& old=original.pages[position->page].blocks[position->block];
    std::string retiring;for(const auto& block:original.pages[position->page].blocks)
        if(block.type==curriculum::BlockType::paragraph){retiring=block.stable_id;break;}
    check(!retiring.empty(),"The archive reading case needs its real authored paragraph");
    const std::array<std::string,1> removal{retiring};
    for(const bool revision_first:{false,true}){
        const auto model=revision_first?test::retire_lesson(test::revise_choice_lesson(original,plan.identity),removal)
            :test::revise_choice_lesson(test::retire_lesson(original,removal),plan.identity);
        state::StateData data;state::prepare_lesson_state(data,original);seed_required_compilers(data,original);
        data.preferences["native_windows"]["ui_language"]=std::string("es");state::save_current_page(data,original,position->page);
        state::Response previous;previous.selected=static_cast<std::int64_t>(plan.option);state::mark_response_reviewed(previous,old);
        const auto key=curriculum::exercise_key(original,old);state::put_response(data,key,previous);
        const auto raw=state::string_value(data,"learning_progress",key);HiddenLesson view(model,data);
        const auto active=curriculum::stable_block_position(model,plan.identity);check(active.has_value(),"Retirement lost the active choice identity");
        const auto summary=view.text(active->block,40);const auto guide=curriculum::incorrect_feedback(old,plan.option);
        std::string archived;for(const auto& region:retired_lesson_reading_regions(view.view))archived+=region.source_text+'\n';
        check(archived.find(guide)!=std::string::npos&&archived.find(plan.feedback)==std::string::npos,
            "Archived narration attributed a new guide to an old selection");
        check((archived.find(summary)!=std::string::npos)==revision_first,
            "Archived narration lost or invented the choice summary for its actual presentation version");
        check(state::string_value(view.saved(),"learning_progress",key)==raw,"Reconstructing an archived reader script rewrote the original choice");
    }
    std::cout<<"PASS archived narration before/after option revision: original selection/guide and version-correct history summary\n";
}
}
int main(){
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    int result=0;try{reviewed_choices_preserve_the_displayed_response();ordinary_choices_keep_their_own_controls();archived_reading_keeps_choice_provenance();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
