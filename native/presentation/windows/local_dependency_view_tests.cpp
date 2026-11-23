// Real LessonView controls for the sixteen authored local/input objectives.
// Each isolated profile earns its historical receipt through the BEFORE UI,
// then opens the reviewed model without rewriting either curricular file.
#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "core/profile.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
using curriculum::Block;
using curriculum::BlockType;
using curriculum::Lesson;
using curriculum::StatePosition;

std::string phase="fixture loading";
ULONGLONG started=0;
std::uint64_t request_sequence=400000;

void check(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(phase+": "+std::string(message));
}
void within_budget() {
    check(GetTickCount64()-started<1100000,"The isolated UI scenario exceeded its global time bound");
}
std::string bytes(const std::filesystem::path& path) {
    const auto value=state::read_bytes(path);
    return std::string(value.begin(),value.end());
}
std::string json_string(const JsonObject& object,std::wstring_view key) {
    return winrt::to_string(object.GetNamedString(winrt::hstring(key)));
}
std::string canonical(std::string_view raw) {
    return winrt::to_string(JsonObject::Parse(wide(raw)).Stringify());
}
std::string normalized(std::string_view value) {
    return curriculum::cpp_local_int_canonical_stdout(value);
}
StatePosition position(const Lesson& lesson,std::string_view identity) {
    const auto found=curriculum::stable_block_position(lesson,identity);
    check(found.has_value(),"A manifest identity is absent from its authenticated lesson");
    return *found;
}
const Block& block_at(const Lesson& lesson,std::string_view identity) {
    const auto found=position(lesson,identity);
    return lesson.pages.at(found.page).blocks.at(found.block);
}
std::size_t private_contracts(const Block& block) {
    return static_cast<std::size_t>(block.local_int_check.has_value())+
        static_cast<std::size_t>(block.local_string_check.has_value())+
        static_cast<std::size_t>(block.input_check.has_value())+
        static_cast<std::size_t>(block.local_role_check.has_value());
}

