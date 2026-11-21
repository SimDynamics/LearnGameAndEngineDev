// Real hosted Win32 C++ run, original published U2L7 editors, in-memory
// authoring and isolated QA StateStore. Never edits curricular assets or profiles.
#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

void require(bool ok,std::string_view reason) {
    if(!ok)throw std::runtime_error(std::string(reason));
}
const Block& owner(const Lesson& lesson,std::string_view identity) {
    const auto at=stable_block_position(lesson,identity);
    require(at.has_value(),"Original published U2L7 owner missing");
    return lesson.pages.at(at->page).blocks.at(at->block);
}
const std::string& source_of(const Lesson& lesson,std::string_view identity) {
    return owner(lesson,identity).code;
}
void replace_once(std::string& source,std::string_view before,std::string_view after) {
    const auto where=source.find(before);
    require(where!=source.npos&&source.find(before,where+before.size())==source.npos,
        "Original starter lacks its one authentic repair location");
    source.replace(where,before.size(),after);
}
CppRunRequest request_for(const Lesson& lesson,std::string_view identity,
        std::string source,const std::filesystem::path& projects,std::uint64_t number) {
    const auto& block=owner(lesson,identity);
    CppRunRequest request;
    request.request_id=number;
    request.revision=number+100;
    request.source_utf8=std::move(source);
    request.stdin_utf8=block.standard_input.value_or("");
    request.project_key="isolated-u2l7-"+std::string(identity);
    request.projects_directory=projects;
    request.compile_timeout=std::chrono::seconds(60);
    request.execution_timeout=std::chrono::seconds(8);
    request.local_string_check=block.local_string_check;
    return request;
}
std::size_t count_receipts(const state::StateData& data) {
    std::size_t count=0;
    if(const auto found=data.preferences.find("native_windows");found!=data.preferences.end())
        for(const auto& [key,value]:found->second)
            if(key.ends_with("/local-string/v1")) {
                require(std::holds_alternative<std::string>(value),
                    "A local-string private receipt has invalid stored type");
                ++count;
            }
    return count;
}
void rejects_document(std::string_view raw) {
    for(bool metadata:{false,true}) {
        bool rejected=false;
        try {
            if(metadata)(void)parse_lesson_progress_metadata(raw,"es");
            else (void)parse_lesson_json(raw,"es");
        }catch(const std::exception&) {rejected=true;}
        require(rejected,"Malformed U2L7 root was accepted by a native loader");
    }
}
void run() {
    const auto file=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_cadenas_y_texto.json";
    const auto original=load_lesson(file);
    constexpr std::string_view first="block_0006_0002",second="block_0008_0007";
    require(!owner(original,first).local_string_check&&
            !owner(original,second).local_string_check&&
            owner(original,first).output_check&&
            owner(original,second).output_check&&
            owner(original,first).output_check->cases.front().outputs==
                std::vector<std::string>{"Aula-04","Aula-04\n"}&&
            owner(original,second).output_check->cases.front().outputs==
                std::vector<std::string>{"Aula-07","Aula-07\n"},
        "Published U2L7 output contracts differ from their known original versions");

    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    constexpr auto authored=R"json([
      {"activity":"block_0006_0002","probes":[
        {"variable":"edificio","replacement":"Sala Norte","moment":"afterInitialization",
         "outputs":["Sala Norte-04","Sala Norte-04\n"]},
        {"variable":"codigo","replacement":"003","moment":"afterInitialization",
         "outputs":["Aula-003","Aula-003\n"]},
        {"variable":"etiqueta","replacement":"CONTROL QA","moment":"beforeOutput",
         "outputs":["CONTROL QA","CONTROL QA\n"]}]},
      {"activity":"block_0008_0007","probes":[
        {"variable":"codigo","replacement":"B2","moment":"afterFirstAssignment",
         "outputs":["Aula-B2","Aula-B2\n"]},
        {"variable":"etiqueta","replacement":"CONTROL QA","moment":"beforeOutput",
         "outputs":["CONTROL QA","CONTROL QA\n"]}]}
    ])json";
    root.Insert(L"compilerLocalStringChecks",JsonValue::Parse(authored));
    const auto generated=winrt::to_string(root.Stringify());
    const auto reviewed=parse_lesson_json(generated,"es");
    const auto metadata=parse_lesson_progress_metadata(generated,"es");
    for(const auto id:{first,second}) {
        const auto& before=owner(original,id);
        const auto& current=owner(reviewed,id);
        const auto& projection=owner(metadata,id);
        require(current.local_string_check&&
                current.local_string_check==projection.local_string_check&&
                before.initial_code==current.initial_code&&
                before.raw_json==current.raw_json&&
                before.address.page==current.address.page&&
                before.address.block==current.address.block&&
                before.output_check->cases.front().outputs==
                    current.output_check->cases.front().outputs,
            "Authoring changed an old U2L7 editor, address or goal");
    }
    const auto original_reference=source_of(original,"block_0008_0003");
    std::string repaired=owner(original,second).initial_code;
    replace_once(repaired,
        "    codigo = \"07\";\n    std::cout << etiqueta;",
        "    codigo = \"07\";\n    etiqueta = edificio + \"-\" + codigo;\n    std::cout << etiqueta;");

    const auto options=LaunchOptions{ProfileId::Codex,
        L"local-string-u2l7-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    const auto projects=environment.paths.projects/L"independent U2L7 variable tests";
    auto first_old=request_for(original,first,original_reference,projects,1);
    auto first_new=request_for(reviewed,first,original_reference,projects,2);
    auto second_new=request_for(reviewed,second,repaired,projects,3);
    require(compiler_runtime_request_allowed(owner(original,first),first_old)&&
            compiler_runtime_request_allowed(owner(reviewed,first),first_new)&&
            compiler_runtime_request_allowed(owner(reviewed,second),second_new)&&
            !compiler_runtime_request_allowed(owner(reviewed,first),first_old),
        "Original and reviewed U2L7 requests did not retain distinct binding");
    require(!cpp_local_string_request_fingerprint(first_new).empty()&&
            !cpp_local_string_request_fingerprint(second_new).empty()&&
            cpp_local_string_request_fingerprint(first_new)!=
                cpp_local_string_request_fingerprint(second_new),
        "The distinct editor/source/check bindings were lost");

    auto missing=first_new;
    missing.source_utf8="#include <iostream>\nint main(){std::cout<<\"Aula-04\";}";
    require(cpp_local_string_request_fingerprint(missing).empty()&&
            !compiler_runtime_request_allowed(owner(reviewed,first),missing),
        "A literal-only program was admitted as a variable-dependent one");

    // Historical stdout remains real saved state but cannot become new credit.
    state::StateData saved;
    state::prepare_lesson_state(saved,original);
    FreshExecution visible;
    visible.source_revision=first_old.revision;
    visible.compilation_succeeded=true;
    visible.execution_succeeded=true;
    visible.execution_completed=true;
    visible.exit_code=0;
    visible.standard_output="Aula-04";
    require(record_compiler_puzzle_case(saved,original,owner(original,first),
        first_old,visible,"historical synthetic stdout receipt")&&
        compiler_puzzle_completed(saved,original,owner(original,first)),
        "Original stdout-only receipt was not retained");
    const auto legacy=saved;
    require(!compiler_puzzle_completed(saved,reviewed,owner(reviewed,first))&&
            compiler_puzzle_case_count(saved,reviewed,owner(reviewed,first))==1&&
            count_receipts(saved)==0&&saved==legacy,
        "An old visible stdout was promoted to the new string dependence");

    const auto run_first=run_cpp(first_new);
    require(run_first.error_utf8.empty()&&run_first.compilation.succeeded()&&
            run_first.execution.succeeded(),
        "Real Windows C++ failed U2L7 first editor: "+
        run_first.error_utf8+run_first.compilation.stderr_bytes);
    const auto verified_first=capture_cpp_execution(first_new,run_first);
    require(verified_first.local_string_check_correct&&
            verified_first.local_string_request_fingerprint==
                cpp_local_string_request_fingerprint(first_new)&&
            verified_first.standard_output=="Aula-04"&&
            run_first.local_string_evidence.size()==3,
        "Real MSVC private first-editor string observations did not match");
    auto tampered=run_first;
    tampered.local_string_evidence[0].execution.stdout_bytes="Aula-04";
    require(!capture_cpp_execution(first_new,tampered).local_string_check_correct,
        "One constant result passed a changed variable output");
    tampered=run_first;
    tampered.local_string_evidence[1].instrumented_source_sha256="other-source";
    require(!capture_cpp_execution(first_new,tampered).local_string_check_correct,
        "An unrelated private source was admitted as the first U2L7 editor");
    auto wrong_fresh=verified_first;
    wrong_fresh.local_string_check_correct=false;
    require(!record_compiler_puzzle_case(saved,reviewed,owner(reviewed,first),
        first_new,wrong_fresh,"unverified")&&saved==legacy,
        "An independently unproven observation replaced historical progress");
    require(record_compiler_puzzle_case(saved,reviewed,owner(reviewed,first),
        first_new,verified_first,"real private MSVC evidence")&&
        compiler_puzzle_completed(saved,reviewed,owner(reviewed,first))&&
        count_receipts(saved)==1,
        "The first real U2L7 editor did not seal its private text witness");
    for(const auto& [key,value]:legacy.preferences.at("native_windows"))
        require(saved.preferences.at("native_windows").at(key)==value,
            "Appending text evidence rewrote the old output receipt");

    // This programme prints the public answer from a constant *inside* the
    // declared etiqueta variable. Two independently changed inputs expose it.
    auto shortcut=first_new;
    shortcut.request_id=9;shortcut.revision=109;
    shortcut.source_utf8=R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta{"Aula-04"};
    std::cout << etiqueta;
}
)CPP";
    require(compiler_runtime_request_allowed(owner(reviewed,first),shortcut),
        "The broken-but-valid candidate did not reach the independent runtime grader");
    const auto broken=run_cpp(shortcut);
    require(broken.error_utf8.empty()&&broken.compilation.succeeded()&&
            broken.execution.succeeded()&&
            broken.execution.stdout_bytes=="Aula-04",
        "The public output of the fixed-text counterexample was not authentic");
    const auto rejected=capture_cpp_execution(shortcut,broken);
    require(!rejected.local_string_check_correct&&
            !record_compiler_puzzle_case(saved,reviewed,owner(reviewed,first),
                shortcut,rejected,"fixed literal rejected"),
        "Correct visible output earned a string dependency without varied input");

    const auto run_second=run_cpp(second_new);
    require(run_second.error_utf8.empty()&&run_second.compilation.succeeded()&&
            run_second.execution.succeeded(),
        "Real Windows C++ failed the repaired second U2L7 editor: "+
        run_second.error_utf8+run_second.compilation.stderr_bytes);
    const auto verified_second=capture_cpp_execution(second_new,run_second);
    require(verified_second.local_string_check_correct&&
            verified_second.standard_output=="Aula-07"&&
            run_second.local_string_evidence.size()==2,
        "Changing codigo to B2 did not rebuild the stored etiqueta");
    require(!compiler_puzzle_completed(saved,reviewed,owner(reviewed,second))&&
            record_compiler_puzzle_case(saved,reviewed,owner(reviewed,second),
                second_new,verified_second,"real repaired second U2L7 editor")&&
            compiler_puzzle_completed(saved,reviewed,owner(reviewed,second))&&
            count_receipts(saved)==2,
        "Second independent string contract reused a receipt from the first");

    state::StateStore store(environment.paths.root);
    (void)store.load();
    store.save(saved);
    const auto reopened=store.load();
    require(compiler_puzzle_completed(reopened,reviewed,owner(reviewed,first))&&
            compiler_puzzle_completed(reopened,reviewed,owner(reviewed,second))&&
            count_receipts(reopened)==2,
        "The isolated Windows StateStore did not persist both distinct U2L7 proofs");
    auto corrupted=reopened;
    for(auto& [key,value]:corrupted.preferences["native_windows"])
        if(key.ends_with("/local-string/v1")){value=std::string("tampered");break;}
    bool rejected_corrupt=false;
    try{validate_compiler_puzzle_state(corrupted,reviewed,owner(reviewed,first));}
    catch(const std::exception&){rejected_corrupt=true;}
    require(rejected_corrupt,
        "Tampering with a saved private string receipt was silently accepted");
    require(store.load()==reopened,
        "Rejected corruption changed the stored original QA profile");
    auto invalid=JsonObject::Parse(winrt::to_hstring(generated));
    auto probe=invalid.GetNamedArray(L"compilerLocalStringChecks").GetObjectAt(0)
        .GetNamedArray(L"probes").GetObjectAt(0);
    JsonArray visible_outputs;
    visible_outputs.Append(JsonValue::CreateStringValue(L"Aula-04"));
    probe.Insert(L"outputs",visible_outputs);
    rejects_document(winrt::to_string(invalid.Stringify()));
    std::cout<<"PASS Win32 real U2L7: two original owners, five private MSVC "
        "compilations and executions, wrong source/constant rejected, "
        "historical stdout preserved, two sealed StateStore receipts.\n";
}
} // namespace

int main() try {
    winrt::init_apartment();
    run();
    return 0;
}catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL native U2L7 local string: "<<winrt::to_string(error.message())<<'\n';
    return 1;
}catch(const std::exception& error) {
    std::cerr<<"FAIL native U2L7 local string: "<<error.what()<<'\n';
    return 1;
}
