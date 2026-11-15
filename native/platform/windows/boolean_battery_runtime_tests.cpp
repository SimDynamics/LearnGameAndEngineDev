#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace std::chrono_literals;
using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing authentic U1 fixture or runner artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
const Block& owner(const Lesson& lesson,std::string_view id) {
    const Block* found=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.stable_id==id) {
            require(!found,"Duplicate stable boolean-battery owner");
            found=&block;
        }
    require(found!=nullptr,"Missing stable boolean-battery owner");
    return *found;
}
constexpr std::string_view declaration=R"json([
  {"activity":"block_0004_0004","kind":"boolean-pair-v1",
   "function":"debe_reponer","publishedResult":"failure","cases":[
    {"first":4,"second":5,"expected":true},
    {"first":5,"second":5,"expected":true},
    {"first":6,"second":5,"expected":false}]},
  {"activity":"block_0005_0009","kind":"boolean-pair-v1",
   "function":"debe_reponer","publishedResult":"success","cases":[
    {"first":4,"second":5,"expected":true},
    {"first":5,"second":5,"expected":true},
    {"first":6,"second":5,"expected":false},
    {"first":7,"second":8,"expected":true},
    {"first":8,"second":8,"expected":true},
    {"first":9,"second":8,"expected":false}]}
])json";