struct Solution { std::string activity,source; };
struct Plan {
    std::string activity,reference,fixed_source;
    std::optional<std::string> no_plan_source;
};
struct Fixture {
    std::string id;
    std::filesystem::path before_path,after_path;
    std::string before_sha256,after_sha256;
    Lesson before,after;
    std::vector<Solution> solutions;
    std::vector<Plan> plans;
};
const std::string& solution_for(const Fixture& fixture,std::string_view identity) {
    const auto found=std::ranges::find(fixture.solutions,identity,&Solution::activity);
    check(found!=fixture.solutions.end(),"A nonfocal compiler lacks an audited real solution");
    return found->source;
}
std::filesystem::path fixture_path(const std::filesystem::path& root,std::string_view relative) {
    const auto path=std::filesystem::path(relative);
    check(!path.empty()&&!path.is_absolute(),"The fixture manifest needs a relative file path");
    for(const auto& component:path)check(component!=L"..","The fixture path leaves its export directory");
    return root/path;
}
void verify_only_root_adoption(const Fixture& fixture) {
    auto normalized_after=JsonObject::Parse(wide(fixture.after.raw_json));
    const auto original=JsonObject::Parse(wide(fixture.before.raw_json));
    std::set<std::string> owners;
    for(const auto& plan:fixture.plans)check(owners.insert(plan.activity).second,"Repeated focal owner");
    std::size_t removed=0;
    for(const auto key:{L"compilerLocalIntChecks",L"compilerLocalStringChecks",
                       L"compilerInputChecks",L"compilerLocalRoleChecks"}) {
        if(!normalized_after.HasKey(key))continue;
        const auto declarations=normalized_after.GetNamedArray(key);
        for(std::uint32_t index=declarations.Size();index>0;--index) {
            if(owners.contains(json_string(declarations.GetObjectAt(index-1),L"activity"))) {
                declarations.RemoveAt(index-1);++removed;
            }
        }
        if(declarations.Size()==0&&!original.HasKey(key))normalized_after.Remove(key);
    }
    check(removed==fixture.plans.size()&&
            winrt::to_string(normalized_after.Stringify())==canonical(fixture.before.raw_json),
        "The reviewed fixture changed something other than its declared private owner roots");
    check(fixture.before.compiler_puzzles_required&&fixture.after.compiler_puzzles_required,
        "UI QA changed the published required-compiler policy");
    for(const auto& plan:fixture.plans) {
        const auto& former=block_at(fixture.before,plan.activity);
        const auto& current=block_at(fixture.after,plan.activity);
        const auto& former_reference=block_at(fixture.before,plan.reference);
        const auto& current_reference=block_at(fixture.after,plan.reference);
        check(curriculum::reference_activity(fixture.before,former_reference)==
                std::optional(position(fixture.before,plan.activity))&&
                curriculum::reference_activity(fixture.after,current_reference)==
                std::optional(position(fixture.after,plan.activity)),
            "The manifest reference is not authored for this focal owner");
        check(former.type==BlockType::cpp_compiler&&current.type==BlockType::cpp_compiler&&
                former.output_check&&current.output_check&&!current.output_check->cases.empty()&&
                private_contracts(former)==0&&private_contracts(current)==1&&
                former.address.page==current.address.page&&former.address.block==current.address.block&&
                canonical(former.raw_json)==canonical(current.raw_json)&&
                curriculum::draft_key("programming",fixture.before,former)==
                    curriculum::draft_key("programming",fixture.after,current)&&
                solution_for(fixture,plan.activity)!=plan.fixed_source,
            "A focal owner moved, changed public payload or already had its new private check");
    }
    std::set<std::string> compiler_ids;
    for(const auto& page:fixture.after.pages)for(const auto& block:page.blocks)
        if(block.type==BlockType::cpp_compiler)compiler_ids.insert(block.stable_id);
    check(compiler_ids.size()==fixture.solutions.size(),"The fixture does not enumerate every actual compiler");
    for(const auto& solution:fixture.solutions)
        check(compiler_ids.erase(solution.activity)==1,"Repeated or unrelated prerequisite solution");
    check(compiler_ids.empty(),"An authentic compiler lacks its solution");
}
std::vector<Fixture> load_manifest(const std::filesystem::path& manifest) {
    windows::ensure_winrt_apartment();
    const auto document=JsonObject::Parse(wide(bytes(manifest)));
    check(document.GetNamedNumber(L"version")==1&&
            json_string(document,L"publishedCommit")=="2ca32b2337178d1effa3a2c81759e2493cd746d0",
        "The UI manifest is not the audited main publication and schema");
    std::vector<Fixture> result;
    std::set<std::string> names;
    std::size_t owner_count=0;
    for(const auto& raw:document.GetNamedArray(L"lessons")) {
        const auto item=raw.GetObject();
        Fixture fixture;
        fixture.id=json_string(item,L"id");
        check(names.insert(fixture.id).second,"Repeated lesson manifest entry");
        fixture.before_path=fixture_path(manifest.parent_path(),json_string(item,L"before"));
        fixture.after_path=fixture_path(manifest.parent_path(),json_string(item,L"after"));
        fixture.before_sha256=json_string(item,L"beforeSha256");
        fixture.after_sha256=json_string(item,L"afterSha256");
        check(state::sha256(state::read_bytes(fixture.before_path))==fixture.before_sha256&&
                state::sha256(state::read_bytes(fixture.after_path))==fixture.after_sha256,
            "A lesson fixture differs from its pinned manifest bytes");
        fixture.before=windows::load_lesson(fixture.before_path);
        fixture.after=windows::load_lesson(fixture.after_path);
        for(const auto& value:item.GetNamedArray(L"solutions")) {
            const auto entry=value.GetObject();
            Solution solution{json_string(entry,L"activity"),json_string(entry,L"source")};
            check(!solution.source.empty()&&state::sha256(solution.source)==json_string(entry,L"sourceSha256"),
                "An audited solution differs from its manifest source hash");
            fixture.solutions.push_back(std::move(solution));
        }
        for(const auto& value:item.GetNamedArray(L"plans")) {
            const auto entry=value.GetObject();
            Plan plan{json_string(entry,L"activity"),json_string(entry,L"reference"),
                json_string(entry,L"fixedSource"),std::nullopt};
            check(!plan.fixed_source.empty()&&state::sha256(plan.fixed_source)==json_string(entry,L"fixedSourceSha256"),
                "A fixed-report counterexample differs from its pinned source");
            if(entry.HasKey(L"noPlanSource")) {
                plan.no_plan_source=json_string(entry,L"noPlanSource");
                check(state::sha256(*plan.no_plan_source)==json_string(entry,L"noPlanSourceSha256"),
                    "The no-plan counterexample differs from its pinned source");
            }
            fixture.plans.push_back(std::move(plan));++owner_count;
        }
        verify_only_root_adoption(fixture);
        result.push_back(std::move(fixture));
    }
    check(result.size()==9&&owner_count==16,"The UI manifest must cover nine lessons and sixteen focal owners");
    return result;
}

