#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
struct HiddenView {
    HWND host=nullptr,view=nullptr;
    HiddenView(const WindowsEnvironment& environment,const curriculum::Lesson& lesson){
        host=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Isolated bibliography QA",WS_OVERLAPPEDWINDOW,
            0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create an isolated hidden QA host");
        view=create_lesson_view(host,environment,lesson);check(view!=nullptr,"Cannot create bibliography lesson view");
        MoveWindow(view,0,0,1000,800,TRUE);pump();
        check(!IsWindowVisible(host),"Bibliography QA must never show its host");
    }
    ~HiddenView(){if(view)DestroyWindow(view);if(host)DestroyWindow(host);}
    void page(std::size_t index){
        // Arrival is independent of a saved correct answer: visit the earlier
        // pages through the same production navigation gate, without input.
        auto current=lesson_page_state(view).index;
        while(current<index){check(navigate_lesson_view(view,current+1),"Seeded QA cannot visit its next prerequisite page");pump();
            const auto next=lesson_page_state(view).index;check(next==current+1,"QA navigation did not arrive at its admitted page");current=next;}
        if(current>index){check(navigate_lesson_view(view,index),"Seeded QA cannot reopen a visited page");pump();}
    }
    state::StateData saved(){check(flush_lesson_view(view),"Cannot save isolated bibliography QA");return read_lesson_state(view);}
};
void seed_qa_prerequisites(state::StateData& data,const curriculum::Lesson& lesson){
    // Synthetic prerequisites isolate presentation; they are not real answers,
    // compilation evidence or achievements of a student.
    state::prepare_lesson_state(data,lesson);
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check){
            windows::CppRunRequest request;request.revision=1;request.source_utf8=block.initial_code;
            for(const auto& item:block.output_check->cases){
                curriculum::FreshExecution execution{1,true,true,item.exit_code,item.standard_input,item.outputs.front()};
                execution.executable=item.executable;
                if(item.standard_error==curriculum::StandardErrorExpectation::non_blank)execution.standard_error="QA only";
                (void)windows::record_compiler_puzzle_case(data,lesson,block,request,execution,"Synthetic QA prerequisite");
            }
        }
        if(block.type==curriculum::BlockType::multiple_choice||block.type==curriculum::BlockType::output_prediction
            ||block.type==curriculum::BlockType::fill_code||block.type==curriculum::BlockType::inline_fill_code
            ||block.type==curriculum::BlockType::drag_fill_code||block.type==curriculum::BlockType::order_code){
            state::Response response;response.correct=true;
            if(block.correct_index)response.selected=static_cast<std::int64_t>(*block.correct_index);
            if(!block.answers.empty())response.answer=block.answers.front();
            for(const auto index:block.correct_order)response.order.push_back(static_cast<std::int64_t>(index));
            if(block.type==curriculum::BlockType::inline_fill_code||block.type==curriculum::BlockType::drag_fill_code){
                using namespace winrt::Windows::Data::Json;
                JsonObject raw;JsonArray values;
                if(block.type==curriculum::BlockType::inline_fill_code){
                    for(const auto& answer:block.answers)values.Append(JsonValue::CreateStringValue(winrt::to_hstring(answer)));
                    raw.Insert(L"answers",values);
                }else{
                    for(const auto index:block.correct_order)values.Append(JsonValue::CreateNumberValue(static_cast<double>(index)));
                    raw.Insert(L"slots",values);
                }
                response.raw_json=winrt::to_string(raw.Stringify());
            }
            state::put_response(data,lesson,block,response);
        }
    }
    data.preferences["qa_bibliography_opaque"]["keep"]=std::string("\tConservar progreso y ajustes\r\n保存");
    state::Draft draft;draft.source="int conservar = 7;\r\n";draft.input="dato\t7\n";
    state::put_draft(data,"qa_unrelated_bibliography_draft",draft);
    state::validate_lesson_state(data,lesson);
}
void save(const WindowsEnvironment& environment,const state::StateData& data){
    state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
}
LessonReadingRegion feedback(HWND view,std::size_t index,std::string_view explanation){
    const auto regions=lesson_reading_regions(view);
    const auto found=std::ranges::find_if(regions,[&](const auto& region){return region.block_index==index
        &&region.feedback&&region.correct&&region.source_text.find(explanation)!=std::string::npos;});
    check(found!=regions.end(),"The real view omitted its full current correct explanation");return *found;
}
void preserve(const state::StateData& before,const state::StateData& after,
              const curriculum::Lesson& old,const curriculum::Lesson& current){
    for(const auto& page:old.pages)for(const auto& block:page.blocks){
        const auto position=curriculum::stable_block_position(current,block.stable_id);
        check(position.has_value(),"Bibliography revision changed a stable block owner");
        const auto& now=current.pages[position->page].blocks[position->block];
        check(state::response_storage_key(old,block)==state::response_storage_key(current,now),"Bibliography revision changed a response key");
        const auto first=state::get_response(before,old,block),second=state::get_response(after,current,now);
        check(first.has_value()==second.has_value(),"Bibliography presentation lost a response");
        if(first)check(first->raw_json==second->raw_json&&first->correct==second->correct
            &&first->answer==second->answer&&first->selected==second->selected&&first->order==second->order,
            "Bibliography presentation changed a saved answer or verdict");
    }
    check(before.preferences.at("qa_bibliography_opaque")==after.preferences.at("qa_bibliography_opaque"),"Bibliography view changed opaque preferences");
    const auto first=state::get_draft(before,"qa_unrelated_bibliography_draft",""),second=state::get_draft(after,"qa_unrelated_bibliography_draft","");
    check(first.source==second.source&&first.input==second.input,"Bibliography view changed an unrelated compiler draft");
}
std::size_t verify(const std::filesystem::path& directory,const char* filename){
    const auto before_path=directory/(std::string("before-")+filename),after_path=directory/(std::string("after-")+filename);
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    const auto old=windows::load_lesson(before_path),current=windows::load_lesson(after_path);
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"bibliography-view-"+std::to_wstring(GetCurrentProcessId())
        +L"-"+std::to_wstring(GetTickCount64()),false});
    check(!std::filesystem::exists(environment.paths.root),"Bibliography QA requires an unused isolated profile");
    initialize_profile_directories(environment.paths);
    state::StateData data;seed_qa_prerequisites(data,old);save(environment,data);
    std::map<std::string,LessonReadingRegion> prior;
    {HiddenView view(environment,old);
        for(const auto& page:old.pages)for(const auto& block:page.blocks){
            const auto at=curriculum::stable_block_position(current,block.stable_id);
            check(at.has_value(),"The proposal lost a current block");
            if(block.explanation==current.pages[at->page].blocks[at->block].explanation)continue;
            view.page(at->page);prior[block.stable_id]=feedback(view.view,at->block,block.explanation);
        }
        data=view.saved();
    }
    check(!prior.empty(),"The fixture contains no bibliography revision");
    {HiddenView view(environment,current);
        for(const auto& [identity,original]:prior){
            const auto at=curriculum::stable_block_position(current,identity);view.page(at->page);
            const auto& block=current.pages[at->page].blocks[at->block];
            const auto now=feedback(view.view,at->block,block.explanation);
            check(now.key==original.key&&now.anchor==original.anchor,"Bibliography revision changed its reading owner");
            const auto archives=editorial_lesson_reading_archives(view.view);
            check(std::ranges::any_of(archives,[&](const auto& archive){return archive.bibliography_only
                &&std::ranges::any_of(archive.regions,[&](const auto& region){return region.key==original.key
                    &&region.source_text==original.source_text&&region.text_semantics==original.text_semantics;});}),
                "The real view did not reconstruct its exact previous explanation and authored semantics");
        }
        const auto after=view.saved();preserve(data,after,old,current);data=after;
    }
    // A rejected QA answer never exposes the new or archived success text.
    const auto identity=prior.begin()->first;const auto at=curriculum::stable_block_position(current,identity);
    const auto& block=current.pages[at->page].blocks[at->block];
    auto rejected=state::get_response(data,current,block);check(rejected.has_value(),"Missing QA rejection owner");
    rejected->correct=false;rejected->answer="QA incorrect";rejected->selected=-1;
    state::put_response(data,current,block,*rejected);state::save_current_page(data,current,at->page);save(environment,data);
    {HiddenView view(environment,current);view.page(at->page);
        for(const auto& region:lesson_reading_regions(view.view))check(!(region.block_index==at->block&&region.feedback&&region.correct),
            "A rejected response exposed a success explanation");
        for(const auto& archive:editorial_lesson_reading_archives(view.view))for(const auto& region:archive.regions)
            check(!(region.block_index==at->block&&region.feedback&&region.correct),"A rejected response exposed archived success feedback");
        const auto after=view.saved();check(!state::get_response(after,current,block)->correct,"Opening a bibliography view changed a rejection to success");
    }
    check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes,"Presentation QA changed a fixture source");
    return prior.size();
}
}
int wmain(int argc,wchar_t** argv)try{
    check(argc==2,"Expected the isolated fixture directory");
    const auto apartment=OleInitialize(nullptr);check(SUCCEEDED(apartment),"Cannot initialize native controls");
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    const auto edit=LoadLibraryW(L"Msftedit.dll");check(edit!=nullptr,"Cannot load RichEdit");
    const auto foreground=GetForegroundWindow(),focus=GetFocus();
    std::size_t count=0;for(const auto filename:{"lesson_caracteres_y_valores_logicos.json","lesson_conversiones_y_deduccion.json","lesson_enteros_y_aproximaciones.json"})
        count+=verify(argv[1],filename);
    check(count==10,"The isolated bibliography view check must cover all ten authored explanations");
    check(GetForegroundWindow()==foreground&&GetFocus()==focus,"Hidden bibliography QA changed focus or the foreground application");
    FreeLibrary(edit);OleUninitialize();
    std::cout<<"PASS 10 real authored explanations: hidden native view, exact old/current regions and semantic archives, stable response keys/answers/verdicts, opaque settings/draft and rejected feedback visibility. Synthetic isolated QA; no input, desktop activation, playback or learner credit.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL bibliography view: "<<error.what()<<'\n';return 1;}
