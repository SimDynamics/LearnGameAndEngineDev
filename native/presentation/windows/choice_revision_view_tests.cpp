#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "core/choice_revision_test_fixture.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/attempts.hpp"
#include "core/narration_state.hpp"
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
    void restore_qa_page(std::size_t page){
        // Select another authored page through only this owned QA store. This
        // isolates simultaneous revisions without inventing answers to the
        // intervening puzzles or claiming that ordinary navigation was tested.
        auto initial=saved();DestroyWindow(view);view=nullptr;
        state::save_current_page(initial,lesson,page);
        state::StateStore store(environment.paths.root);(void)store.load();store.save(initial);open();
        check(lesson_page_state(view).index==page,"The isolated choice bookmark did not restore its authored page");
    }
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
void exact_feedback(HiddenLesson& view,std::size_t index,std::string_view explanation,bool correct){
    const auto expected=std::string(correct?"✓ Correcto":"✗ Incorrecto")
        +(explanation.empty()?"":"\n\n"+std::string(explanation));
    check(view.text(index,4)==expected,"The choice view displayed a different explanation or verdict than its actual response");
    const auto regions=lesson_reading_regions(view.view);
    const auto region=std::ranges::find_if(regions,[&](const auto& item){return item.block_index==index&&item.feedback;});
    check(region!=regions.end()&&region->source_text==expected&&region->correct==correct&&region->solved==correct
        &&region->target==view.control(index,4)&&region->is_exercise,
        "The choice reader source lost the response's exact explanation, target or answer boundary");
}
struct ReadingProjection {
    std::string key,anchor,source,context,math_mode,retired_successor;
    int target_id=0;
    std::size_t block=0;
    bool feedback=false,correct=false,is_exercise=false,solved=false,passive_label=false,required_compiler=false;
    bool operator==(const ReadingProjection&)const=default;
};
std::vector<ReadingProjection> project_reading(const std::vector<LessonReadingRegion>& regions){
    std::vector<ReadingProjection> result;for(const auto& region:regions)
        result.push_back({region.key,region.anchor,region.source_text,region.reading_context,region.math_mode,region.retired_successor,
            region.target?GetDlgCtrlID(region.target):0,region.block_index,region.feedback,region.correct,region.is_exercise,region.solved,
            region.passive_label,region.required_compiler});
    return result;
}
void exact_previous_choice_script(HiddenLesson& view,std::string_view page,const std::vector<ReadingProjection>& expected){
    const auto before=read_lesson_state(view.view);const auto archives=editorial_lesson_reading_archives(view.view);
    check(std::ranges::any_of(archives,[&](const auto& archive){return archive.page_identity==page&&project_reading(archive.regions)==expected;}),
        "The authenticated previous choice script differs from the real before view's order, sources, anchors, control IDs, contexts or exercise flags");
    check(read_lesson_state(view.view)==before,"Reconstructing a previous choice reader script rewrote saved student data");
}
void reviewed_choices_preserve_the_displayed_response(bool consult_only=false){
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    check(test::choice_revision_cases().size()==8,"The native choice revision inventory must cover all eight exact reviews");
    std::size_t groups=0,reviewed=0;
    for(const auto& plan:test::choice_revision_cases()){
        const bool consult=std::string_view(plan.filename)=="lesson_una_tabla_persistente_de_nombres.json"&&std::string_view(plan.identity)=="block_0000_0003";
        if(consult_only&&!consult)continue;++reviewed;
        const auto source=folder/plan.filename;const auto hash=state::sha256(state::read_bytes(source));
        const auto integrated=windows::load_lesson(source);
        const auto original=test::choice_revision_before(integrated,plan.identity);
        const auto revised=test::revise_choice_lesson(original,plan.identity);
        const auto position=curriculum::stable_block_position(original,plan.identity);check(position.has_value(),"The reviewed choice lost its historical address");
        const auto index=position->block;const auto& old=original.pages[position->page].blocks[index];const auto& current=revised.pages[position->page].blocks[index];
        check(current.options.at(plan.option)==plan.replacement&&current.correct_index==old.correct_index
            &&current.question==old.question,"An exact choice proposal changed its question/index or lost the reviewed option");
        if(plan.explanation)check(current.explanation==plan.explanation&&old.explanation!=current.explanation,
            "The paired token proposal lost its exact new explanation or its distinct original");
        const auto legacy=curriculum::exercise_key(original,old),sidecar=state::response_storage_key(revised,current);
        check(legacy!=sidecar,"A choice revision reused its old response slot");
        for(const std::string mode:{"empty","draft","wrong","correct"}){
            state::StateData data;state::prepare_lesson_state(data,original);seed_required_compilers(data,original);
            data.preferences["native_windows"]["ui_language"]=std::string("es");
            data.preferences["independent_choice_preferences"]["untouched"]=std::string(" \t student metadata \r\n");
            state::save_current_page(data,original,position->page);
            std::string personal_draft_key;
            if(consult){
                check(position->page==0&&position->block==3&&plan.option==0&&old.correct_index==1,
                    "The consultation revision no longer has its exact U11L18 owner/index");
                const auto editor_at=curriculum::stable_block_position(original,"block_0005_0002");check(editor_at.has_value(),"The U11L18 draft owner is absent");
                const auto& editor=original.pages[editor_at->page].blocks[editor_at->block];personal_draft_key=curriculum::draft_key("programming",original,editor);
                state::Draft draft;draft.source=editor.initial_code+"\n    // fuente personal conservada\r\n";draft.input=" \t entrada propia\r\n";
                draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=4;draft.selection_end=11;state::put_draft(data,personal_draft_key,draft);
                // Storage-only history checks do not claim PCM, listening or
                // permission to resume the revised text at an old offset.
                auto history=narration::begin({},"consult-view-storage-a","consult-view-owner-a",{0,117,0,false},"original unrelated page A","consult-view-page-a");
                history=narration::begin(std::move(history),"consult-view-storage-b","consult-view-owner-b",{1,239,0,false},"original unrelated page B","consult-view-page-b");
                history=narration::begin(std::move(history),"consult-view-storage-c","consult-view-owner-c",{2,317,0,false},old.options[0],"consult-view-page-c");
                narration::write_history(data,history);
            }
            const auto original_history=narration::read_history(data);
            state::Response previous;previous.raw_json=R"({"incorrectAttempts":4,"opaque":{"caret":7}})";
            const auto wrong=plan.option==*old.correct_index?(*old.correct_index+1)%old.options.size():plan.option;
            previous.selected=static_cast<std::int64_t>(mode=="correct"?*old.correct_index:mode=="draft"?plan.option:wrong);previous.correct=mode=="correct";
            if(mode=="wrong")state::mark_response_reviewed(previous,old);
            if(mode!="empty")state::put_response(data,legacy,previous);
            const auto original_json=state::string_value(data,"learning_progress",legacy);
            std::string previous_page;std::vector<ReadingProjection> previous_regions;
            {
                // Capture from actual old controls, not a hand-built expected
                // script. HWND values expire; only their control IDs are pinned.
                HiddenLesson before_view(original,data);
                check(lesson_page_state(before_view.view).index==position->page,"The previous choice script restored another page");
                previous_page=lesson_page_state(before_view.view).narration_page_identity;
                previous_regions=project_reading(lesson_reading_regions(before_view.view));
            }
            HiddenLesson view(revised,data);
            const auto preserved=[&]{const auto saved=view.saved();check(state::string_value(saved,"learning_progress",legacy)==original_json,
                "The revised view overwrote a historical choice, attempts or opaque metadata");
                check(state::string_value(saved,"independent_choice_preferences","untouched")==" \t student metadata \r\n",
                    "Choice interaction changed an unrelated preference");
                if(consult){const auto actual=state::get_draft(saved,personal_draft_key,""),previous=state::get_draft(data,personal_draft_key,"");
                    check(actual.has_saved&&actual.source==previous.source&&actual.input==previous.input&&actual.indentation_json==previous.indentation_json
                        &&actual.selection_start==previous.selection_start&&actual.selection_end==previous.selection_end,
                        "Choice interaction changed the independent U11L18 compiler source, stdin, indentation or selection");
                    check(narration::read_history(saved)==original_history,"Opening/interacting with the revised choice rewrote stored page history or invented listening");
                }};
            const auto initial=view.saved();const auto available=attempts::balance(initial,"programming",now_ms()).available;
            exact_previous_choice_script(view,previous_page,previous_regions);
            check(lesson_page_state(view.view).index==position->page,"Restoring a choice revision moved the original bookmark");
            check(!state::find(initial,"learning_progress",sidecar),"Opening a revised choice manufactured a current attempt");
            const auto current_regions=lesson_reading_regions(view.view);
            for(std::size_t option=0;option<current.options.size();++option){
                check(SendMessageW(view.control(index,20+static_cast<int>(option)*2),BM_GETCHECK,0,0)==BST_UNCHECKED,
                    "An old selection was attributed to a current option");
                const auto label=view.control(index,21+static_cast<int>(option)*2);
                check(edit_text(label)==current.options[option],"A revised option label lost its exact current wording");
                check(std::ranges::any_of(current_regions,[&](const auto& region){return region.target==label&&region.source_text==current.options[option];}),
                    "A revised option was rendered with a historical or missing reader source");
            }
            check(!IsWindowEnabled(view.control(index,3)),"A historical selection can be checked as a current one without choosing it");
            if(mode=="empty")check(!descendant(view.view,control_id(index,40)),"An empty profile fabricated a previous selection");
            else{
                check(view.text(index,40).find(old.options[static_cast<std::size_t>(previous.selected)])!=std::string::npos,
                    "The previous selection summary lost its original text");
                check(script(view.view).find(view.text(index,40))!=std::string::npos,"The reader omitted the historical choice summary");
            }
            const auto prior_script=script(view.view);
            if(current.explanation!=old.explanation)check(prior_script.find(current.explanation)==std::string::npos,
                "A new success explanation was attributed to a historical or unanswered choice");
            if(mode=="wrong"){
                const auto old_guide=curriculum::incorrect_feedback(old,static_cast<std::size_t>(previous.selected));
                check(!old_guide.empty()&&prior_script.find(old_guide)!=std::string::npos,"The reader restored a current guide over an old rejection");
                exact_feedback(view,index,old_guide,false);
                if(plan.feedback&&old_guide!=plan.feedback)check(prior_script.find(plan.feedback)==std::string::npos,"A revised guide appeared before the student selected its option");
            }else if(mode=="correct")exact_feedback(view,index,old.explanation,true);
            else check(view.text(index,4).empty(),"An unsubmitted selection received an invented verdict");
            preserved();view.reopen();preserved();exact_previous_choice_script(view,previous_page,previous_regions);
            if(mode=="correct")exact_feedback(view,index,old.explanation,true);
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
            exact_feedback(view,index,guide,false);
            const auto rejected_slot=state::string_value(after_wrong,"learning_progress",sidecar);
            check(!rejected_slot.empty(),"A current rejection was not stored in its independent revision slot");
            exact_previous_choice_script(view,previous_page,previous_regions);
            preserved();view.click(index,3);
            check(attempts::balance(view.saved(),"programming",now_ms()).available==attempts::balance(after_wrong,"programming",now_ms()).available,
                "Repeating the same current rejection consumed another attempt");
            view.reopen();preserved();exact_feedback(view,index,guide,false);
            exact_previous_choice_script(view,previous_page,previous_regions);
            check(state::string_value(view.saved(),"learning_progress",sidecar)==rejected_slot,
                "Reopening rewrote the current rejection's independent response blob");
            view.click(index,20+static_cast<int>(*current.correct_index)*2);view.click(index,3);
            exact_feedback(view,index,current.explanation,true);preserved();
            exact_previous_choice_script(view,previous_page,previous_regions);
            const auto approved_slot=state::string_value(view.saved(),"learning_progress",sidecar);
            view.reopen();preserved();const auto solved=state::get_response(view.saved(),revised,current);
            check(solved&&solved->correct&&!solved->historical_choice&&!IsWindowEnabled(view.control(index,20)),"The current achievement did not survive reopening");
            exact_feedback(view,index,current.explanation,true);
            exact_previous_choice_script(view,previous_page,previous_regions);
            check(state::string_value(view.saved(),"learning_progress",sidecar)==approved_slot,
                "Reopening rewrote the current achievement's independent response blob");
            view.click(index,12);view.reopen();preserved();const auto reset=state::get_response(view.saved(),revised,current);
            check(reset&&!reset->correct&&reset->selected<0&&!reset->historical_choice&&view.text(index,4).empty(),
                "Explicit reset resurrected the old achievement or changed its archive");
            exact_previous_choice_script(view,previous_page,previous_regions);++groups;
        }
        check(state::sha256(state::read_bytes(source))==hash,"A native choice revision test wrote the authored asset");
    }
    check(reviewed==(consult_only?1u:test::choice_revision_cases().size())&&groups==reviewed*4,"The reviewed choices did not exercise all four independent historical response states");
    std::cout<<"PASS "<<groups<<" hidden real lesson-view cases: "<<reviewed<<" exact choice revisions, previous text/guide/explanation and exact archived reader projections before/after new sidecars, real new selection, preserved blobs/attempts, exact current option/reader sources, reset and reopen; U11L18 keeps its independent draft and three storage-only positions, with no synthesized audio or listening claimed\n";
}
void paired_u11_revisions_keep_independent_response_slots(){
    constexpr std::string_view token_id="block_0003_0004",exception_id="block_0007_0001";
    const auto source=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json";
    const auto hash=state::sha256(state::read_bytes(source));const auto integrated=windows::load_lesson(source);
    const auto original=test::choice_revision_before(test::choice_revision_before(integrated,token_id),exception_id);
    const auto revised=test::revise_choice_lesson(test::revise_choice_lesson(original,exception_id),token_id);
    const auto reverse=test::revise_choice_lesson(test::revise_choice_lesson(original,token_id),exception_id);
    const auto token_at=curriculum::stable_block_position(original,token_id),exception_at=curriculum::stable_block_position(original,exception_id);
    check(token_at&&exception_at,"The paired U11L14 choice identities are missing");
    const auto& old_token=original.pages[token_at->page].blocks[token_at->block];
    const auto& old_exception=original.pages[exception_at->page].blocks[exception_at->block];
    const auto& token=revised.pages[token_at->page].blocks[token_at->block];
    const auto& exception=revised.pages[exception_at->page].blocks[exception_at->block];
    const auto old_token_key=curriculum::exercise_key(original,old_token),old_exception_key=curriculum::exercise_key(original,old_exception);
    const auto token_key=state::response_storage_key(revised,token),exception_key=state::response_storage_key(revised,exception);
    check(token_key!=exception_key&&token_key!=old_token_key&&exception_key!=old_exception_key
        &&token_key==state::response_storage_key(reverse,reverse.pages[token_at->page].blocks[token_at->block])
        &&exception_key==state::response_storage_key(reverse,reverse.pages[exception_at->page].blocks[exception_at->block]),
        "Paired choice revision order changed or merged its independent current response identities");
    state::StateData data;state::prepare_lesson_state(data,original);seed_required_compilers(data,original);
    data.preferences["native_windows"]["ui_language"]=std::string("es");state::save_current_page(data,original,token_at->page);
    for(const auto* block:{&old_token,&old_exception}){
        state::Response response;response.correct=true;response.selected=static_cast<std::int64_t>(*block->correct_index);
        response.raw_json=R"({"incorrectAttempts":3,"opaque":{"source":"paired U11 QA"}})";
        state::mark_response_reviewed(response,*block);state::put_response(data,curriculum::exercise_key(original,*block),response);
    }
    const auto previous_token=state::string_value(data,"learning_progress",old_token_key);
    const auto previous_exception=state::string_value(data,"learning_progress",old_exception_key);
    HiddenLesson view(revised,data);
    const auto originals_preserved=[&]{const auto saved=view.saved();
        check(state::string_value(saved,"learning_progress",old_token_key)==previous_token
            &&state::string_value(saved,"learning_progress",old_exception_key)==previous_exception,
            "A paired current choice attempt or Reset overwrote either historical response blob");};
    exact_feedback(view,token_at->block,old_token.explanation,true);originals_preserved();
    view.click(token_at->block,12);view.click(token_at->block,20+static_cast<int>(*token.correct_index)*2);view.click(token_at->block,3);
    exact_feedback(view,token_at->block,token.explanation,true);
    const auto accepted_token=state::string_value(view.saved(),"learning_progress",token_key);
    check(!accepted_token.empty(),"The paired token response did not create its own current sidecar");
    view.restore_qa_page(exception_at->page);originals_preserved();
    exact_feedback(view,exception_at->block,old_exception.explanation,true);
    check(state::string_value(view.saved(),"learning_progress",token_key)==accepted_token
        &&!state::find(view.saved(),"learning_progress",exception_key),
        "Opening the second revision changed the first current response or manufactured the second attempt");
    view.click(exception_at->block,12);view.click(exception_at->block,20+static_cast<int>(*exception.correct_index)*2);view.click(exception_at->block,3);
    exact_feedback(view,exception_at->block,exception.explanation,true);
    const auto accepted_exception=state::string_value(view.saved(),"learning_progress",exception_key);
    check(!accepted_exception.empty(),"The paired exception response did not create its own current sidecar");
    view.restore_qa_page(token_at->page);originals_preserved();exact_feedback(view,token_at->block,token.explanation,true);
    view.click(token_at->block,12);view.reopen();originals_preserved();
    check(state::string_value(view.saved(),"learning_progress",exception_key)==accepted_exception
        &&view.text(token_at->block,4).empty(),"Resetting the token revision changed the independent exception achievement");
    view.restore_qa_page(exception_at->page);exact_feedback(view,exception_at->block,exception.explanation,true);
    const auto reset_token=state::string_value(view.saved(),"learning_progress",token_key);
    view.click(exception_at->block,12);view.reopen();originals_preserved();
    check(state::string_value(view.saved(),"learning_progress",token_key)==reset_token
        &&view.text(exception_at->block,4).empty()&&state::sha256(state::read_bytes(source))==hash,
        "Resetting the exception revision changed the independent token reset or authored asset");
    std::cout<<"PASS paired U11L14 choice revisions: order-independent current identities, original success explanations, separate real new answers, exact reader regions, durable blobs and scoped Reset; page bookmarks isolated in QA only\n";
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
    for(const auto& plan:test::choice_revision_cases()){
    if(&plan!=&test::choice_revision_cases().front()&&!plan.explanation)continue;
    const auto integrated=windows::load_lesson(std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos"/plan.filename);
    const auto original=test::choice_revision_before(integrated,plan.identity);
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
        state::Response previous;previous.correct=plan.explanation!=nullptr;
        previous.selected=static_cast<std::int64_t>(previous.correct?*old.correct_index:plan.option);state::mark_response_reviewed(previous,old);
        const auto key=curriculum::exercise_key(original,old);state::put_response(data,key,previous);
        const auto raw=state::string_value(data,"learning_progress",key);HiddenLesson view(model,data);
        const auto active=curriculum::stable_block_position(model,plan.identity);check(active.has_value(),"Retirement lost the active choice identity");
        const auto summary=view.text(active->block,40);
        const auto body=previous.correct?old.explanation:curriculum::incorrect_feedback(old,plan.option);
        const auto expected=std::string(previous.correct?"✓ Correcto":"✗ Incorrecto")+"\n\n"+body;
        std::string archived;for(const auto& region:retired_lesson_reading_regions(view.view))archived+=region.source_text+'\n';
        check(archived.find(expected)!=std::string::npos,"Archived narration lost the original response's exact verdict and explanation");
        if(plan.feedback&&body!=plan.feedback)check(archived.find(plan.feedback)==std::string::npos,
            "Archived narration attributed a new guide to an old selection");
        if(plan.explanation)check(archived.find(plan.explanation)==std::string::npos,
            "Archived narration attributed a new success explanation to an old achievement");
        exact_feedback(view,active->block,body,previous.correct);
        check((archived.find(summary)!=std::string::npos)==revision_first,
            "Archived narration lost or invented the choice summary for its actual presentation version");
        check(state::string_value(view.saved(),"learning_progress",key)==raw,"Reconstructing an archived reader script rewrote the original choice");
    }
    }
    std::cout<<"PASS archived narration before/after option and paired explanation revisions: exact original selection/guide/success explanation and version-correct history summary\n";
}
}
int main(int argc,char** argv){
    const auto apartment=OleInitialize(nullptr);if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    int result=0;try{
        if(argc==2&&std::string_view(argv[1])=="--u11-consult")reviewed_choices_preserve_the_displayed_response(true);
        else{check(argc==1,"Unknown choice revision test selector");reviewed_choices_preserve_the_displayed_response();paired_u11_revisions_keep_independent_response_slots();ordinary_choices_keep_their_own_controls();archived_reading_keeps_choice_provenance();}
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    OleUninitialize();return result;
}