void prepare_quizzes(state::StateData& data,const Lesson& lesson,std::size_t through) {
    // Authored answers evaluated by the real core are QA setup only. No claim
    // is made that these nonfocal quizzes were answered through their controls.
    for(std::size_t page=0;page<=through;++page)for(const auto& block:lesson.pages.at(page).blocks) {
        if(const auto old=state::get_response(data,lesson,block);old&&old->correct)continue;
        state::Response response;
        switch(block.type) {
        case BlockType::multiple_choice:
            check(block.correct_index.has_value(),"A prerequisite choice has no authored answer");
            response.selected=static_cast<std::int64_t>(*block.correct_index);
            response.correct=curriculum::evaluate_choice(block,*block.correct_index).correct;
            break;
        case BlockType::fill_code:
        case BlockType::output_prediction:
            check(!block.answers.empty(),"A prerequisite text quiz has no authored answer");
            response.answer=block.answers.front();
            response.correct=curriculum::evaluate_text_answer(block,response.answer).correct;
            break;
        case BlockType::order_code:
            for(const auto index:block.correct_order)response.order.push_back(static_cast<std::int64_t>(index));
            response.correct=curriculum::evaluate_order(block,block.correct_order).correct;
            break;
        case BlockType::inline_fill_code: {
            JsonArray answers;
            for(const auto& answer:block.answers)answers.Append(JsonValue::CreateStringValue(wide(answer)));
            JsonObject raw;raw.Insert(L"answers",answers);response.raw_json=winrt::to_string(raw.Stringify());
            response.correct=curriculum::evaluate_hole_answers(block,block.answers).correct;
            break;
        }
        case BlockType::drag_fill_code: {
            JsonArray slots;
            for(const auto slot:block.correct_order)slots.Append(JsonValue::CreateNumberValue(static_cast<double>(slot)));
            JsonObject raw;raw.Insert(L"slots",slots);response.raw_json=winrt::to_string(raw.Stringify());
            response.correct=curriculum::evaluate_drag_answers(block,block.correct_order).correct;
            break;
        }
        case BlockType::math_puzzle:
            check(false,"A mathematical prerequisite needs its own explicitly audited preparation");
            break;
        default:continue;
        }
        check(response.correct,"An authored nonfocal quiz answer failed its real evaluator");
        state::mark_response_reviewed(response,block);
        state::put_response(data,lesson,block,response);
    }
}
windows::CppRunRequest request_for(const WindowsEnvironment& environment,const Lesson& lesson,
        const Block& block,std::string source,std::string input) {
    windows::CppRunRequest request;
    request.request_id=++request_sequence;request.revision=request_sequence;
    request.project_key=curriculum::draft_key("programming",lesson,block);
    request.projects_directory=environment.paths.projects;
    request.source_utf8=std::move(source);request.stdin_utf8=std::move(input);
    request.compile_timeout=std::chrono::seconds(60);request.execution_timeout=std::chrono::seconds(8);
    request.local_int_check=block.local_int_check;request.local_string_check=block.local_string_check;
    request.input_check=block.input_check;request.local_role_check=block.local_role_check;
    return request;
}
void prepare_compilers(state::StateData& data,const Fixture& fixture,const Lesson& lesson,
        const Plan& plan,std::size_t through,const WindowsEnvironment& environment) {
    // Each nonfocal receipt comes from an actual native compilation/execution,
    // carrying all four private contracts when present in this exact model.
    for(std::size_t page=0;page<=through;++page)for(const auto& block:lesson.pages.at(page).blocks) {
        if(block.type!=BlockType::cpp_compiler||block.stable_id==plan.activity||
           windows::compiler_puzzle_completed(data,lesson,block))continue;
        within_budget();
        check(block.output_check&&!block.output_check->cases.empty(),"A nonfocal compiler has no public case");
        const auto request=request_for(environment,lesson,block,solution_for(fixture,block.stable_id),
            block.output_check->cases.front().standard_input);
        check(windows::compiler_runtime_request_allowed(block,request),"A nonfocal request lacks its actual authored contract");
        const auto result=windows::run_cpp(request);
        const auto captured=windows::capture_cpp_execution(request,result);
        check(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
                (!block.local_int_check||captured.local_int_check_correct)&&
                (!block.local_string_check||captured.local_string_check_correct)&&
                (!block.input_check||captured.input_check_correct)&&
                (!block.local_role_check||captured.local_role_check_correct)&&
                windows::evaluate_cpp_output_check(*block.output_check,captured,request.revision).verdict==
                    curriculum::OutputVerdict::correct&&
                windows::record_compiler_puzzle_case(data,lesson,block,request,captured,
                    "QA setup: actual nonfocal compiler and its authored private checks")&&
                windows::compiler_puzzle_completed(data,lesson,block),
            "A real nonfocal prerequisite failed: "+block.stable_id+" "+result.error_utf8+
                result.compilation.stderr_bytes);
    }
}

void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT)continue;
        if(!dispatch_lesson_enter(message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
}
HWND descendant(HWND parent,int id) {
    struct Search { int id;HWND result=nullptr; } search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL {
        auto& current=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=current.id)return TRUE;
        current.result=item;return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
std::string window_text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));
    return utf8(value);
}
struct View {
    WindowsEnvironment environment;
    Lesson lesson;
    std::string activity;
    HWND host=nullptr,view=nullptr;
    std::size_t executions=0;

