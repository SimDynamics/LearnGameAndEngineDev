#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/simulation.hpp"
#include "core/compiler_flow.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void pump() {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
struct Window {HWND value{};~Window(){if(IsWindow(value))DestroyWindow(value);}};
HWND descendant(HWND parent,int id) {
    struct Search {int id;HWND found{};} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL {
        auto& search=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)==search.id){search.found=item;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.found;
}
HWND button_named(HWND parent,std::wstring_view name) {
    struct Search {std::wstring_view name;HWND found{};} search{name};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL {
        auto& search=*reinterpret_cast<Search*>(raw);wchar_t kind[32]{},label[128]{};
        GetClassNameW(item,kind,32);GetWindowTextW(item,label,128);
        if(std::wstring_view(kind)==L"Button"&&std::wstring_view(label)==search.name){search.found=item;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.found;
}
JsonObject simulation_block(JsonObject root) {
    return root.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks").GetObjectAt(1);
}
void rejected_text(std::string_view text) {
    bool full=false,metadata=false;
    try {(void)windows::parse_lesson_json(text);}catch(...){full=true;}
    try {(void)windows::parse_lesson_progress_metadata(text);}catch(...){metadata=true;}
    check(full&&metadata,"Simulation contract differs between full and metadata loaders");
}
void rejected(const JsonObject& root) {rejected_text(winrt::to_string(root.Stringify()));}
void empty_text_contracts(std::string_view source) {
    struct Case {
        const wchar_t* json;
        std::vector<curriculum::SimulationValue> values;
    };
    const std::vector<Case> cases{
        {L"[]",{}},
        {L"[\"\"]",{std::string("")}},
        {L"[\"\",\"patio\",\"patio\"]",{std::string(""),std::string("patio"),std::string("patio")}},
        {L"[\"\",\" \"]",{std::string(""),std::string(" ")}}
    };
    for(const auto& sample:cases) {
        auto root=JsonObject::Parse(winrt::to_hstring(source));
        auto simulation=simulation_block(root).GetNamedObject(L"simulation");
        simulation.Insert(L"valueType",JsonValue::CreateStringValue(L"text"));
        simulation.Insert(L"values",JsonArray::Parse(sample.json));
        JsonObject scenario;
        scenario.Insert(L"id",JsonValue::CreateStringValue(L"empty-labels"));
        scenario.Insert(L"label",JsonValue::CreateStringValue(L"Etiquetas vacías y espacios"));
        scenario.Insert(L"values",JsonArray::Parse(sample.json));
        JsonArray scenarios;scenarios.Append(scenario);simulation.Insert(L"scenarios",scenarios);
        const auto authored=winrt::to_string(root.Stringify());
        const auto full=windows::parse_lesson_json(authored,"es");
        const auto progress=windows::parse_lesson_progress_metadata(authored,"es");
        const auto& spec=full.pages.at(0).blocks.at(1).simulation;
        check(spec&&progress.pages.at(0).blocks.at(1).simulation==spec,
            "Empty-label data has different full/progress loading contracts");
        check(spec->text_values&&spec->values==sample.values&&spec->scenarios.size()==1&&
            spec->scenarios.front().values==sample.values,
            "Loading changed empty labels, meaningful spaces or the number of values");
    }
}
void invalid_contracts(std::string_view source) {
    for(const auto id:{L"",L" \t\r\n",L"\u0085",L"\u2003"}) {
        auto root=JsonObject::Parse(winrt::to_hstring(source));
        simulation_block(root).Insert(L"id",JsonValue::CreateStringValue(id));rejected(root);
    }
    for(const auto path:{L"model",L"view",L"valueType"}) {
        auto root=JsonObject::Parse(winrt::to_hstring(source));
        simulation_block(root).GetNamedObject(L"simulation").Insert(path,JsonValue::CreateStringValue(L"unknown"));rejected(root);
    }
    for(const auto number:{-2147483649.0,2147483648.0,1.5}) {
        auto root=JsonObject::Parse(winrt::to_hstring(source));auto values=JsonArray{};
        values.Append(JsonValue::CreateNumberValue(number));
        simulation_block(root).GetNamedObject(L"simulation").Insert(L"values",values);rejected(root);
    }
    for(const auto payload:{L"[true]",L"[\"7\"]",L"{}"}) {
        auto root=JsonObject::Parse(winrt::to_hstring(source));
        simulation_block(root).GetNamedObject(L"simulation").Insert(L"values",JsonValue::Parse(payload));rejected(root);
    }
    for(const auto payload:{L"{unknown}",L"{value",L"value}"}) {
        auto root=JsonObject::Parse(winrt::to_hstring(source));
        simulation_block(root).GetNamedObject(L"simulation").Insert(L"outputTemplate",JsonValue::CreateStringValue(payload));rejected(root);
    }
    for(const auto invalid:{"\\ud800","\\udc00"}) {
        // Keep the malformed escape in the source bytes: converting an actual
        // lone UTF-16 surrogate to UTF-8 here could replace it before loading.
        const auto reject_surrogate=[&](const JsonObject& root) {
            auto raw=winrt::to_string(root.Stringify());
            const std::string marker="QA_INVALID_SURROGATE";const auto position=raw.find(marker);
            check(position!=raw.npos,"Missing malformed Unicode fixture marker");
            raw.replace(position,marker.size(),invalid);rejected_text(raw);
        };
        const auto surrogate=JsonValue::CreateStringValue(L"QA_INVALID_SURROGATE");
        for(const auto field:{L"title",L"description",L"id"}) {
            auto root=JsonObject::Parse(winrt::to_hstring(source));
            simulation_block(root).Insert(field,surrogate);
            reject_surrogate(root);
        }
        auto root=JsonObject::Parse(winrt::to_hstring(source));
        auto value=simulation_block(root).GetNamedObject(L"simulation");
        value.Insert(L"valueType",JsonValue::CreateStringValue(L"text"));
        value.Remove(L"scenarios");auto values=JsonArray{};
        values.Append(surrogate);value.Insert(L"values",values);reject_surrogate(root);
        root=JsonObject::Parse(winrt::to_hstring(source));
        simulation_block(root).GetNamedObject(L"simulation").GetNamedObject(L"explanations").Insert(
            L"initial",surrogate);reject_surrogate(root);
    }
    auto root=JsonObject::Parse(winrt::to_hstring(source));
    auto simulation=simulation_block(root).GetNamedObject(L"simulation");
    simulation.Insert(L"sort",JsonValue::CreateBooleanValue(false));
    simulation.Insert(L"values",JsonArray::Parse(L"[3,2]"));rejected(root);
    root=JsonObject::Parse(winrt::to_hstring(source));
    simulation_block(root).Insert(L"correctIndex",JsonValue::CreateNumberValue(0));rejected(root);
    root=JsonObject::Parse(winrt::to_hstring(source));
    simulation_block(root).GetNamedObject(L"simulation").GetNamedObject(L"labels").Remove(L"end");rejected(root);
    root=JsonObject::Parse(winrt::to_hstring(source));
    simulation_block(root).GetNamedObject(L"simulation").Insert(L"scenarios",JsonArray::Parse(
        LR"([{"id":"same","label":"Uno","values":[1]},{"id":"same","label":"Dos","values":[2]}])"));rejected(root);
}
}

int main(int argc,char** argv)try {
    check(argc==3,"Expected the reusable simulation example and its synthetic stateLayout fixture");
    const auto bytes=state::read_bytes(std::filesystem::path(wide(argv[1])));
    const std::string example(bytes.begin(),bytes.end());
    // The documentation example is loaded verbatim. Only the surrounding QA
    // lesson is synthetic; no current lesson or student state is rewritten.
    const std::string source=std::string(R"({"id":"qa_simulation_lesson","title":"Simulación nativa","pages":[{"blocks":[{"type":"paragraph","text":"Este párrafo conserva su propia lectura y su pausa."},)")+example+
        R"(]},{"blocks":[{"type":"paragraph","text":"Página posterior sin ejercicio artificial."}]}]})";
    const auto lesson=windows::parse_lesson_json(source,"es");
    const auto metadata=windows::parse_lesson_progress_metadata(source,"es");
    const auto& block=lesson.pages.at(0).blocks.at(1);
    check(block.type==curriculum::BlockType::simulation&&block.simulation&&
        metadata.pages.at(0).blocks.at(1).simulation==block.simulation,
        "Full and read-only loading disagree on the simulation contract");
    check(!block.output_check&&!block.compile_check,
        "Exploration unexpectedly became a graded compiler activity");
    const auto trace=curriculum::build_simulation_trace(*block.simulation);
    check(trace.frames.size()>2&&!trace.groups.empty(),"The authored example has no real computed traversal");
    invalid_contracts(source);
    empty_text_contracts(source);

    // Stable publications keep integer JSON tokens exactly, as required by
    // the pre-existing fingerprint scanner. The isolated block adapter can
    // accept a numerically integral JSON value, but a v2 lesson cannot.
    const auto layout_bytes=state::read_bytes(std::filesystem::path(wide(argv[2])));
    const auto stable_root=JsonObject::Parse(winrt::to_hstring(source));
    stable_root.Insert(L"stateLayout",JsonObject::Parse(winrt::to_hstring(std::string(layout_bytes.begin(),layout_bytes.end()))));
    const auto stable_source=winrt::to_string(stable_root.Stringify());
    check(windows::parse_lesson_json(stable_source,"es").state_layout.version==2&&
        windows::parse_lesson_progress_metadata(stable_source,"es").state_layout.version==2,
        "The authored stable simulation fixture is invalid");
    const auto decimal_token=[](std::string text,std::string_view suffix) {
        const auto values=text.find("\"values\":[");check(values!=text.npos,"Missing numeric fixture values");
        const auto end=text.find_first_of(",]",values+10);check(end!=text.npos,"Missing numeric fixture delimiter");
        text.insert(end,suffix);return text;
    };
    for(const auto suffix:{".0","e0"}) {
        const auto standalone=decimal_token(winrt::to_string(JsonObject::Parse(winrt::to_hstring(source)).Stringify()),suffix);
        check(windows::parse_lesson_json(standalone,"es").pages[0].blocks[1].simulation==block.simulation&&
            windows::parse_lesson_progress_metadata(standalone,"es").pages[0].blocks[1].simulation==block.simulation,
            "An integral standalone numeric token changed the model");
        rejected_text(decimal_token(stable_source,suffix));
    }

    // A reference keeps the same ordinary gate, without treating the simulator
    // as an exercise or accepting an invented simulation response.
    auto reference_lesson=lesson;
    curriculum::Block compiler;compiler.type=curriculum::BlockType::cpp_compiler;
    compiler.stable_id="qa_compiler";compiler.output_check=curriculum::OutputCheck{};
    compiler.output_check->cases.push_back({"",{"7"}});
    reference_lesson.state_layout.version=1;
    reference_lesson.pages.at(0).blocks.insert(reference_lesson.pages.at(0).blocks.begin()+1,compiler);
    reference_lesson.reference_after.push_back({"qa_compiler",{block.stable_id}});
    curriculum::validate_reference_relationships(reference_lesson);
    const auto& reference=reference_lesson.pages[0].blocks[2];
    check(!curriculum::compiler_reference_available(reference_lesson,0,reference,
        {{"qa_compiler",false}},[](const auto&){return false;}),"A simulation reference ignored its prerequisite");
    check(curriculum::compiler_reference_available(reference_lesson,0,reference,
        {{"qa_compiler",true}},[](const auto&){return true;}),"A completed prerequisite cannot reveal its simulation reference");

    check(SUCCEEDED(OleInitialize(nullptr)),"Cannot initialize the native simulation host");
    const auto edits=LoadLibraryW(L"Msftedit.dll");check(edits,"Missing native RichEdit");
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    reader_theme::prepare_fonts();
    Window host{CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Isolated simulation lesson QA",
        WS_OVERLAPPEDWINDOW,0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    check(host.value,"Cannot create a native lesson host");
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"simulation-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    initialize_profile_directories(environment.paths);
    state::Draft draft;draft.source="int conserva = 7;\r\n";draft.input="valor\t7\n";
    {
        state::StateStore store(environment.paths.root);auto data=store.load();
        data.preferences["qa_simulation_unrelated"]["keep"]=std::string("Conserva mis datos.");
        state::put_draft(data,"qa_simulation_unrelated_editor",draft);store.save(data);
    }
    std::size_t changed=0;
    LessonViewHooks hooks;hooks.content_changed=[&](HWND){++changed;};
    auto loaded=lesson;
    loaded.pages.at(0).blocks.at(1).text_semantics["/description"]={block.simulation->description,{}};
    Window view{create_lesson_view(host.value,environment,loaded,hooks)};
    check(view.value,"Cannot create the integrated simulation lesson");
    MoveWindow(view.value,0,0,1000,800,TRUE);pump();
    const auto simulator=descendant(view.value,1105);
    check(simulator,"The actual lesson did not create its SimulationView child");
    const auto before=lesson_reading_regions(view.value);
    const auto paragraph=std::ranges::find_if(before,[](const auto& region){return region.block_index==0;});
    check(paragraph!=before.end(),"The preceding paragraph disappeared");
    for(const auto& region:before)if(region.block_index==1)
        check(!region.is_exercise&&!region.required_compiler,"Simulation narration was assigned a grade gate");
    const auto described=std::ranges::find_if(before,[&](const auto& region){return region.source_text==block.simulation->description;});
    check(described!=before.end()&&described->text_semantics==loaded.pages[0].blocks[1].text_semantics.at("/description"),
        "Authored simulation description semantics were not preserved");
    const auto next=button_named(simulator,L"Siguiente"),previous=button_named(simulator,L"Anterior");
    check(next&&previous&&IsWindowEnabled(next)&&!IsWindowEnabled(previous),"Initial native simulation buttons are inconsistent");
    const auto previous_state=read_lesson_state(view.value);
    const auto notifications=changed;
    SendMessageW(next,BM_CLICK,0,0);pump();
    const auto after=lesson_reading_regions(view.value);
    const auto preserved=std::ranges::find_if(after,[&](const auto& region){return region.key==paragraph->key;});
    check(preserved!=after.end()&&preserved->source_text==paragraph->source_text&&
        preserved->anchor==paragraph->anchor&&preserved->text_semantics==paragraph->text_semantics,
        "A simulation step changed the preceding paragraph's pause identity");
    check(changed>notifications&&lesson_page_state(view.value).index==0&&IsWindowEnabled(previous),
        "The step changed the lesson page or failed to notify its visible reading regions");
    check(state::encode(previous_state)==state::encode(read_lesson_state(view.value)),
        "Exploring a simulation changed progress, responses or drafts");
    SendMessageW(previous,BM_CLICK,0,0);pump();
    const auto reversed=lesson_reading_regions(view.value);
    check(reversed.size()==before.size(),"Previous did not restore the visible narration inventory");
    for(std::size_t i=0;i<before.size();++i)
        check(reversed[i].key==before[i].key&&reversed[i].source_text==before[i].source_text,
            "Previous did not restore the exact state text and its identity");
    // A native control consumes Enter; the page-level shortcut must stand down.
    MSG key{};key.hwnd=next;key.message=WM_KEYDOWN;key.wParam=VK_RETURN;
    check(!dispatch_lesson_enter(key)&&lesson_page_state(view.value).index==0,
        "Enter in the simulator was intercepted by lesson navigation");
    check(lesson_page_state(view.value).can_next,
        "Exploration made the ungraded next page inaccessible");
    check(flush_lesson_view(view.value),"Cannot save isolated simulation state");
    const auto saved=read_lesson_state(view.value);
    const auto restored=state::get_draft(saved,"qa_simulation_unrelated_editor","","");
    check(restored.has_saved&&restored.source==draft.source&&restored.input==draft.input,
        "Simulation exploration altered another editor's saved draft");
    check(!state::get_response(saved,loaded,loaded.pages[0].blocks[1]),
        "Simulation controls generated a curricular response");
    DestroyWindow(view.value);view.value=nullptr;
    view.value=create_lesson_view(host.value,environment,loaded,hooks);pump();
    check(view.value&&flush_lesson_view(view.value),"Cannot reopen the simulation lesson");
    const auto reopened=read_lesson_state(view.value);
    check(saved.preferences==reopened.preferences,"Reopening the simulator changed preserved state");
    check(!IsWindowVisible(host.value),"Simulation QA activated a user desktop window");
    DestroyWindow(view.value);view.value=nullptr;

    // Exercise bookmark successor routing through the trusted model adapter.
    // Authenticated JSON retirement is covered separately by the authoring
    // tests; this fixture isolates the native visible-region contract.
    auto retirement=loaded;retirement.id="qa_simulation_retirement";
    curriculum::StateRetirementHistory history;history.pages=retirement.pages;
    for(const auto& page:history.pages) {
        curriculum::StatePage record;record.id=page.stable_id;
        for(const auto& item:page.blocks)record.blocks.push_back(item.stable_id);
        history.current.push_back(std::move(record));
    }
    const auto removed=retirement.pages[0].blocks[0];
    history.retired.push_back(removed.stable_id);
    retirement.state_layout.retirement_history.push_back(std::move(history));
    retirement.state_layout.retired.push_back({removed.stable_id,retirement.pages[0].stable_id,
        retirement.pages[0].blocks[1].stable_id,{},{}});
    retirement.pages[0].blocks.erase(retirement.pages[0].blocks.begin());
    view.value=create_lesson_view(host.value,environment,retirement);pump();
    check(view.value,"Cannot create the isolated retirement successor fixture");
    const auto current_regions=lesson_reading_regions(view.value);
    const auto archives=retired_lesson_reading_archives(view.value);
    check(archives.size()==1,"The pre-retirement narration inventory disappeared");
    const auto old=std::ranges::find_if(archives[0].regions,[&](const auto& region){return region.source_text==removed.text;});
    check(old!=archives[0].regions.end()&&!old->retired_successor.empty()&&
        std::ranges::any_of(current_regions,[&](const auto& region){return region.anchor.starts_with(old->retired_successor);}),
        "A retired paragraph cannot resume at its following simulation");
    for(const auto& region:archives[0].regions)if(region.block_index==1) {
        check(std::ranges::any_of(current_regions,[&](const auto& current){return current.key==region.key&&current.source_text==region.source_text;}),
            "An unchanged simulation title or description lost its archived identity");
    }
    DestroyWindow(view.value);view.value=nullptr;
    FreeLibrary(edits);OleUninitialize();
    std::cout<<"PASS simulation full/progress loaders, malformed contracts, gated reference, actual lesson controls, exact Previous, stable paragraph anchors, no grades, untouched drafts and physical reopen\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL simulation lesson: "<<error.what()<<'\n';return 1;}
