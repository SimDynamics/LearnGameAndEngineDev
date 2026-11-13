#include "presentation/windows/lesson_view.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::presentation;

void require(bool valid,std::string_view reason) {
    if(!valid)throw std::runtime_error(std::string(reason));
}
void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
        if(message.message!=WM_QUIT) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
}
void seed_compiler_prerequisites(state::StateData& data,const Lesson& lesson) {
    // These receipts exist only in a unique disposable QA profile. No
    // source is executed, and no personal learning progress is touched.
    for(const auto& page:lesson.pages)
        for(const auto& block:page.blocks)
            if(block.type==BlockType::cpp_compiler&&block.output_check) {
                windows::CppRunRequest request;
                request.revision=1;
                request.source_utf8=block.initial_code;
                request.project_files=block.project_files;
                request.source_files=block.source_files;
                request.cmake=block.cmake;
                for(const auto& item:block.output_check->cases) {
                    request.stdin_utf8=item.standard_input;
                    FreshExecution execution{1,true,true,0,
                        item.standard_input,item.outputs.front()};
                    execution.executable=item.executable;
                    if(item.standard_error==StandardErrorExpectation::non_blank)
                        execution.standard_error="Synthetic isolated QA diagnostic";
                    (void)windows::record_compiler_puzzle_case(
                        data,lesson,block,request,execution,"Disposable reader QA");
                }
                require(windows::compiler_puzzle_completed(data,lesson,block),
                    "Required compiler prerequisite could not be seeded in QA");
            }
}
struct HiddenView {
    WindowsEnvironment env;
    Lesson lesson;
    HWND host=nullptr,view=nullptr;
    HiddenView(Lesson model,const state::StateData& state)
        :env(resolve_windows_environment({ProfileId::Codex,
            L"general-choice-reading-"+std::to_wstring(GetCurrentProcessId())+
                L"-"+std::to_wstring(GetTickCount64()),false})),
         lesson(std::move(model)) {
        require(!std::filesystem::exists(env.paths.root),
            "The general-choice QA profile must not exist already");
        initialize_profile_directories(env.paths);
        state::StateStore store(env.paths.root);
        require(store.load().preferences.empty(),
            "A supposedly isolated reader profile contains personal data");
        store.save(state);
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",
            L"Hidden general choice reading QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(host!=nullptr,"Cannot create hidden reader QA host");
        open();
    }
    ~HiddenView() {
        if(view){(void)flush_lesson_view(view);DestroyWindow(view);}
        if(host)DestroyWindow(host);
    }
    void open() {
        view=create_lesson_view(host,env,lesson);
        require(view!=nullptr,"Cannot create the real U9 lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);
        pump();
        require(!IsWindowVisible(host),"Hidden reader QA activated the desktop");
        require(lesson_page_state(view).index==7,"Wrong U9 reading page restored");
    }
    void reopen() {
        require(flush_lesson_view(view),"Cannot save the isolated U9 reader state");
        DestroyWindow(view);view=nullptr;open();
    }
};
void check_reading(HiddenView& view) {
    constexpr std::string_view earlier=
        "La operación incumple su precondición aunque conserve una cantidad válida.";
    constexpr std::string_view newer=
        "La operación incumple la condición para aceptar la transición, aunque conserve una cantidad válida.";
    const auto& choice=view.lesson.pages.at(7).blocks.at(2);
    const auto marked=choice.text_semantics.find("/options/0");
    require(marked!=choice.text_semantics.end()&&
            marked->second.source==newer&&marked->second.spans.size()==1&&
            marked->second.spans.front().text=="La operación",
        "The active reviewed choice lost its genuine UTF-16 highlight");
    const auto actual=lesson_reading_regions(view.view);
    const auto selected=std::ranges::find_if(actual,[&](const auto& region){
        return region.block_index==2&&!region.feedback&&region.source_text==newer;
    });
    require(selected!=actual.end()&&selected->text_semantics&&
            selected->text_semantics->source==newer&&
            selected->text_semantics->spans==marked->second.spans,
        "The visible reading region lost the current option's exact marking");
    const auto before=read_lesson_state(view.view);
    const auto archives=editorial_lesson_reading_archives(view.view);
    require(read_lesson_state(view.view)==before,
        "Reconstructing old U9 narration modified the learning state");
    bool authentic_previous=false;
    for(const auto& archive:archives)
        for(const auto& region:archive.regions) {
            if(region.block_index!=2||region.feedback||
               region.source_text!=earlier)continue;
            require(region.key==selected->key&&region.text_semantics&&
                    region.text_semantics->source==earlier&&
                    region.text_semantics->spans==marked->second.spans,
                "The historical option lost its authenticated original highlight");
            require(std::ranges::find(archive.revised_regions,region.key)!=
                    archive.revised_regions.end(),
                "The revised wording did not identify its changed audio segment");
            authentic_previous=true;
        }
    require(authentic_previous,
        "The old, marked choice does not occur in its authenticated reading archive");
}
}

int main(int argc,char** argv) {
    const auto apartment=OleInitialize(nullptr);
    if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    int outcome=0;
    try {
        require(argc==2,"Expected the generated, highlighted U9 choice fixture");
        auto lesson=windows::load_lesson(std::filesystem::path(argv[1]),"es");
        state::StateData qa;
        state::prepare_lesson_state(qa,lesson);
        seed_compiler_prerequisites(qa,lesson);
        state::save_current_page(qa,lesson,7);
        HiddenView view(std::move(lesson),qa);
        check_reading(view);
        view.reopen();
        check_reading(view);
        std::cout<<"PASS general choice reading: original/current semantic spans, "
            "audio revision and unchanged QA state\n";
    } catch(const std::exception& error) {
        std::cerr<<"FAIL general choice reading: "<<error.what()<<'\n';
        outcome=1;
    }
    OleUninitialize();
    return outcome;
}
