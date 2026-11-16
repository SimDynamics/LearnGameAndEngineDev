#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
void check(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(std::string(message));
}
void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}
std::string text(const std::filesystem::path& path) {
    const auto bytes=state::read_bytes(path);return {bytes.begin(),bytes.end()};
}
std::filesystem::path fixture(const std::filesystem::path& root,const winrt::hstring& name) {
    const std::filesystem::path relative(winrt::to_string(name));
    check(!relative.empty()&&!relative.is_absolute()&&relative.filename()==relative,
        "A semantic view fixture must be a local filename");
    const auto path=root/relative;
    check(std::filesystem::is_regular_file(path),"A semantic view fixture was not generated");
    return path;
}
struct HiddenView {
    HWND host=nullptr,view=nullptr;
    HiddenView(const WindowsEnvironment& environment,const curriculum::Lesson& lesson) {
        host=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Isolated semantic retirement QA",
            WS_OVERLAPPEDWINDOW,0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the hidden semantic QA host");
        view=create_lesson_view(host,environment,lesson);
        check(view!=nullptr,"Cannot create the real semantic lesson view");
        MoveWindow(view,0,0,1000,800,TRUE);pump();
        check(!IsWindowVisible(host),"Semantic retirement QA must keep its host hidden");
    }
    ~HiddenView(){if(view)DestroyWindow(view);if(host)DestroyWindow(host);}
    void page(std::size_t index) {
        auto current=lesson_page_state(view).index;
        while(current<index) {
            check(navigate_lesson_view(view,current+1),"QA cannot pass its ordinary answered prerequisite page");
            pump();const auto next=lesson_page_state(view).index;
            check(next==current+1,"QA navigation did not arrive at the requested page");current=next;
        }
        if(current>index) {
            check(navigate_lesson_view(view,index),"QA cannot reopen its visited page");pump();
        }
    }
    state::StateData saved() {
        check(flush_lesson_view(view),"Cannot persist the isolated semantic view");
        return read_lesson_state(view);
    }
};
void save(const WindowsEnvironment& environment,const state::StateData& data) {
    state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
}
void seed_answers(state::StateData& data,const curriculum::Lesson& lesson,
        std::size_t reviewed_page,bool reviewed) {
    // Isolated ordinary answers exercise presentation; they are not learner
    // achievements. Compiler seals are obtained only from the real runner below.
    state::prepare_lesson_state(data,lesson);
    for(std::size_t p=0;p<lesson.pages.size();++p)for(const auto& block:lesson.pages[p].blocks) {
        const auto type=block.type;
        if(type!=curriculum::BlockType::multiple_choice&&type!=curriculum::BlockType::output_prediction&&
            type!=curriculum::BlockType::fill_code&&type!=curriculum::BlockType::inline_fill_code&&
            type!=curriculum::BlockType::drag_fill_code&&type!=curriculum::BlockType::order_code)continue;
        state::Response response;response.correct=true;
        response.raw_json=R"({"opaque":{"semantic-retirement":"keep"},"incorrectAttempts":2})";
        if(block.correct_index)response.selected=static_cast<std::int64_t>(*block.correct_index);
        if(!block.answers.empty())response.answer=block.answers.front();
        for(const auto index:block.correct_order)response.order.push_back(static_cast<std::int64_t>(index));
        if(type==curriculum::BlockType::inline_fill_code||type==curriculum::BlockType::drag_fill_code) {
            JsonObject raw;JsonArray values;
            if(type==curriculum::BlockType::inline_fill_code) {
                for(const auto& answer:block.answers)values.Append(JsonValue::CreateStringValue(winrt::to_hstring(answer)));
                raw.Insert(L"answers",values);
            }else {
                for(const auto index:block.correct_order)values.Append(JsonValue::CreateNumberValue(static_cast<double>(index)));
                raw.Insert(L"slots",values);
            }
            response.raw_json=winrt::to_string(raw.Stringify());
        }
        if(reviewed&&p==reviewed_page&&type==curriculum::BlockType::output_prediction) {
            response.correct=false;response.answer="__semantic_qa_reviewed_error__";
            state::mark_response_reviewed(response,block);
        }
        state::put_response(data,lesson,block,response);
    }
    data.preferences["native_windows"]["ui_language"]=std::string("es");
    data.preferences["semantic_retirement_qa"]["opaque"]=std::string("\tConservar significado\r\n保存");
    state::Draft draft;draft.source="int conservar = 7;\r\n";draft.input="dato\t7\n";
    state::put_draft(data,"qa_unrelated_semantic_retirement",draft);
    state::validate_lesson_state(data,lesson);
}
std::size_t run_compiler_cases(const JsonObject& sample,const WindowsEnvironment& environment,
        const curriculum::Lesson& lesson,state::StateData& data) {
    if(!sample.HasKey(L"compilerCases"))return 0;
    std::size_t count=0;
    for(const auto& entry:sample.GetNamedArray(L"compilerCases")) {
        const auto item=entry.GetObject();
        const auto identity=winrt::to_string(item.GetNamedString(L"id"));
        const auto position=curriculum::stable_block_position(lesson,identity);
        check(position.has_value(),"Missing actual compiler owner for semantic feedback QA");
        const auto& block=lesson.pages[position->page].blocks[position->block];
        check(block.output_check&&!block.output_check->cases.empty(),
            "Semantic compiler QA requires authenticated output cases");
        const auto index=static_cast<std::size_t>(item.GetNamedNumber(L"case"));
        const auto& objective=block.output_check->cases.at(index);
        windows::CppRunRequest request;request.request_id=count+1;request.revision=count+1;
        request.source_utf8=block.initial_code;request.stdin_utf8=objective.standard_input;
        request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
        request.project_key=curriculum::draft_key("programming",lesson,block);
        request.projects_directory=environment.paths.root/"p";
        auto running=std::async(std::launch::async,[request]{return windows::run_cpp(request);});
        while(running.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
            pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
        }
        const auto result=running.get();const auto execution=windows::capture_cpp_execution(request,result);
        const auto verdict=windows::evaluate_cpp_output_check(*block.output_check,execution,request.revision);
        check(result.compilation.succeeded()&&result.execution.status==windows::CppPhaseStatus::completed&&
            verdict.verdict==curriculum::OutputVerdict::correct,
            "The real semantic compiler program did not satisfy its authored output case");
        const auto feedback=std::string("Correcto")+(verdict.feedback.empty()?"":"\n"+verdict.feedback);
        check(windows::record_compiler_puzzle_case(data,lesson,block,request,execution,feedback),
            "The native compiler evaluator rejected a real successful QA execution");
        ++count;
        const auto success=windows::compiler_puzzle_success_feedback(data,lesson,block);
        check(success&&success->correct&&success->source_path=="/outputCheck/cases/0/explanation",
            "Identical feedback strings selected an unsealed optional case descriptor");
    }
    return count;
}
std::vector<LessonReadingRegion> supported_regions(std::vector<LessonReadingRegion> regions,
        const curriculum::Page& page) {
    // Math metadata is compared completely by the full/progress loader test.
    // This view test covers the historical reader's existing C++/prose surfaces;
    // it does not claim reconstruction of a historical math widget script.
    std::erase_if(regions,[&](const auto& region){return region.block_index<page.blocks.size()&&
        page.blocks[region.block_index].type==curriculum::BlockType::math_puzzle;});
    return regions;
}
void describe_region(std::string_view label,std::size_t index,const LessonReadingRegion& region) {
    const auto quoted=[](std::string_view value){return winrt::to_string(
        JsonValue::CreateStringValue(winrt::to_hstring(value)).Stringify());};
    std::cerr<<label<<'['<<index<<"] key="<<quoted(region.key)<<" block="<<region.block_index
        <<" anchor="<<quoted(region.anchor)<<" source="<<quoted(region.source_text)
        <<" context="<<quoted(region.reading_context)<<" math="<<quoted(region.math_mode)
        <<" feedback="<<region.feedback<<" correct="<<region.correct
        <<" passive="<<region.passive_label<<" semantics="<<region.text_semantics.has_value()<<'\n';
    if(region.text_semantics)for(const auto& span:region.text_semantics->spans)
        std::cerr<<"  span "<<span.start<<':'<<span.end<<' '<<quoted(span.text)<<' '
            <<curriculum::semantic_role_name(span.role)<<'/'
            <<curriculum::semantic_reading_name(span.reading)<<'\n';
}
void same_region(const LessonReadingRegion& expected,const LessonReadingRegion& actual,
        std::string_view context,std::size_t index) {
    const bool identity=expected.key==actual.key&&expected.anchor==actual.anchor&&
        expected.source_text==actual.source_text&&expected.block_index==actual.block_index&&
        expected.feedback==actual.feedback&&expected.correct==actual.correct&&
        expected.passive_label==actual.passive_label&&
        expected.reading_context==actual.reading_context&&expected.math_mode==actual.math_mode;
    const bool meaning=expected.text_semantics==actual.text_semantics;
    if(!identity||!meaning) {
        std::cerr<<"SEMANTIC_REGION_MISMATCH "<<context<<" index="<<index<<'\n';
        describe_region("expected",index,expected);describe_region("archived",index,actual);
    }
    check(identity,"A retirement archive changed the actual old reading source, identity or context");
    check(meaning,"A retirement reading borrowed current meanings or composed feedback with the wrong descriptor");
}
struct Counts {
    std::size_t pages=0,regions=0,marked=0,root=0,correct=0,reviewed=0,compilations=0;
    std::size_t compiler_feedback=0,multicase_feedback=0,prior_choice_feedback=0;
};
void verify_history(const std::filesystem::path& directory,const JsonObject& sample,
        const JsonObject& history,const curriculum::Lesson& current,Counts& counts) {
    const auto source_file=fixture(directory,history.GetNamedString(L"source"));
    const auto original_bytes=state::read_bytes(source_file);
    const auto original=windows::load_lesson(source_file,"es");
    const auto state_file=sample.HasKey(L"stateSource")
        ?fixture(directory,sample.GetNamedString(L"stateSource")):source_file;
    const auto state_bytes=state::read_bytes(state_file);
    const auto state_source=windows::load_lesson(state_file,"es");
    const auto index=static_cast<std::size_t>(history.GetNamedNumber(L"index"));
    const auto& snapshot=current.state_layout.retirement_history.at(index);
    std::set<std::size_t> pages{0};
    for(const auto& owner:snapshot.retired) {
        const auto at=curriculum::stable_block_position(original,owner);
        check(at.has_value(),"The semantic predecessor lost its retired reading owner");pages.insert(at->page);
    }
    static std::size_t serial=0;
    for(const auto page:pages)for(int mode=0;mode<(sample.HasKey(L"compilerCases")?1:2);++mode) {
        const auto context=winrt::to_string(sample.GetNamedString(L"name"))+" history="+
            std::to_string(index)+" page="+std::to_string(page)+" mode="+std::to_string(mode);
        const auto environment=resolve_windows_environment({ProfileId::Codex,
            L"sr-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++serial),false});
        check(!std::filesystem::exists(environment.paths.root),"Semantic view QA requires an unused isolated profile");
        initialize_profile_directories(environment.paths);
        state::StateData data;seed_answers(data,state_source,page,mode!=0);
        if(sample.HasKey(L"stateSource")) {
            bool prior_choice=false;
            for(const auto& prior_page:original.pages)for(const auto& block:prior_page.blocks)
                if(block.type==curriculum::BlockType::multiple_choice&&block.choice_revision) {
                    const auto at=curriculum::stable_block_position(state_source,block.stable_id);
                    check(at.has_value(),"The prior choice response source lost its stable owner");
                    const auto before=state::get_response(data,state_source,state_source.pages[at->page].blocks[at->block]);
                    const auto response=state::get_response(data,original,block);
                    check(before&&response&&before->raw_json==response->raw_json&&
                        before->correct==response->correct&&before->selected==response->selected,
                        "A general choice revision rewrote the earlier stored answer");
                    prior_choice=true;
                }
            check(prior_choice,"The semantic choice QA did not exercise an earlier answer across a real revision");
        }
        counts.compilations+=run_compiler_cases(sample,environment,original,data);save(environment,data);
        std::vector<LessonReadingRegion> prior;LessonPageState previous_page;
        {HiddenView view(environment,original);view.page(page);
            prior=supported_regions(lesson_reading_regions(view.view),original.pages[page]);
            previous_page=lesson_page_state(view.view);data=view.saved();
        }
        check(!prior.empty(),"The real previous semantic page has no reading regions");
        {HiddenView view(environment,current);view.page(page);
            const auto unchanged=read_lesson_state(view.view);
            const auto archives=retired_lesson_reading_archives(view.view);
            check(read_lesson_state(view.view)==unchanged,
                "Reading a semantic retirement archive mutated progress or compiler receipts");
            check(index<archives.size(),"The real view omitted a semantic retirement batch");
            const auto& archived=archives.at(index);
            const auto regions=supported_regions(archived.regions,original.pages[page]);
            const bool same_script=archived.page_identity==previous_page.narration_page_identity&&regions.size()==prior.size();
            if(!same_script) {
                std::cerr<<"SEMANTIC_SCRIPT_MISMATCH "<<context
                    <<" expected_identity="<<previous_page.narration_page_identity
                    <<" archived_identity="<<archived.page_identity
                    <<" expected_regions="<<prior.size()<<" archived_regions="<<regions.size()<<'\n';
                for(std::size_t i=0;i<prior.size();++i)describe_region("expected",i,prior[i]);
                for(std::size_t i=0;i<regions.size();++i)describe_region("archived",i,regions[i]);
            }
            check(same_script,"Retirement failed to reconstruct the exact published page identity or supported script");
            for(std::size_t i=0;i<prior.size();++i) {
                same_region(prior[i],regions[i],context,i);++counts.regions;
                if(prior[i].text_semantics) {
                    ++counts.marked;
                    counts.root+=prior[i].key.ends_with("/title");
                    if(prior[i].feedback) {
                        if(prior[i].correct)++counts.correct;else ++counts.reviewed;
                    }
                }
                if(prior[i].feedback&&prior[i].block_index<original.pages[page].blocks.size()) {
                    const auto& owner=original.pages[page].blocks[prior[i].block_index];
                    if(owner.type==curriculum::BlockType::cpp_compiler) {
                        const auto& first=owner.text_semantics.at("/outputCheck/cases/0/explanation");
                        check(regions[i].text_semantics&&!first.spans.empty()&&
                            std::ranges::all_of(first.spans,[&](const auto& authored){
                                return std::ranges::any_of(regions[i].text_semantics->spans,[&](const auto& span){
                                    return span.text==authored.text&&span.role==authored.role&&span.reading==authored.reading;
                                });
                            }),"The actual compiler archive lost the first sealed case's authored role/reading");
                        if(owner.output_check&&owner.output_check->cases.size()>1) {
                            const auto& optional=owner.text_semantics.at("/outputCheck/cases/1/explanation");
                            check(first.source==optional.source&&first.spans!=optional.spans&&
                                first.spans.front().role==curriculum::SemanticRole::output&&
                                first.spans.front().reading==curriculum::SemanticReading::natural,
                                "Multicase semantic QA must distinguish identical explanations by sealed origin");
                            ++counts.multicase_feedback;
                        }
                        ++counts.compiler_feedback;
                    }
                    if(sample.HasKey(L"stateSource")&&owner.type==curriculum::BlockType::multiple_choice) {
                        check(regions[i].text_semantics.has_value(),
                            "The exact historical choice explanation lost its authored descriptor");
                        ++counts.prior_choice_feedback;
                    }
                }
            }
            const auto after=view.saved();
            check(after.preferences.at("semantic_retirement_qa")==data.preferences.at("semantic_retirement_qa"),
                "Semantic presentation changed an opaque setting");
            const auto old_draft=state::get_draft(data,"qa_unrelated_semantic_retirement","");
            const auto draft=state::get_draft(after,"qa_unrelated_semantic_retirement","");
            check(old_draft.source==draft.source&&old_draft.input==draft.input,
                "Semantic presentation changed an unrelated compiler draft");
            if(sample.HasKey(L"compilerCases")) {
                const auto owner=winrt::to_string(sample.GetNamedArray(L"compilerCases").GetAt(0).GetObject().GetNamedString(L"id"));
                const auto at=curriculum::stable_block_position(current,owner);
                check(at.has_value(),"The multicase semantic fixture lost its active compiler");
                const auto& block=current.pages[at->page].blocks[at->block];
                const auto success=windows::compiler_puzzle_success_feedback(after,current,block);
                check(success&&success->source_path=="/outputCheck/cases/0/explanation"&&
                    windows::compiler_puzzle_case_count(after,current,block)==sample.GetNamedArray(L"compilerCases").Size(),
                    "Opening retirement semantics rewrote real compiler receipt ownership");
            }
        }
        ++counts.pages;
    }
    check(state::read_bytes(source_file)==original_bytes,"Semantic view QA changed a predecessor document");
    check(state::read_bytes(state_file)==state_bytes,"Semantic view QA changed its original answer source");
}

std::size_t project_controls(HWND view) {
    std::size_t count=0;
    EnumChildWindows(view,[](HWND child,LPARAM parameter)->BOOL {
        wchar_t kind[128]{};
        if(GetClassNameW(child,kind,128)&&
            std::wstring_view(kind)==L"LearnGameAndEngineDev.CppProjectControls")
            ++*reinterpret_cast<std::size_t*>(parameter);
        return TRUE;
    },reinterpret_cast<LPARAM>(&count));
    return count;
}

std::size_t verify_formatted_compiler_retirement(const std::filesystem::path& directory) {
    const auto manifest=JsonObject::Parse(winrt::to_hstring(text(directory/"manifest.json")));
    std::size_t executed=0;
    for(const auto& entry:manifest.GetNamedArray(L"cases")) {
        const auto sample=entry.GetObject();
        if(!sample.HasKey(L"retiredCompilerCases"))continue;
        const auto before_file=fixture(directory,sample.GetNamedString(L"before"));
        const auto after_file=fixture(directory,sample.GetNamedString(L"after"));
        const auto before_bytes=state::read_bytes(before_file),after_bytes=state::read_bytes(after_file);
        const auto before=windows::load_lesson(before_file,"es");
        const auto after=windows::load_lesson(after_file,"es");
        const auto progress=windows::load_lesson_progress_metadata(after_file,"es");
        for(const auto& run:sample.GetNamedArray(L"retiredCompilerCases")) {
            const auto spec=run.GetObject();
            const auto identity=winrt::to_string(spec.GetNamedString(L"id"));
            const auto at=curriculum::stable_block_position(before,identity);
            check(at.has_value(),"The formatted retirement QA lost its active predecessor");
            const auto& owner=before.pages[at->page].blocks[at->block];
            check(owner.type==curriculum::BlockType::cpp_compiler&&owner.cmake&&
                owner.source_format_original_initial_code&&owner.source_format_original_project_files&&
                owner.output_check&&!owner.output_check->cases.empty(),
                "Formatted retirement QA needs an existing formatted CMake objective");
            const auto retired=std::ranges::find(after.retired_blocks,identity,&curriculum::Block::stable_id);
            const auto catalog=std::ranges::find(progress.retired_blocks,identity,&curriculum::Block::stable_id);
            check(retired!=after.retired_blocks.end()&&catalog!=progress.retired_blocks.end()&&
                !curriculum::stable_block_position(after,identity)&&
                !curriculum::stable_block_position(progress,identity)&&
                owner.initial_code==retired->initial_code&&owner.project_files==retired->project_files&&
                retired->source_format_original_initial_code==owner.source_format_original_initial_code&&
                retired->source_format_original_project_files==owner.source_format_original_project_files&&
                catalog->source_format_original_initial_code==retired->source_format_original_initial_code&&
                catalog->source_format_original_project_files==retired->source_format_original_project_files,
                "Full/progress retirement changed the compiler source, original archive or active membership");
            const auto environment=resolve_windows_environment({ProfileId::Codex,
                L"sf-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                    std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(executed),false});
            check(!std::filesystem::exists(environment.paths.root),
                "Formatted retirement QA requires a new isolated profile");
            initialize_profile_directories(environment.paths);
            state::StateData data;seed_answers(data,before,at->page,false);
            const auto& objective=owner.output_check->cases.at(
                static_cast<std::size_t>(spec.GetNamedNumber(L"case")));
            windows::CppRunRequest request;request.request_id=executed+1;request.revision=executed+1;
            request.source_utf8=owner.initial_code;request.stdin_utf8=objective.standard_input;
            request.project_files=owner.project_files;request.source_files=owner.source_files;request.cmake=owner.cmake;
            request.project_key=curriculum::draft_key("programming",before,owner);
            request.projects_directory=environment.paths.root/"p";
            auto task=std::async(std::launch::async,[request]{return windows::run_cpp(request);});
            while(task.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
                pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
            }
            const auto result=task.get();const auto execution=windows::capture_cpp_execution(request,result);
            const auto verdict=windows::evaluate_cpp_output_check(*owner.output_check,execution,request.revision);
            if(!result.compilation.succeeded()||verdict.verdict!=curriculum::OutputVerdict::correct)
                std::cerr<<"FORMATTED_RETIREMENT_RUN "<<winrt::to_string(sample.GetNamedString(L"name"))
                    <<" configure="<<result.configuration.stdout_bytes<<result.configuration.stderr_bytes
                    <<" compile="<<result.compilation.stdout_bytes<<result.compilation.stderr_bytes
                    <<" execute="<<result.execution.stdout_bytes<<result.execution.stderr_bytes<<'\n';
            const auto feedback=std::string("Correcto")+(verdict.feedback.empty()?"":"\n"+verdict.feedback);
            check(result.compilation.succeeded()&&result.execution.status==windows::CppPhaseStatus::completed&&
                verdict.verdict==curriculum::OutputVerdict::correct&&
                windows::record_compiler_puzzle_case(data,before,owner,request,execution,feedback),
                "The formatted compiler retirement fixture did not earn a real CMake execution receipt");
            ++executed;
            state::Draft draft;draft.source=owner.initial_code;draft.input=objective.standard_input;
            draft.selection_start=3;draft.selection_end=3;draft.has_saved=true;
            draft.indentation_json=state::make_indentation_metadata(draft.source,{});
            state::put_draft(data,request.project_key,draft);save(environment,data);
            std::vector<LessonReadingRegion> previous;LessonPageState previous_page;
            {HiddenView view(environment,before);view.page(at->page);
                const auto ready=[&] {
                    const auto regions=lesson_reading_regions(view.view);
                    return std::ranges::any_of(regions,[&](const auto& region){
                        return region.block_index==at->block&&region.feedback&&region.correct;});
                };
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(!ready()&&std::chrono::steady_clock::now()<deadline) {
                    pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
                }
                check(ready()&&project_controls(view.view)>0,
                    "The genuine predecessor did not display its CMake controls and earned feedback");
                previous=lesson_reading_regions(view.view);previous_page=lesson_page_state(view.view);
                data=view.saved();
            }
            const auto original_receipts=data.preferences.at("native_windows");
            const auto original_drafts=data.preferences.at("cpp_lesson_drafts");
            const auto verify=[&](const state::StateData& current) {
                state::validate_lesson_state(current,after);
                check(current.preferences.at("native_windows")==original_receipts&&
                    current.preferences.at("cpp_lesson_drafts")==original_drafts&&
                    windows::compiler_puzzle_completed(current,after,*retired)&&
                    windows::compiler_puzzle_case_count(current,after,*retired)==1&&
                    windows::compiler_puzzle_completed(current,progress,*catalog),
                    "Retirement changed the actual receipt, draft or catalog ownership");
            };
            state::StateData saved;
            for(int reopen=0;reopen!=2;++reopen) {
                HiddenView view(environment,after);view.page(at->page);
                check(project_controls(view.view)==0,
                    "An archived formatted compiler created active CMake controls after retirement");
                const auto current=lesson_reading_regions(view.view);
                for(const auto& region:previous)if(region.block_index==at->block)
                    check(std::ranges::none_of(current,[&](const auto& active){return active.key==region.key;}),
                        "The retired compiler still appears in the active reader");
                const auto archives=retired_lesson_reading_archives(view.view);
                const auto index=static_cast<std::size_t>(sample.GetNamedArray(L"history").GetAt(
                    sample.GetNamedArray(L"history").Size()-1).GetObject().GetNamedNumber(L"index"));
                const auto& archived=archives.at(index);
                check(archived.page_identity==previous_page.narration_page_identity&&
                    archived.regions.size()==previous.size(),
                    "The retired compiler lost its exact published reader script");
                for(std::size_t i=0;i<previous.size();++i)
                    same_region(previous[i],archived.regions[i],"formatted compiler retirement",i);
                saved=view.saved();verify(saved);
            }
            {state::StateStore store(environment.paths.root);auto physical=store.load();
                verify(physical);
                // Opening an unchanged page prepares authenticated provenance
                // in memory; it does not mark the view dirty or force a write.
                // Compare both snapshots at that same adoption stage, retaining
                // every original preference, before explicitly persisting it.
                state::prepare_lesson_state(physical,after);
                check(physical==saved,
                    "Physical reopening changed more than authenticated retirement adoption");
                store.save(physical);
            }
            {state::StateStore store(environment.paths.root);const auto physical=store.load();
                check(physical==saved,"Explicitly saved retired compiler state changed on reopening");
                verify(physical);
            }
            const auto empty_environment=resolve_windows_environment({ProfileId::Codex,
                L"sfe-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                    std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(executed),false});
            check(!std::filesystem::exists(empty_environment.paths.root),
                "The unearned retirement control requires its own new QA profile");
            initialize_profile_directories(empty_environment.paths);
            state::StateData unearned;seed_answers(unearned,after,at->page,false);
            save(empty_environment,unearned);
            {HiddenView view(empty_environment,after);view.page(at->page);
                LessonViewHooks hooks;hooks.lesson_completed=[](HWND){};
                set_lesson_view_hooks(view.view,std::move(hooks));pump();
                check(project_controls(view.view)==0&&lesson_options_snapshot(view.view).finish_enabled,
                    "A retired compiler still blocks completion without a receipt");
                const auto fresh=view.saved();
                check(!windows::compiler_puzzle_completed(fresh,after,*retired)&&
                    windows::compiler_puzzle_case_count(fresh,after,*retired)==0,
                    "Opening a retired compiler fabricated a new execution achievement");
            }
            std::cout<<"PASS formatted compiler retirement "<<winrt::to_string(sample.GetNamedString(L"name"))
                <<": real CMake receipt, exact full/progress archive, drafts, hidden prior script, "
                <<"no active project controls or unearned gate, physical reopen\n";
        }
        check(state::read_bytes(before_file)==before_bytes&&state::read_bytes(after_file)==after_bytes,
            "Formatted retirement view QA changed an authenticated fixture");
    }
    check(executed==3,"Formatted retirement view QA omitted one-step, successive or marked compiler retirement");
    return executed;
}
}

int wmain(int argc,wchar_t** argv)try {
    check(argc==3,"Expected the isolated semantic and source-format retirement fixture directories");
    check(SUCCEEDED(OleInitialize(nullptr)),"Cannot initialize semantic Win32 QA controls");
    struct OleScope {~OleScope(){OleUninitialize();}} ole;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};
    check(InitCommonControlsEx(&controls)!=FALSE,"Cannot initialize native common controls");
    const auto edit=LoadLibraryW(L"Msftedit.dll");check(edit!=nullptr,"Cannot load native RichEdit");
    const auto foreground=GetForegroundWindow(),focus=GetFocus();
    const std::filesystem::path directory(argv[1]);
    const auto manifest=JsonObject::Parse(winrt::to_hstring(text(directory/"manifest.json")));
    Counts counts;
    for(const auto& entry:manifest.GetNamedArray(L"cases")) {
        const auto sample=entry.GetObject();
        const auto after_file=fixture(directory,sample.GetNamedString(L"after"));
        const auto original=state::read_bytes(after_file);
        const auto lesson=windows::load_lesson(after_file,"es");
        for(const auto& history:sample.GetNamedArray(L"history"))
            verify_history(directory,sample,history.GetObject(),lesson,counts);
        check(state::read_bytes(after_file)==original,"Semantic view QA changed the current fixture");
    }
    check(counts.root>0&&counts.marked>0&&counts.correct>0&&counts.reviewed>0&&counts.compilations==3&&
        counts.compiler_feedback>0&&counts.multicase_feedback>0&&counts.prior_choice_feedback>0,
        "Semantic view QA omitted root, exact intent, feedback verdicts or real multicase execution");
    const auto retired_compilations=verify_formatted_compiler_retirement(std::filesystem::path(argv[2]));
    check(GetForegroundWindow()==foreground&&GetFocus()==focus,
        "Hidden semantic QA changed focus or the foreground application");
    FreeLibrary(edit);
    std::cout<<"PASS semantic retirement view: "<<counts.pages<<" historical page/verdict views, "
        <<counts.regions<<" exact supported reading regions, "<<counts.marked
        <<" authored meanings, exact root/correct/reviewed feedback, three real MSVC cases and "
        <<retired_compilations<<" real CMake receipts retained after retiring formatted compilers. "
        <<"Isolated QA; no playback, human listening acceptance or learner credit.\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL semantic retirement view: "<<error.what()<<'\n';return 1;
}
