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
        check(block.output_check&&block.output_check->cases.size()>1,
            "Semantic compiler QA requires distinct authenticated output cases");
        const auto index=static_cast<std::size_t>(item.GetNamedNumber(L"case"));
        const auto& objective=block.output_check->cases.at(index);
        windows::CppRunRequest request;request.request_id=count+1;request.revision=count+1;
        request.source_utf8=block.initial_code;request.stdin_utf8=objective.standard_input;
        request.project_files=block.project_files;request.source_files=block.source_files;request.cmake=block.cmake;
        request.project_key=curriculum::draft_key("programming",lesson,block);
        request.projects_directory=environment.paths.root/"semantic-compiler-runs";
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
void same_region(const LessonReadingRegion& expected,const LessonReadingRegion& actual) {
    check(expected.key==actual.key&&expected.anchor==actual.anchor&&
        expected.source_text==actual.source_text&&expected.block_index==actual.block_index&&
        expected.feedback==actual.feedback&&expected.correct==actual.correct&&
        expected.passive_label==actual.passive_label&&
        expected.reading_context==actual.reading_context&&expected.math_mode==actual.math_mode,
        "A retirement archive changed the actual old reading source, identity or context");
    check(expected.text_semantics==actual.text_semantics,
        "A retirement reading borrowed current meanings or composed feedback with the wrong descriptor");
}
struct Counts {
    std::size_t pages=0,regions=0,marked=0,root=0,correct=0,reviewed=0,compilations=0;
    std::size_t compiler_feedback=0,prior_choice_feedback=0;
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
        const auto environment=resolve_windows_environment({ProfileId::Codex,
            L"semantic-retirement-"+std::to_wstring(GetCurrentProcessId())+L"-"+
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
            check(archived.page_identity==previous_page.narration_page_identity&&regions.size()==prior.size(),
                "Retirement failed to reconstruct the exact published page identity or supported script");
            for(std::size_t i=0;i<prior.size();++i) {
                same_region(prior[i],regions[i]);++counts.regions;
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
                        check(regions[i].text_semantics&&std::ranges::any_of(regions[i].text_semantics->spans,
                            [](const auto& span){return span.text=="resultado"&&
                                span.role==curriculum::SemanticRole::output&&
                                span.reading==curriculum::SemanticReading::natural;}),
                            "The actual compiler archive lost the first sealed case's authored role/reading");
                        const auto& first=owner.text_semantics.at("/outputCheck/cases/0/explanation");
                        const auto& optional=owner.text_semantics.at("/outputCheck/cases/1/explanation");
                        check(first.source==optional.source&&first.spans!=optional.spans,
                            "Compiler semantic QA must distinguish identical explanations by sealed origin");
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
                    windows::compiler_puzzle_case_count(after,current,block)==2,
                    "Opening retirement semantics rewrote real compiler receipt ownership");
            }
        }
        ++counts.pages;
    }
    check(state::read_bytes(source_file)==original_bytes,"Semantic view QA changed a predecessor document");
    check(state::read_bytes(state_file)==state_bytes,"Semantic view QA changed its original answer source");
}
}

int wmain(int argc,wchar_t** argv)try {
    check(argc==2,"Expected the isolated semantic retirement fixture directory");
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
    check(counts.root>0&&counts.marked>0&&counts.correct>0&&counts.reviewed>0&&counts.compilations==2&&
        counts.compiler_feedback>0&&counts.prior_choice_feedback>0,
        "Semantic view QA omitted root, exact intent, feedback verdicts or real multicase execution");
    check(GetForegroundWindow()==foreground&&GetFocus()==focus,
        "Hidden semantic QA changed focus or the foreground application");
    FreeLibrary(edit);
    std::cout<<"PASS semantic retirement view: "<<counts.pages<<" historical page/verdict views, "
        <<counts.regions<<" exact supported reading regions, "<<counts.marked
        <<" authored meanings, exact root/correct/reviewed feedback, and two real MSVC cases. "
        <<"Isolated QA; no playback, human listening acceptance or learner credit.\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL semantic retirement view: "<<error.what()<<'\n';return 1;
}