    View(const Fixture& fixture,const Plan& plan,std::size_t ordinal)
        :environment(resolve_windows_environment({ProfileId::Codex,
            L"local-dependency-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(ordinal),false})),
         lesson(fixture.before),activity(plan.activity) {
        check(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
                !std::filesystem::exists(environment.paths.root),"UI QA would open an existing profile");
        initialize_profile_directories(environment.paths);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden local dependency QA",
            WS_OVERLAPPEDWINDOW,0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the real hidden Win32 host");
    }
    ~View() {
        if(view){(void)flush_lesson_view(view);DestroyWindow(view);}
        if(host)DestroyWindow(host);
    }
    void open() {
        within_budget();
        check(!view,"A second LessonView would duplicate a running worker");
        view=create_lesson_view(host,environment,lesson);
        check(view!=nullptr,"Cannot create the production LessonView");
        MoveWindow(view,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Hidden UI QA exposed a desktop window");
    }
    void close() {
        check(view&&flush_lesson_view(view),"Cannot flush the real lesson before closing");
        DestroyWindow(view);view=nullptr;pump();
    }
    void reopen() {close();open();}
    state::StateData saved() {
        check(view&&flush_lesson_view(view),"Cannot flush the real learner controls");
        state::StateStore store(environment.paths.root);return store.load();
    }
    void write_closed(const state::StateData& data) {
        check(!view,"QA setup cannot rewrite state beneath a live LessonView");
        state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
    }
    HWND maybe_control(std::size_t block,int part) const {
        return descendant(view,1000+static_cast<int>(block)*100+part);
    }
    HWND control(int part) const {
        const auto at=position(lesson,activity);
        check(lesson_page_state(view).index==at.page,"A focal control was requested on another page");
        const auto result=maybe_control(at.block,part);
        check(result!=nullptr,"Missing real focal control part="+std::to_string(part));
        return result;
    }
    std::string text(int part) const {return edit_text(control(part));}
    void replace(int part,std::string_view value) {
        const auto field=control(part);
        check(IsWindowEnabled(field),"The focal source/input field is unexpectedly disabled");
        CHARRANGE all{0,-1};SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
        const auto native=wide(value);
        SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(native.c_str()));pump();
        check(normalized(edit_text(field))==normalized(value),"The real edit changed supplied source/input characters");
    }
    void input(std::string_view value) {
        const auto at=position(lesson,activity);
        if(maybe_control(at.block,15))replace(15,value);
        else check(value.empty(),"An authored input case has no actual input control");
    }
    void select_source() {
        CHARRANGE range{3,11};SendMessageW(control(10),EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));pump();
    }
    std::set<std::filesystem::path> directories() const {
        std::set<std::filesystem::path> result;
        const auto parent=environment.paths.projects/L"cpp-runs";
        if(std::filesystem::exists(parent))for(const auto& entry:std::filesystem::directory_iterator(parent))
            if(entry.is_directory())result.insert(entry.path());
        return result;
    }
    void run(std::string_view source,const curriculum::OutputCase& output) {
        within_budget();
        const auto before=directories();
        const auto button=control(11);
        check(IsWindowEnabled(button)&&window_text(button)=="Compilar, ejecutar y comprobar",
            "The actual combined compiler action is unavailable");
        SendMessageW(button,BM_CLICK,0,0);
        const auto deadline=GetTickCount64()+230000;
        do {
            pump();within_budget();
            if(IsWindowEnabled(button)&&directories()!=before)break;
            MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
        }while(GetTickCount64()<deadline);
        check(IsWindowEnabled(button)&&directories()!=before,"The native UI worker did not finish within its bound");
        const auto status=text(34);
        check(status=="Ejecución terminada (código 0)."||
                status=="Caso ejecutado. Aún falta acreditar el primer caso obligatorio de este editor."||
                status.starts_with("Comprobación obligatoria: "),
            "The public native execution did not complete normally: "+status);
        std::vector<std::filesystem::path> created;
        for(const auto& directory:directories())if(!before.contains(directory))created.push_back(directory);
        check(created.size()==1,"One real button action did not create exactly one public run");
        const auto& directory=created.front();
        const auto compiler=std::filesystem::path(build::educational_vc_root)/L"bin/Hostx64/x64/cl.exe";
        const auto toolchain=bytes(directory/L"toolchain.txt");
        check(toolchain.starts_with("MSVC=")&&toolchain.find("compiler="+utf8(compiler.wstring())+'\n')!=toolchain.npos&&
                std::filesystem::is_regular_file(directory/L"program.exe")&&
                std::filesystem::file_size(directory/L"program.exe")>0,
            "The button did not use the configured native compiler and a real executable");
        const auto stdout_bytes=bytes(directory/L"execution-stdout.bin");
        check(bytes(directory/L"main.cpp")==source&&bytes(directory/L"stdin.bin")==output.standard_input&&
                bytes(directory/L"execution-stderr.bin").empty()&&
                normalized(text(14))==normalized(stdout_bytes)&&
                std::ranges::any_of(output.outputs,[&](const auto& expected){return normalized(expected)==normalized(stdout_bytes);}),
            "A private trial changed the public source, stdin, stdout or visible console");
        check(!IsWindowVisible(host),"The real worker exposed the hidden QA host");
        ++executions;
    }
    void navigate_to(std::size_t destination) {
        auto current=lesson_page_state(view).index;
        if(destination<current) {
            check(navigate_lesson_view(view,destination),"Cannot return to the real focal page");pump();
        }else while(current<destination) {
            check(navigate_lesson_view(view,++current),"An earned first case did not release actual page navigation");pump();
        }
        check(lesson_page_state(view).index==destination,"Navigation did not reach its real target page");
    }
    void reset() {
        const auto button=control(12);
        check(IsWindowEnabled(button),"The actual compiler Reset action is unavailable");
        SendMessageW(button,BM_CLICK,0,0);pump();
        check(text(10)==block_at(lesson,activity).initial_code&&text(4).empty()&&text(14).empty(),
            "Reset did not restore this editor's initial source and clear its own verdict/console");
    }
};

std::string receipt_prefix(const Lesson& lesson,const Block& block) {
    return "compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+curriculum::exercise_key(lesson,block)+'/';
}
state::Preferences receipts(const state::StateData& data,std::string_view prefix) {
    state::Preferences result;
    if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
        for(const auto& [key,value]:domain->second)if(key.starts_with(prefix))result.emplace(key,value);
    return result;
}
void retained_pairs(const state::StateData& data,const state::Preferences& earlier) {
    for(const auto& [key,value]:earlier)
        check(data.preferences.at("native_windows").at(key)==value,
            "An operation rewrote a historical receipt: "+key);
}
state::Preferences achievement_seals(const state::StateData& data,std::string_view prefix) {
    auto result=receipts(data,prefix);
    std::erase_if(result,[](const auto& entry) {
        const auto& key=entry.first;
        return key.find("/review/")!=key.npos||
            !(key.ends_with("/completed")||key.ends_with("/feedback")||key.ends_with("/witness")||
              key.ends_with("/local-int/v1")||key.ends_with("/local-string/v1"));
    });
    return result;
}
void same_draft(const state::Draft& left,const state::Draft& right) {
    check(left.has_saved&&right.has_saved&&left.source==right.source&&left.input==right.input&&
            left.selection_start==right.selection_start&&left.selection_end==right.selection_end,
        "Closing/reopening changed draft source, stdin or selection");
}
void check_feedback(View& view,bool correct) {
    const auto feedback=view.text(4);
    check(feedback.starts_with(correct?"✓ Correcto":"✗ Incorrecto"),"The visible verdict differs from the actual private outcome");
    const auto focal=position(view.lesson,view.activity);
    bool found=false;
    for(const auto& region:lesson_reading_regions(view.view))if(region.target==view.control(4)) {
        found=true;
        check(region.block_index==focal.block&&region.feedback&&region.correct==correct&&
                region.source_text==feedback,"Visible feedback and its real reading metadata diverged");
    }
    check(found,"The actual feedback lacks its reading region");
    if(correct)return;
    check(feedback.size()>std::string_view("✗ Incorrecto").size()&&
            feedback.find("CMake")==feedback.npos&&feedback.find("todos los archivos originales")==feedback.npos,
        "A scalar/input rejection omitted its guide or claimed an unrelated CMake requirement");
    const auto& block=block_at(view.lesson,view.activity);
    const auto absent=[&](std::string_view value) {
        if(value.empty())return;
        check(feedback.find(value)==feedback.npos&&view.text(34).find(value)==std::string::npos,
            "The public guide disclosed a private oracle or trial input");
    };
    if(block.local_int_check)for(const auto& probe:block.local_int_check->probes)
        for(const auto& output:probe.expected_stdout)absent(output);
    if(block.local_string_check)for(const auto& probe:block.local_string_check->probes)
        for(const auto& output:probe.expected_stdout)absent(output);
    if(block.input_check)for(const auto& probe:block.input_check->probes) {
        absent(probe.standard_input);for(const auto& output:probe.outputs)absent(output);
    }
    if(block.local_role_check)for(const auto& role:block.local_role_check->roles)
        for(const auto& probe:role.probes)for(const auto& output:probe.expected_stdout)absent(output);
    absent("lgaed_role_probe_");absent("lgaed_int_probe_");
}
void check_reader_goal(View& view,bool completed) {
    const auto focal=position(view.lesson,view.activity);
    bool found=false;
    for(const auto& region:lesson_reading_regions(view.view))
        if(region.block_index==focal.block&&region.required_compiler&&!region.feedback) {
            found=true;check(region.solved==completed,"The real reader metadata granted or lost the private objective");
        }
    check(found,"The actual compiler lacks its required reader metadata");
}
void reference_visible(View& view,const Plan& plan) {
    const auto at=position(view.lesson,plan.reference);
    check(lesson_page_state(view.view).index==at.page,"Reference visibility was queried on another page");
    const auto& reference=block_at(view.lesson,plan.reference);
    const auto field=view.maybe_control(at.block,1);
    check(field&&(GetWindowLongPtrW(field,GWL_STYLE)&WS_VISIBLE)&&edit_text(field)==reference.code,
        "The earned reference is not rendered from its authentic code");
    // The reader names a code example; its source stays in the visual control
    // and is deliberately excluded from the narration script.
    const auto caption=view.maybe_control(at.block,16);
    const auto regions=lesson_reading_regions(view.view);
    check(caption&&(GetWindowLongPtrW(caption,GWL_STYLE)&WS_VISIBLE)&&
            std::ranges::any_of(regions,[&](const auto& region) {
                return region.block_index==at.block&&region.target==caption&&region.passive_label&&
                    region.source_text=="Ejemplo de código C más más.";
            })&&std::ranges::none_of(regions,[&](const auto& region){return region.target==field;}),
        "The earned reference lost its passive reading caption or exposed code as narration");
}
void blocked(View& view,const Plan& plan,std::size_t bookmark) {
    const auto target=position(view.lesson,plan.activity);
    check(lesson_page_state(view.view).index==target.page&&!lesson_page_state(view.view).can_next,
        "A legacy or optional-only receipt released the new required objective");
    check(!navigate_lesson_view(view.view,bookmark)&&lesson_page_state(view.view).index==target.page,
        "Navigation bypassed an unverified local/input dependency");
    const auto options=lesson_options_snapshot(view.view);
    check(std::ranges::find(options.available_pages,bookmark)==options.available_pages.end(),
        "The actual Pages gate admitted the saved later bookmark");
    const auto reference=position(view.lesson,plan.reference);
    if(reference.page==target.page) {
        check(!view.maybe_control(reference.block,1),"A same-page reference was exposed before its private objective");
        const auto regions=lesson_reading_regions(view.view);
        check(std::ranges::none_of(regions,[&](const auto& region){return region.block_index==reference.block;}),
            "A blocked same-page reference leaked through reading metadata");
    }
    check_reader_goal(view,false);
}

void run_plan(const Fixture& fixture,const Plan& plan,std::size_t ordinal) {
    phase=fixture.id+"/"+plan.activity+" preparing authentic BEFORE";
    const auto target=position(fixture.before,plan.activity);
    const auto reference=position(fixture.before,plan.reference);
    const auto bookmark=std::max(reference.page,target.page+1);
    check(bookmark<fixture.before.pages.size(),"The owner has no real later page for bookmark adoption");
    const auto& old=block_at(fixture.before,plan.activity);
    const auto& current=block_at(fixture.after,plan.activity);
    const auto& first=current.output_check->cases.front();
    const auto& source=solution_for(fixture,plan.activity);
    const auto key=curriculum::draft_key("programming",fixture.before,old);
    const auto prefix=receipt_prefix(fixture.before,old);
    View view(fixture,plan,ordinal);
    state::StateData initial;state::prepare_lesson_state(initial,fixture.before);
    prepare_quizzes(initial,fixture.before,bookmark);
    prepare_compilers(initial,fixture,fixture.before,plan,bookmark,view.environment);
    initial.preferences["native_windows"]["ui_language"]=std::string("es");
    initial.preferences["local_dependency_view_unrelated"]["keep"]=std::string(" \t independent preference\n");
    std::vector<std::pair<std::string,state::Draft>> other_drafts;
    for(const auto& page:fixture.before.pages)for(const auto& block:page.blocks)
        if(block.type==BlockType::cpp_compiler&&block.stable_id!=plan.activity) {
            state::Draft draft;draft.source=block.initial_code+"\n// Saved independent learner draft.\n";
            draft.input="saved independent input\n";draft.selection_start=2;draft.selection_end=8;
            const auto other_key=curriculum::draft_key("programming",fixture.before,block);
            state::put_draft(initial,other_key,draft);
            other_drafts.emplace_back(other_key,state::get_draft(initial,other_key,""));
        }
    for(std::size_t page=0;page<target.page;++page)(void)state::mark_page_reached(initial,fixture.before,page);
    state::save_current_page(initial,fixture.before,target.page);
    view.write_closed(initial);view.open();
    check(lesson_page_state(view.view).index==target.page,"BEFORE did not open its actual focal page");
    view.replace(10,plan.fixed_source);view.input(first.standard_input);
    phase=fixture.id+"/"+plan.activity+" earning actual historical UI receipt";
    view.run(plan.fixed_source,first);check_feedback(view,true);
    check(windows::compiler_puzzle_completed(view.saved(),fixture.before,old)&&
            lesson_page_state(view.view).can_next,"A real historical first stdout failed to earn its old contract");
    view.select_source();
    if(reference.page==target.page)reference_visible(view,plan);
    view.navigate_to(bookmark);
    if(reference.page==bookmark)reference_visible(view,plan);
    const auto legacy=view.saved();
    const auto old_receipts=receipts(legacy,prefix);
    const auto old_draft=state::get_draft(legacy,key,old.initial_code,old.standard_input.value_or(""));
    check(!old_receipts.empty()&&windows::compiler_puzzle_case_count(legacy,fixture.before,old)==1&&
            old_draft.source==plan.fixed_source&&old_draft.input==first.standard_input,
        "The actual BEFORE UI did not persist its fixed source, first case and later bookmark");
    const bool has_reference=std::ranges::any_of(fixture.before.reference_after,
        [&](const auto& relation){return relation.activity==plan.activity;})||
        std::ranges::any_of(fixture.before.activity_after,[&](const auto& relation){
            return std::ranges::find(relation.prerequisites,plan.activity)!=relation.prerequisites.end();
        });
    const auto reference_key="reference-proof/v1/"+fixture.before.locale+'/'+key;
    const auto old_reference_proof=state::string_value(legacy,"native_windows",reference_key);
    if(has_reference)check(!old_reference_proof.empty(),
        "The BEFORE UI failed to create the real legacy reference proof being tested");
    view.close();

    phase=fixture.id+"/"+plan.activity+" reopening AFTER from the historical bookmark";
    auto prepared=legacy;state::prepare_lesson_state(prepared,fixture.after);
    prepare_quizzes(prepared,fixture.after,bookmark);
    prepare_compilers(prepared,fixture,fixture.after,plan,bookmark,view.environment);
    retained_pairs(prepared,old_receipts);
    state::Preferences other_receipts;
    const auto all_prefix="compiler-puzzle/v1/"+fixture.after.locale+'/'+fixture.after.id+'/';
    for(const auto& [entry,value]:receipts(prepared,all_prefix))
        if(!entry.starts_with(prefix))other_receipts.emplace(entry,value);
    view.write_closed(prepared);view.lesson=fixture.after;view.open();
    const auto preserved=[&](const state::StateData& saved) {
        retained_pairs(saved,old_receipts);retained_pairs(saved,other_receipts);
        check(state::string_value(saved,"native_windows",reference_key)==old_reference_proof,
            "An operation rewrote or removed the historical reference proof");
        check(saved.preferences.at("local_dependency_view_unrelated")==
                initial.preferences.at("local_dependency_view_unrelated"),"An unrelated preference changed");
        check(windows::compiler_puzzle_completed(saved,fixture.before,old)&&
                windows::compiler_puzzle_case_count(saved,fixture.before,old)==1,
            "The genuine historical stdout receipt disappeared");
        for(const auto& [draft_key,expected]:other_drafts)
            same_draft(state::get_draft(saved,draft_key,""),expected);
    };
    auto saved=view.saved();preserved(saved);
    same_draft(state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")),old_draft);
    check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==0&&
            !view.text(4).starts_with("✓ Correcto"),"Old stdout was promoted to the new private objective");
    blocked(view,plan,bookmark);

    phase=fixture.id+"/"+plan.activity+" rejecting fixed stdout through the actual worker";
    const auto fixed_request=request_for(view.environment,fixture.after,current,plan.fixed_source,first.standard_input);
    check(windows::compiler_runtime_request_allowed(current,fixed_request),
        "The fixed-source negative lacks a valid plan and would test only a request-shape guard");
    view.run(plan.fixed_source,first);check_feedback(view,false);
    const auto rejected_feedback=view.text(4);
    view.select_source();const auto rejected=view.saved();preserved(rejected);
    const auto rejected_draft=state::get_draft(rejected,key,current.initial_code,current.standard_input.value_or(""));
    check(!windows::compiler_puzzle_completed(rejected,fixture.after,current)&&
            windows::compiler_puzzle_case_count(rejected,fixture.after,current)==0,
        "A publicly correct fixed result acquired private credit");
    view.reopen();saved=view.saved();preserved(saved);
    same_draft(state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")),rejected_draft);
    check(view.text(4)==rejected_feedback,"The reopened private rejection lost its actual feedback");
    check_feedback(view,false);blocked(view,plan,bookmark);
    if(plan.no_plan_source) {
        phase=fixture.id+"/"+plan.activity+" showing a no-plan guide without claiming private execution";
        const auto no_plan_request=request_for(view.environment,fixture.after,current,
            *plan.no_plan_source,first.standard_input);
        check(!windows::compiler_runtime_request_allowed(current,no_plan_request),
            "The no-plan fixture unexpectedly formed a valid private local binding");
        view.replace(10,*plan.no_plan_source);view.input(first.standard_input);
        view.run(*plan.no_plan_source,first);check_feedback(view,false);
        saved=view.saved();preserved(saved);
        check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==0,
            "A source without a local plan earned private credit");
        // No private request fingerprint exists here. This checks visible
        // guidance only; persistent private rejection was checked above.
        blocked(view,plan,bookmark);
    }

    const bool optional=current.output_check->cases.size()>1;
    if(optional) {
        phase=fixture.id+"/"+plan.activity+" checking an optional input before the first case";
        const auto& trial=current.output_check->cases[1];
        view.replace(10,source);view.input(trial.standard_input);view.run(source,trial);
        saved=view.saved();preserved(saved);
        check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==1&&
                !view.text(4).starts_with("✓ Correcto"),"An optional input or its private trials completed the first case");
        blocked(view,plan,bookmark);view.reopen();saved=view.saved();preserved(saved);
        check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==1,
            "Reopening an optional-only real result fabricated completion or lost the partial case");
        blocked(view,plan,bookmark);
        // Remove the optional observation through the actual Reset button so
        // the next first-case success proves that no optional run is required.
        view.reset();saved=view.saved();preserved(saved);
        check(windows::compiler_puzzle_case_count(saved,fixture.after,current)==0,
            "Reset retained the optional observation before the first-case-only proof");
    }
    phase=fixture.id+"/"+plan.activity+" earning the reviewed goal with exactly one first case";
    view.replace(10,source);view.input(first.standard_input);view.run(source,first);
    saved=view.saved();preserved(saved);check_feedback(view,true);check_reader_goal(view,true);
    check(windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==1&&
            lesson_page_state(view.view).can_next&&
            view.text(34).find("Comprobación obligatoria: 1 de 1.")!=std::string::npos,
        "One real first case did not release the stronger goal or private trials became public cases");
    if(optional)check(view.text(34).find("Pruebas opcionales comprobadas: 0 de "+
        std::to_string(current.output_check->cases.size()-1)+'.')!=std::string::npos,
        "The first-case status treated optional input trials as requirements");
    if(reference.page==target.page)reference_visible(view,plan);
    view.navigate_to(bookmark);if(reference.page==bookmark)reference_visible(view,plan);
    view.navigate_to(target.page);view.select_source();
    const auto earned=view.saved();preserved(earned);
    const auto earned_draft=state::get_draft(earned,key,current.initial_code,current.standard_input.value_or(""));
    const auto earned_seals=achievement_seals(earned,prefix);
    check(!earned_seals.empty(),"The earned goal has no durable completion evidence");
    view.reopen();saved=view.saved();preserved(saved);
    same_draft(state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")),earned_draft);
    check(windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==1&&
            achievement_seals(saved,prefix)==earned_seals&&lesson_page_state(view.view).can_next,
        "Closing/reopening lost the new first-case achievement or its independently stored historical receipt");
    check_feedback(view,true);
    if(optional) {
        phase=fixture.id+"/"+plan.activity+" retaining first-case credit after an optional run";
        const auto& trial=current.output_check->cases[1];
        view.input(trial.standard_input);view.run(source,trial);saved=view.saved();preserved(saved);
        check(windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==2&&
                achievement_seals(saved,prefix)==earned_seals&&lesson_page_state(view.view).can_next,
            "An optional run replaced the first achievement or required its private trials as manual cases");
    }
    phase=fixture.id+"/"+plan.activity+" resetting only the new objective through its real button";
    const auto input_before_reset=state::get_draft(view.saved(),key,current.initial_code,current.standard_input.value_or("" )).input;
    view.reset();saved=view.saved();preserved(saved);
    check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==0&&
            state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")).input==input_before_reset,
        "Reset retained new credit or changed the independently saved stdin");
    blocked(view,plan,bookmark);view.reopen();saved=view.saved();preserved(saved);
    check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==0&&
            view.text(10)==current.initial_code&&view.text(4).empty(),
        "Reopening after Reset resurrected old credit or changed the reset source");
    blocked(view,plan,bookmark);
    std::cout<<"PASS real LessonView "<<fixture.id<<'/'<<plan.activity<<": "<<view.executions
        <<" actual button runs; historical fixed stdout and later-page reference proof; "
        "root adoption blocked; private rejection retained; one first case opens reference; "
        <<(optional?"optional-before-first blocked/reset and optional-after-first retained; ":"")
        <<"separate receipts, draft/stdin/selection, actual reopen and scoped Reset.\n";
}
} // namespace