std::string first_solution(const Block& block) {
    return R"cpp(#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
)cpp"+block.boolean_battery_probe->protected_definition+R"cpp(

int main(int argc,char** argv) {
    if(std::filesystem::exists("main.cpp")||
       std::filesystem::exists("program.obj")||
       std::filesystem::exists("compilation-stdout.bin")||
       std::filesystem::exists("phase-marker.txt"))return 79;
    std::ifstream toolchain_input("toolchain.txt",std::ios::binary);
    if(!toolchain_input)return 78;
    const std::string initial_toolchain{std::istreambuf_iterator<char>(toolchain_input),
        std::istreambuf_iterator<char>()};
    toolchain_input.close();
    std::ofstream("toolchain.txt",std::ios::binary|std::ios::trunc) << "public toolchain change\n";
    std::ofstream("phase-marker.txt") << "public output\n";
    const auto* language=std::getenv("VSLANG");
    std::cout << __FILE__ << '|' << __LINE__ << '|' << argc << '|'
              << argv[0] << '|' << std::filesystem::current_path().string()
              << '|' << (language?language:"") << '|' << initial_toolchain.size() << ':';
    for(const unsigned char byte:initial_toolchain)std::cout << static_cast<unsigned>(byte) << ',';
    std::cout << '\n';
    struct Case {int first,second;bool expected;};
    const Case cases[]{{4,5,true},{5,5,true},{6,5,false}};
    int failures=0;
    for(const auto& item:cases) {
        const bool actual=debe_reponer(item.first,item.second);
        if(actual!=item.expected) {
            ++failures;
            std::cout << "Fallo " << item.first << ',' << item.second
                      << " esperado=" << item.expected << " obtenido=" << actual << '\n';
        }
    }
    return failures ? 257 : 0;
}
)cpp";
}
std::string second_solution(const Block& block) {
    // A different comparison style, reverse order, stderr diagnostics and a
    // silent successful run are all valid. -1 is the complete Windows DWORD.
    return "#include <iostream>\n"+block.boolean_battery_probe->protected_definition+R"cpp(
int main() {
    bool correct=true;
    const auto check=[&](int first,int second,bool expected) {
        const bool actual=debe_reponer(first,second);
        if(expected==actual)return;
        correct=false;
        std::cerr << "Caso " << first << '/' << second << " esperaba "
                  << expected << " y obtuvo " << actual << '\n';
    };
    check(9,8,false);
    check(8,8,true);
    check(7,8,true);
    check(6,5,false);
    check(5,5,true);
    check(4,5,true);
    return correct ? 0 : -1;
}
)cpp";
}
void checked_run(const CppRunRequest& request,const CppRunResult& result,
                 const FreshExecution& execution,const Block& block) {
    require(result.error_utf8.empty(),"Runner error: "+result.error_utf8);
    require(result.compilation.succeeded(),"Candidate compilation failed: "+
        result.compilation.stdout_bytes+result.compilation.stderr_bytes);
    require(result.execution.status==CppPhaseStatus::completed&&result.execution.exit_code,
        "Candidate did not complete an actual Windows execution");
    require(result.boolean_battery_evidence.has_value(),"Missing independent runtime observations");
    const auto& evidence=*result.boolean_battery_evidence;
    const auto plan=boolean_battery_mutation_plan(*request.boolean_battery_probe);
    require(plan&&evidence.runs.size()==plan->size(),"Not every control, mutant and published phase ran");
    for(std::size_t i=0;i<plan->size();++i) {
        const auto& run=evidence.runs[i];
        require(run.compiled&&run.executed&&run.completed_normally,
            "A private phase failed to compile, execute or finish normally");
        require(run.mutation==(*plan)[i].mutation&&run.target==(*plan)[i].target,
            "Private runtime observations were assigned to another phase");
        require(run.calls.size()==request.boolean_battery_probe->cases.size(),
            "A private phase did not execute the complete declared battery");
    }
    require(execution.boolean_battery_probe_correct&&execution.execution_completed&&
            execution.boolean_battery_exit_code==result.execution.exit_code&&
            execution.boolean_battery_request_fingerprint==cpp_boolean_battery_request_fingerprint(request),
            "Authentic runtime proof was rejected: "+execution.boolean_battery_probe_feedback);
    require(evaluate_cpp_output_check(*block.output_check,execution,request.revision).verdict==
            OutputVerdict::correct,"The verified battery did not earn its declared result");
    require(execution.standard_output==result.execution.stdout_bytes&&
            execution.standard_error==result.execution.stderr_bytes,
            "A private trace replaced the user's visible public streams");
    require(read(result.run_directory/L"main.cpp")==request.source_utf8&&
            std::filesystem::is_regular_file(result.artifact)&&
            read(result.run_directory/L"compilation-stdout.bin")==result.compilation.stdout_bytes&&
            read(result.run_directory/L"compilation-stderr.bin")==result.compilation.stderr_bytes&&
            read(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
            read(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "The public source, executable or captured logs were not restored");
    for(const auto& entry:std::filesystem::directory_iterator(result.run_directory)) {
        const auto name=entry.path().filename().string();
        require(name.find("private")==std::string::npos&&name.find("mutant")==std::string::npos,
            "A phase-labelled private artifact escaped into the public workspace");
    }
}
void rejected_capture(const CppRunRequest& request,const CppRunResult& result) {
    require(!capture_cpp_execution(request,result).boolean_battery_probe_correct,
        "Stale, missing or altered evidence acquired a boolean-battery proof");
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const LaunchOptions options{ProfileId::Codex,
            L"boolean-battery-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64()),false};
        const auto environment=resolve_windows_environment(options);
        initialize_profile_directories(environment.paths);
        const auto projects=environment.paths.projects/L"Batería & (fuente)";
        const auto original_text=read(std::filesystem::path(argv[1]));
        const auto original=parse_lesson_json(original_text,"es");
        auto document=JsonObject::Parse(winrt::to_hstring(original_text));
        document.Insert(L"compilerOpenBatteryChecks",JsonArray::Parse(winrt::to_hstring(declaration)));
        const auto reviewed=winrt::to_string(document.Stringify());
        const auto lesson=parse_lesson_json(reviewed,"es");
        const auto progress=parse_lesson_progress_metadata(reviewed,"es");
        require(original.pages.size()==lesson.pages.size()&&lesson.pages.size()==progress.pages.size()&&
                original.state_layout.raw_json==lesson.state_layout.raw_json&&
                original.state_layout.raw_json==progress.state_layout.raw_json&&
                original.state_layout.lineage==lesson.state_layout.lineage&&
                original.state_layout.lineage==progress.state_layout.lineage&&
                original.state_layout.signatures==lesson.state_layout.signatures&&
                original.state_layout.signatures==progress.state_layout.signatures,
                "Root declarations changed an authentic state map or family");
        for(const auto id:{"block_0004_0004","block_0005_0009"}) {
            const auto& before=owner(original,id);
            const auto& current=owner(lesson,id);
            const auto& restored=owner(progress,id);
            require(current.output_check&&restored.output_check&&
                    current.boolean_battery_probe&&restored.boolean_battery_probe&&
                    current.boolean_battery_probe==restored.boolean_battery_probe&&
                    current.initial_code==before.initial_code&&restored.initial_code==before.initial_code&&
                    current.stable_id==before.stable_id&&restored.stable_id==before.stable_id&&
                    current.address.page==before.address.page&&current.address.block==before.address.block&&
                    restored.address.page==before.address.page&&restored.address.block==before.address.block&&
                    current.output_check->cases.size()==1&&restored.output_check->cases.size()==1,
                    "Boolean metadata changed a source, owner, address or first-case contract");
        }
        // The progress path must reject this before indexing an absent map.
        // No other editorial metadata can accidentally hide that failure.
        constexpr std::string_view unmapped=R"json({"id":"boolean_unmapped","title":"QA","locale":"es",
          "pages":[{"blocks":[{"type":"cpp_compiler","language":"cpp",
          "initialCode":"bool debe_reponer(int a,int b){return a<b;} int main(){return 0;}"}]}],
          "compilerOpenBatteryChecks":[{"activity":"missing","kind":"boolean-pair-v1",
          "function":"debe_reponer","publishedResult":"failure",
          "cases":[{"first":4,"second":5,"expected":true}]}]})json";
        for(const bool metadata:{false,true}) {
            bool rejected=false;
            try {
                if(metadata)(void)parse_lesson_progress_metadata(unmapped,"es");
                else (void)parse_lesson_json(unmapped,"es");
            }catch(const std::exception&){rejected=true;}
            require(rejected,"A boolean declaration without stateLayout was not rejected cleanly");
        }
        std::cout << "PASS authentic U1 full/progress loaders and missing-map rejection\n";

        const auto& first=owner(lesson,"block_0004_0004");
        const auto& second=owner(lesson,"block_0005_0009");
        require(first.boolean_battery_probe->cases.size()==3&&
                second.boolean_battery_probe->cases.size()==6,"Wrong authentic battery sizes");
        std::uint64_t sequence=0;
        const auto request=[&](const Block& block,std::string source) {
            CppRunRequest result;
            result.request_id=++sequence;result.revision=100+sequence;
            result.projects_directory=projects;result.source_utf8=std::move(source);
            result.stdin_utf8=block.standard_input.value_or("");
            result.boolean_battery_probe=block.boolean_battery_probe;
            result.compile_timeout=60s;result.execution_timeout=5s;
            return result;
        };
        const auto first_request=request(first,first_solution(first));
        require(compiler_runtime_request_allowed(first,first_request),
            "The authentic owner with an implicit C++ language was refused");
        const auto first_result=run_cpp(first_request);
        const auto first_capture=capture_cpp_execution(first_request,first_result);
        checked_run(first_request,first_result,first_capture,first);
        require(first_result.execution.exit_code==std::uint32_t{257}&&
                !first_capture.execution_succeeded&&
                read(first_result.run_directory/L"phase-marker.txt")=="public output\r\n"&&
                read(first_result.run_directory/L"toolchain.txt")=="public toolchain change\n",
                "Normal DWORD 257 or the public program's created/modified files were lost");
        const auto identity_end=first_result.execution.stdout_bytes.find('\n');
        require(identity_end!=std::string::npos,"Missing observable phase identity");
        const auto identity=first_result.execution.stdout_bytes.substr(0,identity_end+1);
        for(const auto& run:first_result.boolean_battery_evidence->runs)
            require(run.stdout_bytes.starts_with(identity),
                "__FILE__, __LINE__, argv, cwd, environment or initial toolchain bytes revealed a private phase");
        std::cout << "PASS first U1 battery: normal 257, phase identity and restored public files\n";

        const auto second_request=request(second,second_solution(second));
        require(compiler_runtime_request_allowed(second,second_request),"Reverse-order lambda battery was refused");
        const auto second_result=run_cpp(second_request);
        const auto second_capture=capture_cpp_execution(second_request,second_result);
        checked_run(second_request,second_result,second_capture,second);
        require(second_capture.execution_succeeded&&second_capture.standard_output.empty()&&
                second_capture.standard_error.empty(),"Silent successful control/publication was rejected");
        for(const auto& run:second_result.boolean_battery_evidence->runs)
            if(run.mutation==BooleanBatteryMutation::invert_result)
                require(run.exit_code==std::numeric_limits<std::uint32_t>::max()&&
                        !run.stderr_bytes.empty(),"Normal -1 mutation or stderr diagnostic was narrowed");
        std::cout << "PASS second U1 battery: six independent mutants, silent success and normal -1\n";

        auto changed=first_request;
        changed.source_utf8+="\n// another captured source\n";
        require(changed.request_id==first_request.request_id&&changed.revision==first_request.revision,
            "Stale-source regression accidentally changed request identity");
        rejected_capture(changed,first_result);
        state::StateData untouched;
        untouched.preferences["qa"]["keep"]=std::string("unrelated state");
        auto recorded=untouched;
        require(!record_compiler_puzzle_case(recorded,lesson,first,changed,first_capture,"stale")&&recorded==untouched,
            "Direct consumption rebound an old FreshExecution to new source bytes");
        changed=first_request;changed.boolean_battery_probe->cases[0].expected=false;
        auto another_goal=first;another_goal.boolean_battery_probe=changed.boolean_battery_probe;
        rejected_capture(changed,first_result);
        require(!record_compiler_puzzle_case(recorded,lesson,another_goal,changed,first_capture,"stale")&&recorded==untouched,
            "Direct consumption rebound an old FreshExecution to another battery contract");
        changed=first_request;changed.stdin_utf8="unexpected";rejected_capture(changed,first_result);
        changed=first_request;++changed.request_id;rejected_capture(changed,first_result);
        changed=first_request;++changed.revision;rejected_capture(changed,first_result);
        changed=first_request;changed.backend=CppRunRequest::Backend::wasi;rejected_capture(changed,first_result);
        changed=first_request;changed.rejection_probe=RejectionOperation::boxes;rejected_capture(changed,first_result);
        require(!compiler_runtime_request_allowed(first,changed),"An unrelated runtime probe changed the battery context");
        changed=first_request;changed.action=CppRunRequest::Action::execute;rejected_capture(changed,first_result);
        changed=first_request;changed.boolean_battery_probe.reset();
        require(!compiler_runtime_request_allowed(first,changed),"Missing objective was accepted");
        changed=first_request;
        const auto comparison=changed.source_utf8.find("disponibles < minimo");
        require(comparison!=std::string::npos,"The protected authentic defect fixture changed");
        changed.source_utf8.replace(comparison,std::string_view("disponibles < minimo").size(),"disponibles <= minimo");
        require(!compiler_runtime_request_allowed(first,changed),"A repaired protected function bypassed the test objective");
        auto altered=first_result;altered.boolean_battery_evidence.reset();rejected_capture(first_request,altered);
        altered=first_result;altered.execution.stdout_bytes+="forged";rejected_capture(first_request,altered);
        altered=first_result;altered.execution.exit_code=1;rejected_capture(first_request,altered);
        altered=first_result;altered.boolean_battery_evidence->runs[1].calls.pop_back();rejected_capture(first_request,altered);
        altered=first_result;altered.boolean_battery_evidence->runs.back().completed_normally=false;rejected_capture(first_request,altered);
        altered=first_result;altered.boolean_battery_evidence->original_program_fingerprint="stale";rejected_capture(first_request,altered);
        auto missing=first_capture;missing.boolean_battery_probe_correct=false;
        require(!record_compiler_puzzle_case(recorded,lesson,first,first_request,missing,"incorrect")&&recorded==untouched,
            "A missing independent proof changed progress");
        missing=first_capture;missing.boolean_battery_request_fingerprint.clear();
        require(!record_compiler_puzzle_case(recorded,lesson,first,first_request,missing,"forged")&&recorded==untouched,
            "A boolean summary without the captured request binding earned credit");
        std::cout << "PASS source, contract, snapshot and direct-recorder binding guards\n";

        require(record_compiler_puzzle_case(recorded,lesson,first,first_request,first_capture,"Primera batería correcta")&&
                record_compiler_puzzle_case(recorded,lesson,second,second_request,second_capture,"Segunda batería correcta"),
                "Real runtime evidence could not be sealed");
        require(state::string_value(recorded,"qa","keep")=="unrelated state","An unrelated preference was overwritten");
        const auto reopened=state::decode(state::encode(recorded));
        for(const auto id:{"block_0004_0004","block_0005_0009"}) {
            const auto& restored=owner(progress,id);
            require(compiler_puzzle_completed(reopened,progress,restored)&&
                    compiler_puzzle_case_count(reopened,progress,restored)==1,
                    "A sealed first-case battery achievement was lost on progress reopening");
            validate_compiler_puzzle_state(reopened,progress,restored);
        }
        const auto first_key="compiler-puzzle/v1/"+lesson.locale+"/"+lesson.id+"/"+
            exercise_key(lesson,first)+"/"+state::sha256(reference_objective_frame(lesson,first))+"/";
        auto tampered=reopened;
        auto& proofs=std::get<std::vector<std::string>>(tampered.preferences["native_windows"][first_key+"cases"]);
        require(proofs.size()==1,"The authentic one-case receipt was not found");
        proofs[0]="unverified battery";
        require(!compiler_puzzle_completed(tampered,progress,owner(progress,"block_0004_0004")),
            "A tampered battery receipt unlocked the restored editor");
        const auto earned=recorded;
        require(!record_compiler_puzzle_case(recorded,lesson,first,first_request,missing,"incorrect")&&recorded==earned,
            "A later unverified attempt erased the earned achievement");
        std::cout << "PASS both sealed receipts, progress reopening and tamper rejection\n";

        // The published starters remain executable learning material. A main
        // with no comparisons and the later three-case starter must both fail
        // this independent objective without changing either earned receipt.
        for(const Block* initial_owner:{&first,&second}) {
            const auto initial_request=request(*initial_owner,initial_owner->initial_code);
            require(compiler_runtime_request_allowed(*initial_owner,initial_request),
                "An authentic unchanged U1 starter was refused before execution");
            const auto initial_result=run_cpp(initial_request);
            require(initial_result.error_utf8.empty()&&initial_result.compilation.succeeded()&&
                    initial_result.execution.status==CppPhaseStatus::completed&&
                    initial_result.execution.exit_code&&initial_result.boolean_battery_evidence,
                    "An unchanged U1 starter did not compile and execute with private evidence: "+
                    initial_result.error_utf8+initial_result.compilation.stdout_bytes+
                    initial_result.compilation.stderr_bytes);
            const auto initial_plan=boolean_battery_mutation_plan(*initial_request.boolean_battery_probe);
            require(initial_plan&&initial_result.boolean_battery_evidence->runs.size()==initial_plan->size(),
                "An unchanged U1 starter skipped a required private phase");
            for(const auto& run:initial_result.boolean_battery_evidence->runs)
                require(run.compiled&&run.executed&&run.completed_normally,
                    "An unchanged U1 starter failed technically instead of receiving an objective verdict");
            const auto initial_capture=capture_cpp_execution(initial_request,initial_result);
            const auto initial_evaluation=evaluate_cpp_output_check(
                *initial_owner->output_check,initial_capture,initial_request.revision);
            require(initial_capture.execution_completed&&!initial_capture.boolean_battery_probe_correct&&
                    initial_capture.boolean_battery_exit_code==initial_result.execution.exit_code&&
                    initial_evaluation.verdict==OutputVerdict::incorrect&&
                    !initial_evaluation.feedback.empty(),
                    "An unchanged U1 starter acquired credit or lost its actual rejection feedback");
            require(!record_compiler_puzzle_case(recorded,lesson,*initial_owner,initial_request,
                        initial_capture,initial_evaluation.feedback)&&recorded==earned,
                    "An unchanged U1 starter changed an earned achievement");
        }
        std::cout << "PASS both authentic unchanged U1 starters rejected without changing progress\n";

        auto abort_source=first_solution(first);
        const auto return_position=abort_source.find("return failures ? 257 : 0;");
        require(return_position!=std::string::npos,"Missing normal-return fixture");
        abort_source.replace(return_position,std::string_view("return failures ? 257 : 0;").size(),
            "_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); std::abort();");
        const auto abort_request=request(first,std::move(abort_source));
        const auto abort_result=run_cpp(abort_request);
        require(abort_result.error_utf8.empty()&&abort_result.compilation.succeeded()&&
                abort_result.execution.status==CppPhaseStatus::completed&&
                abort_result.execution.exit_code==std::uint32_t{3},
                "The abnormal-return fixture did not actually execute abort(3)");
        const auto abort_capture=capture_cpp_execution(abort_request,abort_result);
        const auto rejected=evaluate_cpp_output_check(*first.output_check,abort_capture,abort_request.revision);
        require(!abort_capture.execution_completed&&!abort_capture.boolean_battery_probe_correct&&
                abort_capture.boolean_battery_exit_code==std::uint32_t{3}&&
                rejected.verdict==OutputVerdict::incorrect&&!rejected.feedback.empty(),
                "Abnormal exit 3 acquired normal-return credit or lost its actionable feedback");
        require(!record_compiler_puzzle_case(recorded,lesson,first,abort_request,abort_capture,rejected.feedback)&&
                recorded==earned,"A crashing battery changed its earlier achievement");
        require(record_compiler_puzzle_review(recorded,lesson,first,abort_request,abort_capture,rejected.feedback),
                "A genuine rejected attempt could not preserve its feedback");
        const auto review_state=state::decode(state::encode(recorded));
        require(compiler_puzzle_completed(review_state,progress,owner(progress,"block_0004_0004"))&&
                compiler_puzzle_review(review_state,progress,owner(progress,"block_0004_0004"),abort_request)==rejected.feedback,
                "Reopening lost either the achievement or the actual rejected-attempt feedback");
        auto other_request=abort_request;
        other_request.boolean_battery_probe->cases[0].expected=false;
        require(compiler_puzzle_review(review_state,lesson,first,other_request).empty(),
                "A rejection explanation was reused for another current battery contract");
        const auto review_untouched=recorded;
        require(!record_compiler_puzzle_review(recorded,lesson,first,other_request,abort_capture,"stale")&&recorded==review_untouched,
                "Old FreshExecution rebound a rejected attempt to a new current contract");
        std::cout << "PASS real abort rejection, feedback reopening and preserved achievement\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr << "FAIL boolean battery runtime: " << error.what() << '\n';
        return 1;
    }catch(const winrt::hresult_error& error) {
        std::cerr << "FAIL boolean battery runtime: " << winrt::to_string(error.message()) << '\n';
        return 1;
    }
}
