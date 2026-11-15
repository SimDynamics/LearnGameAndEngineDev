#include "presentation/windows/lesson_view.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <atomic>
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
constexpr std::size_t reviewed_page=7;
constexpr std::string_view prefix_math_owner="block_0001_0004";
constexpr std::string_view prefix_math_answers=R"({
    "resultados":{"type":"fields","values":{
        "resultado_1":"1","consulta_1":"7","resultado_2":"0","consulta_2":"7",
        "resultado_3":"1","consulta_3":"1","resultado_4":"0","consulta_4":"1",
        "resultado_5":"0","consulta_5":"7"}},
    "motivos":{"type":"fields","values":{
        "motivo_1":"cantidad_y_estado","motivo_2":"cantidad_fuera",
        "motivo_3":"transicion_preparado","motivo_4":"ya_enviado","motivo_5":"ya_enviado"}}
})";

struct QaMathPrerequisite {
    // This exact prerequisite is a test double, not a MathBridge verdict.
    // Its QA-only fingerprint and compiler receipts never leave this profile.
    std::string source,fingerprint;
    explicit QaMathPrerequisite(const Lesson& lesson) {
        using namespace winrt::Windows::Data::Json;
        const auto position=stable_block_position(lesson,prefix_math_owner);
        require(position&&position->page==1&&position->block==2,
            "The audited U9 mathematical prerequisite moved");
        const auto& block=lesson.pages[position->page].blocks[position->block];
        require(block.type==BlockType::math_puzzle&&lesson.locale=="es",
            "The U9 prerequisite is no longer the audited Spanish math activity");
        source=math_evaluation_source(block);
        fingerprint=state::sha256("general-choice-reader-qa-only/"+source);
        const auto authored=JsonObject::Parse(winrt::to_hstring(source));
        const auto expected=JsonObject::Parse(winrt::to_hstring(prefix_math_answers));
        const auto responses=authored.GetNamedArray(L"responses");
        require(responses.Size()==expected.Size(),
            "The audited U9 prerequisite response groups changed");
        std::size_t fields=0;
        for(const auto& value:responses) {
            const auto response=value.GetObject();
            const auto id=response.GetNamedString(L"id");
            const auto validator=response.GetNamedObject(L"validator");
            require(response.GetNamedString(L"type")==L"math_multi_input"&&
                    validator.GetNamedString(L"type")==L"fields"&&expected.HasKey(id),
                "The audited U9 prerequisite changed its response contract");
            const auto canonical=expected.GetNamedObject(id).GetNamedObject(L"values");
            const auto validators=validator.GetNamedObject(L"fields");
            require(validators.Size()==canonical.Size()&&
                    response.GetNamedArray(L"fields").Size()==canonical.Size(),
                "The audited U9 prerequisite field inventory changed");
            for(const auto& value:response.GetNamedArray(L"fields")) {
                const auto field=value.GetObject();
                require(canonical.HasKey(field.GetNamedString(L"id"))&&
                        field.GetNamedString(L"type")==(id==L"resultados"?L"numeric":L"dropdown"),
                    "The audited U9 prerequisite changed a field identity or interaction");
            }
            for(const auto& field:canonical) {
                const auto rule=validators.GetNamedObject(field.Key());
                require(rule.GetNamedString(L"type")==(id==L"resultados"?L"numeric":L"text")&&
                        rule.GetNamedString(L"expected")==field.Value().GetString(),
                    "The independently calculated U9 prerequisite answer changed");
                ++fields;
            }
        }
        require(fields==15,"The U9 prerequisite no longer has fifteen audited fields");
    }
    std::string validate(std::string_view block,std::string_view answers,
                         std::string_view language) const {
        using namespace winrt::Windows::Data::Json;
        require(block==source&&language=="es",
            "The QA math double received another activity or language");
        const auto submitted=JsonObject::Parse(winrt::to_hstring(answers));
        const auto expected=JsonObject::Parse(winrt::to_hstring(prefix_math_answers));
        require(submitted.Size()==expected.Size(),"Unexpected QA math response groups");
        for(const auto& group:expected) {
            const auto response=submitted.GetNamedObject(group.Key());
            const auto values=response.GetNamedObject(L"values");
            const auto canonical=group.Value().GetObject().GetNamedObject(L"values");
            require(response.Size()==2&&response.GetNamedString(L"type")==L"fields"&&
                    values.Size()==canonical.Size(),"Unexpected QA math response shape");
            for(const auto& field:canonical)
                require(values.GetNamedString(field.Key())==field.Value().GetString(),
                    "The QA math double refuses another answer");
        }
        JsonObject reply;
        reply.Insert(L"evaluable",JsonValue::CreateBooleanValue(true));
        reply.Insert(L"correct",JsonValue::CreateBooleanValue(true));
        reply.Insert(L"contentFingerprint",JsonValue::CreateStringValue(winrt::to_hstring(fingerprint)));
        return winrt::to_string(reply.Stringify());
    }
    state::Response receipt() const {
        using namespace winrt::Windows::Data::Json;
        JsonObject raw;
        raw.Insert(L"responses",JsonObject::Parse(winrt::to_hstring(prefix_math_answers)));
        raw.Insert(L"contentFingerprint",JsonValue::CreateStringValue(winrt::to_hstring(fingerprint)));
        state::Response response;response.correct=true;
        response.raw_json=winrt::to_string(raw.Stringify());
        return response;
    }
};
void seed_prefix(state::StateData& data,const Lesson& lesson,
                 const QaMathPrerequisite& math) {
    // Only the preceding pages are prepared. Ordinary answers use the real
    // authored evaluators; compiler/math prerequisites are isolated QA models.
    // The target response and all subsequent activities remain untouched.
    std::size_t ordinary=0,compilers=0,mathematical=0;
    for(std::size_t page=0;page<reviewed_page;++page) {
        require(state::can_reach_page(data,lesson,page),
            "The U9 QA prefix attempted to skip an unreached page");
        state::save_current_page(data,lesson,page);
        for(const auto& block:lesson.pages[page].blocks) {
            if(block.type==BlockType::cpp_compiler) {
                require(block.output_check&&!block.output_check->cases.empty()&&
                        !block.compile_check,"The audited U9 compiler prerequisite changed");
                // As in production, only the first declared case is mandatory.
                const auto& item=block.output_check->cases.front();
                require(item.exit_code==0&&!item.outputs.empty(),
                    "The U9 compiler prerequisite acquired another process-result contract");
                windows::CppRunRequest request;
                request.revision=1;request.source_utf8=block.initial_code;
                request.project_files=block.project_files;
                request.source_files=block.source_files;request.cmake=block.cmake;
                request.stdin_utf8=item.standard_input;
                FreshExecution execution{1,true,true,0,item.standard_input,item.outputs.front()};
                execution.executable=item.executable;
                if(item.standard_error==StandardErrorExpectation::non_blank)
                    execution.standard_error="Synthetic isolated QA diagnostic";
                (void)windows::record_compiler_puzzle_case(
                    data,lesson,block,request,execution,"Disposable reader QA");
                require(windows::compiler_puzzle_completed(data,lesson,block),
                    "Required compiler prerequisite could not be seeded in QA");
                ++compilers;continue;
            }
            state::Response response;
            if(block.type==BlockType::math_puzzle) {
                require(block.stable_id==prefix_math_owner,
                    "The U9 prefix acquired another mathematical prerequisite");
                response=math.receipt();++mathematical;
            } else if(block.type==BlockType::multiple_choice) {
                require(block.correct_index&&*block.correct_index<block.options.size(),
                    "Missing canonical U9 prefix choice");
                response.selected=static_cast<std::int64_t>(*block.correct_index);
                response.answer=block.options[*block.correct_index];
                response.correct=evaluate_choice(block,*block.correct_index).correct;
                ++ordinary;
            } else if(block.type==BlockType::output_prediction) {
                require(!block.answers.empty(),"Missing canonical U9 prefix output");
                response.answer=block.answers.front();
                response.correct=evaluate_text_answer(block,response.answer).correct;
                ++ordinary;
            } else {
                require(block.type!=BlockType::fill_code&&block.type!=BlockType::order_code&&
                        block.type!=BlockType::inline_fill_code&&block.type!=BlockType::drag_fill_code,
                    "The U9 prefix acquired an unaudited ordinary prerequisite");
                continue;
            }
            require(!state::get_response(data,lesson,block)&&response.correct,
                "The U9 QA prefix would replace a response or seed an incorrect answer");
            state::mark_response_reviewed(response,block);
            state::put_response(data,lesson,block,response);
        }
    }
    require(ordinary==5&&compilers==5&&mathematical==1,
        "The audited U9 QA prerequisite inventory changed");
    require(state::can_reach_page(data,lesson,reviewed_page),
        "The U9 QA prefix did not reach the reviewed page");
    state::validate_lesson_state(data,lesson);
}
struct HiddenView {
    WindowsEnvironment env;
    Lesson lesson;
    const QaMathPrerequisite& math;
    std::atomic<unsigned> math_restores{0};
    HWND host=nullptr,view=nullptr;
    HiddenView(Lesson model,const state::StateData& state,const QaMathPrerequisite& prerequisite)
        :env(resolve_windows_environment({ProfileId::Codex,
            L"general-choice-reading-"+std::to_wstring(GetCurrentProcessId())+
                L"-"+std::to_wstring(GetTickCount64()),false})),
         lesson(std::move(model)),math(prerequisite) {
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
        try { open(); }
        catch(...) {
            if(view){DestroyWindow(view);view=nullptr;}
            DestroyWindow(host);host=nullptr;throw;
        }
    }
    ~HiddenView() {
        if(view){(void)flush_lesson_view(view);DestroyWindow(view);}
        if(host)DestroyWindow(host);
    }
    void open() {
        const auto previous_restores=math_restores.load();
        LessonViewHooks hooks;
        hooks.validate_math=[this](std::string_view source,std::string_view answers,
                                  std::string_view language,std::stop_token stop) {
            require(!stop.stop_requested(),"U9 prerequisite restore was cancelled");
            auto reply=math.validate(source,answers,language);
            ++math_restores;return reply;
        };
        view=create_lesson_view(host,env,lesson,std::move(hooks));
        require(view!=nullptr,"Cannot create the real U9 lesson view");
        MoveWindow(view,0,0,1100,850,TRUE);
        const auto deadline=GetTickCount64()+30000;
        do {
            pump();
            const auto page=lesson_page_state(view);
            // can_previous remains false while saved math is still pending.
            if(math_restores.load()>previous_restores&&page.index==reviewed_page&&page.can_previous)break;
            MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
        } while(GetTickCount64()<deadline);
        const auto page=lesson_page_state(view);
        require(math_restores.load()>previous_restores&&page.index==reviewed_page&&page.can_previous,
            "The U9 reading page did not finish restoring its QA prerequisites");
        require(!IsWindowVisible(host),"Hidden reader QA activated the desktop");
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
    const auto receipt=state::get_response(read_lesson_state(view.view),
        view.lesson,choice);
    require(receipt&&receipt->correct&&receipt->selected==0&&
            !receipt->historical_choice&&
            state::selected_choice_text(*receipt,choice)==newer&&
            state::choice_correct_feedback(*receipt,choice)==choice.explanation,
        "A valid previous success was lost, duplicated or treated as a new puzzle");
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
        const QaMathPrerequisite math(lesson);
        seed_prefix(qa,lesson,math);
        // The selected answer predates the editorial correction. Its exact
        // legacy storage key and opaque attempt metadata must remain owned
        // by this same activity after the revised words appear.
        const auto& choice=lesson.pages.at(7).blocks.at(2);
        state::Response previous;
        previous.selected=0;
        previous.correct=true;
        previous.raw_json=R"({"incorrectAttempts":3,"opaque":{"cursor":11}})";
        const auto key=exercise_key(lesson,choice);
        state::put_response(qa,key,previous);
        require(state::response_storage_key(lesson,choice)==key,
            "A wording-only change moved the historic choice response");
        const auto original_receipt=state::string_value(qa,"learning_progress",key);
        state::save_current_page(qa,lesson,7);
        HiddenView view(std::move(lesson),qa,math);
        check_reading(view);
        view.reopen();
        check_reading(view);
        const auto reopened=read_lesson_state(view.view);
        require(state::string_value(reopened,"learning_progress",key)==original_receipt,
            "Reopening the reviewed reading changed a successful historical response");
        std::cout<<"PASS general choice reading: original/current semantic spans, "
            "authenticated reading archives and original success across reopening; "
            "isolated prerequisite models: five compilers and one math activity\n";
    } catch(const std::exception& error) {
        std::cerr<<"FAIL general choice reading: "<<error.what()<<'\n';
        outcome=1;
    }
    OleUninitialize();
    return outcome;
}