int main(int argc,char** argv) {
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    started=GetTickCount64();
    if(argc!=2&&argc!=3) {
        std::cerr<<"Usage: local_dependency_view_tests <manifest.json> [lesson-id]\n";return 2;
    }
    const auto apartment=OleInitialize(nullptr);
    if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto rich=LoadLibraryW(L"Msftedit.dll");
    int result=0;
    try {
        check(rich!=nullptr,"Cannot load actual RichEdit controls");reader_theme::prepare_fonts();
        const auto manifest=std::filesystem::path(argv[1]);
        const auto manifest_bytes=state::read_bytes(manifest);
        const auto fixtures=load_manifest(manifest);
        const auto filter=argc==3?std::string_view(argv[2]):std::string_view{};
        std::size_t completed=0,ordinal=0;
        for(const auto& fixture:fixtures) {
            if(!filter.empty()&&fixture.id!=filter)continue;
            std::cout<<"QA setup "<<fixture.id<<": authored nonfocal quiz answers evaluated by the core; "
                "nonfocal compilers observed by real native runs. These are not claimed as manual learner interactions.\n";
            for(const auto& plan:fixture.plans){run_plan(fixture,plan,++ordinal);++completed;}
        }
        check(completed>0&&(filter.empty()?completed==16:true),"The requested UI group is missing or incomplete");
        for(const auto& fixture:fixtures)
            check(state::sha256(state::read_bytes(fixture.before_path))==fixture.before_sha256&&
                    state::sha256(state::read_bytes(fixture.after_path))==fixture.after_sha256,
                "UI QA changed an authenticated curricular fixture");
        check(state::read_bytes(manifest)==manifest_bytes,"UI QA changed its manifest");
        std::cout<<"PASS local dependency View coverage: "<<completed
            <<" original owners; real Win32 controls and native worker, unchanged publications. "
            "No desktop visual or narration-playback acceptance is claimed.\n";
    }catch(const std::exception& error) {
        std::cerr<<"FAIL local dependency View: "<<error.what()<<'\n';result=1;
    }catch(const winrt::hresult_error& error) {
        std::cerr<<"FAIL local dependency View: "<<phase<<": "<<winrt::to_string(error.message())<<'\n';result=1;
    }
    if(rich)FreeLibrary(rich);OleUninitialize();return result;
}
