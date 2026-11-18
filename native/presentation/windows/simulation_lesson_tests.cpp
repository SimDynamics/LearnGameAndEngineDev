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
#include <cstdlib>
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
    for(const auto payload:{L"{unknown}",L"{value",L"value}",
            L"{countedIndex}",L"{originIndex}",L"{value}:{originIndex}"}) {
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
    check(argc==4,"Expected numeric and text simulation examples plus the stateLayout fixture");
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
    check(trace.source_indices==std::vector<std::size_t>{2,6,1,4,7,0,3,8,5} &&
          trace.frames[1].explanation.find("casilla 0 de la copia ordenada, que procede del índice original 2")!=std::string::npos &&
          trace.frames[2].explanation.find("casilla 1, procedente del índice original 6")!=std::string::npos &&
          trace.frames[3].phase==curriculum::SimulationPhase::new_maximum &&
          trace.frames[3].explanation.find("casilla") == std::string::npos,
        "The documented numeric example lost the calculated counted/origin indices");
    invalid_contracts(source);
    empty_text_contracts(source);

    // Consume the exact documented text example, not a hand-transcribed
    // approximation. Neither the lesson parser nor QA may normalize empty
    // strings into absent items or spaces into the empty string.
    const auto text_bytes=state::read_bytes(std::filesystem::path(wide(argv[3])));
    const std::string text_example(text_bytes.begin(),text_bytes.end());
    const auto original_position=source.find(example);
    check(original_position!=std::string::npos,
        "Cannot locate the numeric example inside its synthetic lesson");
    const auto text_source=source.substr(0,original_position)+text_example+
        source.substr(original_position+example.size());
    const auto text_lesson=windows::parse_lesson_json(text_source,"es");
    const auto text_metadata=windows::parse_lesson_progress_metadata(text_source,"es");
    const auto& text_block=text_lesson.pages.at(0).blocks.at(1);
    check(text_block.type==curriculum::BlockType::simulation&&text_block.simulation&&
        text_metadata.pages.at(0).blocks.at(1).simulation==text_block.simulation&&
        text_block.simulation->text_values,
        "Actual text example differs between full and progress loading");
    const auto& text_spec=*text_block.simulation;
    struct TextExampleCase {
        const char* scenario;
        std::size_t groups,maximum;
        std::vector<std::string> leaders;
        std::string output;
    };
    const std::vector<TextExampleCase> text_example_cases{
        {"initial",2,2,{"patio"},"[]=1\n[patio]=2\n"},
        {"coleccion_vacia",0,0,{},""},
        {"cadena_vacia",1,1,{""},"[]=1\n"},
        {"patio_repetido",2,2,{"patio"},"[]=1\n[patio]=2\n"},
        {"vacia_y_espacio",2,1,{""," "},"[]=1\n[ ]=1\n"},
        {"empate_y_espacios",3,1,{""," ","casa"},"[]=1\n[ ]=1\n[casa]=1\n"},
        {"desorden_y_repeticiones",3,2,{"sol"},"[]=1\n[ ]=1\n[sol]=2\n"}
    };
    check(text_spec.scenarios.size()+1==text_example_cases.size(),
        "The documented text example lost a scenario");
    for(std::size_t i=0;i<text_example_cases.size();++i){
        const auto& expected=text_example_cases[i];
        const auto& applied=i==0?text_spec.values:text_spec.scenarios[i-1].values;
        check(i==0||text_spec.scenarios[i-1].id==expected.scenario,
            "A documented text example scenario changed its identity/order");
        const auto computed=curriculum::build_simulation_trace(text_spec,applied);
        const auto& final=computed.frames.back();
        std::vector<std::string> leaders;
        for(const auto& value:final.leaders){
            check(std::holds_alternative<std::string>(value),
                "A text example candidate became a numeric value");
            leaders.push_back(std::get<std::string>(value));
        }
        check(computed.groups.size()==expected.groups&&
            final.maximum==expected.maximum&&
            final.output==expected.output&&leaders==expected.leaders,
            "The real text example's groups, empty/space candidates or stdout changed");
        if(std::string_view(expected.scenario)=="desorden_y_repeticiones")
            check(computed.source_indices==std::vector<std::size_t>{3,1,0,2} &&
                computed.frames[1].explanation.find("casilla 0 de la copia ordenada, procedente del índice original 3")!=std::string::npos &&
                computed.frames[6].explanation.find("casilla 3, procedente del índice original 2")!=std::string::npos &&
                computed.frames[6].phase==curriculum::SimulationPhase::advance,
                "The documented text scenario confused repeated labels or empty-string origin indices");
    }

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
    // The actual native reader, its console captions and the simulation all
    // share this RichEdit zoom helper. Verify Win32 accepts the fractions;
    // neither zooming nor approximating 101% may rewrite student text.
    {
        Window zoom_field{CreateWindowExW(0,MSFTEDIT_CLASS,L"",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE,0,0,320,120,host.value,
            nullptr,GetModuleHandleW(nullptr),nullptr)};
        check(zoom_field.value,"Cannot create native RichEdit zoom test");
        constexpr std::string_view sample="borrador conservado\nsegunda línea";
        set_edit_text(zoom_field.value,sample);
        for(const int requested:{25,50,100,101,110,125,150}){
            check(set_rich_text_zoom(zoom_field.value,requested),
                "The shared zoom helper was rejected by native RichEdit");
            int numerator=0,denominator=0;
            check(SendMessageW(zoom_field.value,EM_GETZOOM,
                reinterpret_cast<WPARAM>(&numerator),
                reinterpret_cast<LPARAM>(&denominator))!=FALSE&&
                numerator>=1&&numerator<=64&&denominator>=1&&denominator<=64,
                "The shared zoom helper produced an invalid native fraction");
            check(std::abs(MulDiv(numerator,100,denominator)-requested)<=1,
                "RichEdit accepted the zoom but rendered a different percentage");
            check(edit_text(zoom_field.value)==sample,
                "Applying native zoom changed the student's original text");
        }
        check(!set_rich_text_zoom(nullptr,150),
            "The shared zoom helper accepted a nonexistent native window");
    }
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
    check(changed==notifications+1&&lesson_page_state(view.value).index==0&&IsWindowEnabled(previous),
        "A simulation step failed to publish exactly one new narration inventory");
    check(state::encode(previous_state)==state::encode(read_lesson_state(view.value)),
        "Exploring a simulation changed progress, responses or drafts");
    const auto forward_notifications=changed;
    SendMessageW(previous,BM_CLICK,0,0);pump();
    check(changed==forward_notifications+1,
        "Returning to a simulation step emitted duplicate or missing narration updates");
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

    // Exercise the exact lesson-reader mount, not only the JSON adapter and
    // stand-alone view. Each disposable case gets its own lesson identity,
    // including one empty string that must never become a missing element.
    struct EmptyLabelCase {
        const wchar_t* json;
        std::vector<curriculum::SimulationValue> values;
    };
    const std::vector<EmptyLabelCase> empty_cases{
        {L"[]",{}},
        {L"[\"\"]",{std::string("")}},
        {L"[\"\",\"patio\",\"patio\"]",{std::string(""),std::string("patio"),std::string("patio")}},
        {L"[\"\",\" \"]",{std::string(""),std::string(" ")}}
    };
    const auto native_caption=[](HWND target) {
        check(target!=nullptr,"Missing native control in the empty-label reader");
        std::wstring buffer(static_cast<std::size_t>(GetWindowTextLengthW(target))+1,L'\0');
        buffer.resize(static_cast<std::size_t>(GetWindowTextW(target,buffer.data(),
            static_cast<int>(buffer.size()))));
        return utf8(buffer);
    };
    for(std::size_t index=0;index<empty_cases.size();++index) {
        const auto& sample=empty_cases[index];
        auto empty_root=JsonObject::Parse(winrt::to_hstring(source));
        empty_root.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(
            "qa_simulation_empty_label_reader_"+std::to_string(index))));
        auto simulation=simulation_block(empty_root).GetNamedObject(L"simulation");
        simulation.Insert(L"valueType",JsonValue::CreateStringValue(L"text"));
        simulation.Insert(L"values",JsonArray::Parse(sample.json));
        JsonObject scenario;
        scenario.Insert(L"id",JsonValue::CreateStringValue(L"qa_empty"));
        scenario.Insert(L"label",JsonValue::CreateStringValue(L"Escenario vacío"));
        scenario.Insert(L"values",JsonArray::Parse(L"[]"));
        JsonArray scenarios;scenarios.Append(scenario);
        simulation.Insert(L"scenarios",scenarios);
        const auto json=winrt::to_string(empty_root.Stringify());
        const auto empty_lesson=windows::parse_lesson_json(json,"es");
        const auto empty_metadata=windows::parse_lesson_progress_metadata(json,"es");
        const auto& spec=empty_lesson.pages[0].blocks[1].simulation;
        check(spec&&spec->text_values&&spec->values==sample.values&&
            empty_metadata.pages[0].blocks[1].simulation==spec,
            "Empty string, significant space or zero elements changed at lesson load");
        const auto expected=curriculum::build_simulation_trace(*spec);
        Window mounted{create_lesson_view(host.value,environment,empty_lesson,hooks)};
        check(mounted.value,"Cannot mount a native lesson containing empty text labels");
        MoveWindow(mounted.value,0,0,1000,800,TRUE);pump();
        const auto child=descendant(mounted.value,1105);
        check(child,"Empty-label lesson did not mount the native simulation");
        const auto canvas=descendant(child,105),output=descendant(child,110),
            next_button=button_named(child,L"Siguiente"),
            previous_button=button_named(child,L"Anterior"),
            step_selector=descendant(child,121),input=descendant(child,115),
            format=descendant(child,120),apply=button_named(child,L"Aplicar datos"),
            select_scenario=descendant(child,103);
        check(canvas&&output&&next_button&&previous_button&&step_selector&&
            input&&format&&apply&&select_scenario,
            "Empty-label lesson omitted a required native simulation control");
        check(SendMessageW(step_selector,CB_GETCOUNT,0,0)==
                static_cast<LRESULT>(expected.frames.size())&&
            !IsWindowEnabled(previous_button),
            "Empty-label lesson exposed the wrong number of computed steps");
        const auto array_name=native_caption(canvas);
        check((array_name.find("No hay valores.")!=std::string::npos)==sample.values.empty(),
            "The array view confused [] with a nonempty array");
        if(!sample.values.empty()) {
            check(array_name.find("índice 0: \"\" (cadena vacía)")!=std::string::npos,
                "The first empty string was not represented as an actual indexed element");
            check(SendMessageW(format,CB_GETCURSEL,0,0)==1&&
                edit_text(input).find("[\"\"")!=std::string::npos,
                "An empty string was not editable in the explicit JSON text format");
        }
        const auto initial_state=state::encode(read_lesson_state(mounted.value));
        for(std::size_t stage=0;stage<expected.frames.size();++stage) {
            check(edit_text(output)==expected.frames[stage].output&&
                SendMessageW(step_selector,CB_GETCURSEL,0,0)==
                    static_cast<LRESULT>(stage),
                "Native lesson exposed future output or another step's state");
            if(stage+1<expected.frames.size()) {
                check(IsWindowEnabled(next_button),
                    "A non-terminal empty-label state disabled Next");
                SendMessageW(next_button,BM_CLICK,0,0);pump();
            }
        }
        check(!IsWindowEnabled(next_button),
            "The final empty-label decision offered an invented next step");
        for(std::size_t stage=expected.frames.size();stage>1;--stage) {
            check(IsWindowEnabled(previous_button),
                "Empty-label lesson cannot revisit a previously calculated state");
            SendMessageW(previous_button,BM_CLICK,0,0);pump();
            check(edit_text(output)==expected.frames[stage-2].output,
                "Backwards navigation retained a later group's output");
        }
        check(!IsWindowEnabled(previous_button)&&
            state::encode(read_lesson_state(mounted.value))==initial_state,
            "Exploring empty-label groups changed the lesson's graded progress");

        SendMessageW(select_scenario,CB_SETCURSEL,1,0);
        SendMessageW(child,WM_COMMAND,MAKEWPARAM(103,CBN_SELCHANGE),
            reinterpret_cast<LPARAM>(select_scenario));pump();
        check(native_caption(canvas).find("No hay valores.")!=std::string::npos&&
            edit_text(output).empty()&&!IsWindowEnabled(next_button),
            "Selecting an empty scenario retained old text groups or output");

        // Explicit editing has to distinguish two text items ["", " "]
        // from both [] and [""], without creating a curricular response.
        SendMessageW(format,CB_SETCURSEL,1,0);
        SendMessageW(child,WM_COMMAND,MAKEWPARAM(120,CBN_SELCHANGE),
            reinterpret_cast<LPARAM>(format));pump();
        check(SetWindowTextW(input,L"[\"\", \" \"]"),
            "Cannot enter an explicit empty label and a significant space");
        SendMessageW(apply,BM_CLICK,0,0);pump();
        const auto edited=curriculum::build_simulation_trace(*spec,
            std::vector<curriculum::SimulationValue>{
                std::string(""),std::string(" ")});
        check(SendMessageW(step_selector,CB_GETCOUNT,0,0)==
                static_cast<LRESULT>(edited.frames.size())&&
            native_caption(canvas).find("índice 0: \"\" (cadena vacía)")!=std::string::npos&&
            native_caption(canvas).find("índice 1: \" \" (1 espacio)")!=std::string::npos,
            "Applying text data lost either an empty string or a significant space");
        for(std::size_t stage=0;stage<edited.frames.size();++stage) {
            check(edit_text(output)==edited.frames[stage].output,
                "Text input editor or native output changed the calculated empty-label trace");
            if(stage+1<edited.frames.size()) {
                SendMessageW(next_button,BM_CLICK,0,0);pump();
            }
        }
        check(state::encode(read_lesson_state(mounted.value))==initial_state&&
            !state::get_response(read_lesson_state(mounted.value),
                empty_lesson,empty_lesson.pages[0].blocks[1]),
            "Editing a simulation was incorrectly stored as a graded response");
    }

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
