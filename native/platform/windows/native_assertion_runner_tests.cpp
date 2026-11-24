#include "core/lesson.hpp"
#include "core/assertion_runtime_check.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
using namespace lgaed::curriculum;

void require(bool success,std::string_view reason) {
    if(!success)throw std::runtime_error(std::string(reason));
}
std::string replace_once(std::string source,std::string_view previous,std::string_view revised) {
    const auto at=source.find(previous);
    require(at!=source.npos&&source.find(previous,at+previous.size())==source.npos,
        "An authenticated U3L9 source no longer has its unique mutation site");
    source.replace(at,previous.size(),revised);
    return source;
}
}

int main() {
    try {
        constexpr auto relative=
            "app/src/main/assets/content/es/programming/"
            "programming_practice_tools/comprobaciones_repetibles/"
            "lesson_comprobar_acuerdos_internos_con_aserciones.json";
        const auto lesson=load_lesson(std::filesystem::path(build::source_root)/relative);
        require(lesson.pages.size()==6,"U3L9 original page count changed");
        const auto progress=parse_lesson_progress_metadata(lesson.raw_json,"es");
        const auto& active=lesson.pages[2].blocks[3];
        const auto& disabled=lesson.pages[3].blocks[7];
        const auto& repair=lesson.pages[4].blocks[3];
        for(const auto& [block,mode,identity,original_sha256]:{
            std::tuple{&active,AssertionMode::enabled_abort,"block_0002_0003",
                "5a6740bc63eec4ab3ce410cc2f5524d85e732d6e6dd7915bc7822eff8b5772a8"},
            std::tuple{&disabled,AssertionMode::disabled,"block_0003_0007",
                "b1cce15a90f626f5940e5d7a40a2cf525a5bb47611e58c316ad589fb40fbb892"},
            std::tuple{&repair,AssertionMode::repaired,"block_0004_0007",
                "5a6740bc63eec4ab3ce410cc2f5524d85e732d6e6dd7915bc7822eff8b5772a8"}}) {
            require(block->stable_id==identity&&block->output_check&&
                block->output_check->assertion_check==mode&&
                block->output_check->cases.size()==1&&
                block->source_format_original_initial_code&&
                *block->source_format_original_initial_code!=block->initial_code&&
                state::sha256(*block->source_format_original_initial_code)==original_sha256,
                "U3L9 lost its native objective or authenticated pre-format source");
            const auto where=stable_block_position(progress,identity);
            require(where.has_value(),"The progress loader omitted an original U3L9 owner");
            const auto& lean=progress.pages.at(where->page).blocks.at(where->block);
            require(lean.output_check&&lean.output_check->cases.size()==1&&
                same_output_check_runtime_requirements(*block->output_check,*lean.output_check)&&
                same_output_case_requirements(block->output_check->cases.front(),lean.output_check->cases.front())&&
                lean.source_format_original_initial_code==block->source_format_original_initial_code&&
                reference_objective_frame(lesson,*block)==reference_objective_frame(progress,lean)&&
                draft_key("programming",lesson,*block)==draft_key("programming",progress,lean),
                "Full and progress loaders disagree about the U3L9 source, objective or draft");
            const auto wrong_mode=mode==AssertionMode::disabled
                ?AssertionMode::enabled_abort:AssertionMode::disabled;
            require(!assertion_source_allowed(*block,wrong_mode,block->initial_code),
                "A source was admitted under another assertion objective");
            auto undeclared=*block;undeclared.output_check->assertion_check.reset();
            require(!assertion_source_allowed(undeclared,mode,block->initial_code)&&
                !assertion_source_allowed(undeclared,mode,*block->source_format_original_initial_code),
                "An undeclared assertion goal borrowed a current or historical source");
        }
        const LaunchOptions options{ProfileId::Codex,
            L"native-assertion-qa-"+std::to_wstring(GetCurrentProcessId())+
            L"-"+std::to_wstring(GetTickCount64()),false};
        const auto environment=resolve_windows_environment(options);
        initialize_profile_directories(environment.paths);
        std::uint64_t counter=0;
        const auto run=[&](const Block& block,AssertionMode mode,std::string source) {
            CppRunRequest req;
            req.request_id=++counter;
            req.revision=500+counter;
            req.action=CppRunRequest::Action::compile_run;
            req.backend=CppRunRequest::Backend::native_msvc;
            req.assertion_mode=mode;
            req.source_utf8=std::move(source);
            req.stdin_utf8=block.standard_input.value_or("");
            req.projects_directory=environment.paths.projects/L"native-u3l9-qa";
            require(compiler_runtime_request_allowed(block,req),
                "The reviewed U3L9 source was not admitted under native MSVC");
            const auto result=run_cpp(req);
            require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                result.execution.status==CppPhaseStatus::completed&&
                result.execution.exit_code.has_value()&&
                result.target_type=="EXECUTABLE"&&
                result.artifact.extension()==L".exe"&&
                std::filesystem::is_regular_file(result.artifact),
                std::string("Windows child process did not complete its real MSVC compilation and execution: ")+
                "error="+result.error_utf8+
                " compile_status="+std::to_string(static_cast<int>(result.compilation.status))+
                " compile_exit="+std::to_string(result.compilation.exit_code.value_or(999999u))+
                " compile_stdout="+result.compilation.stdout_bytes+
                " compile_stderr="+result.compilation.stderr_bytes+
                " run_status="+std::to_string(static_cast<int>(result.execution.status))+
                " run_exit="+std::to_string(result.execution.exit_code.value_or(999999u))+
                " target="+result.target_type);
            const auto compiled=state::read_bytes(result.run_directory/L"main.cpp");
            require(std::string(compiled.begin(),compiled.end())==req.source_utf8,
                "The runner rewrote the supplied current or historical source");
            const auto evidence=capture_cpp_execution(req,result);
            const auto verdict=evaluate_cpp_output_check(*block.output_check,evidence,req.revision);
            require(verdict.verdict==OutputVerdict::correct,
                std::string("An authenticated U3L9 native check was not credited: ")+
                verdict.feedback+" stdout="+result.execution.stdout_bytes+
                " stderr="+result.execution.stderr_bytes);
            require(evaluate_cpp_output_check(*block.output_check,evidence,req.revision+1).verdict
                    ==OutputVerdict::unavailable,
                "A stale source revision borrowed a valid native assertion proof");
            return std::tuple{req,result,evidence};
        };
        const auto [active_request,active_result,active_evidence]=
            run(active,AssertionMode::enabled_abort,active.initial_code);
        require(active_evidence.assertion_abort_verified&&
            active_result.execution.exit_code==0xC0000409u&&
            !active_evidence.execution_succeeded&&active_evidence.execution_completed&&
            assertion_native_abort(active_result.execution.stdout_bytes,
                active_result.execution.stderr_bytes,
                *active_result.execution.exit_code),
            "MSVC CRT fast-fail was not authenticated by its exact original source and diagnostic");
        auto forged=active_result;
        forged.execution.stderr_bytes="Unrelated access violation";
        const auto fake=capture_cpp_execution(active_request,forged);
        require(!fake.assertion_abort_verified&&
            evaluate_cpp_output_check(*active.output_check,fake,active_request.revision).verdict
                !=OutputVerdict::correct,
            "A different native crash forged the original assertion");
        forged=active_result;
        forged.execution.exit_code=1u;
        require(!capture_cpp_execution(active_request,forged).assertion_abort_verified,
            "Any arbitrary nonzero process status was treated as a CRT fast-fail");
        const auto [disabled_request,disabled_result,disabled_evidence]=
            run(disabled,AssertionMode::disabled,disabled.initial_code);
        require(disabled_evidence.exit_code==1&&!disabled_evidence.assertion_abort_verified&&
            assertion_native_lines("Cajas=1\n",disabled_result.execution.stdout_bytes),
            "NDEBUG disabled the public rejection or did not preserve the old wrong calculation");
        constexpr std::string_view source="const int cajas = piezas / piezas_por_caja;";
        constexpr std::string_view revised=
            "const int cajas = (piezas + piezas_por_caja - 1) / piezas_por_caja;";
        const auto corrected=replace_once(repair.initial_code,source,revised);
        const auto [repair_request,repair_result,repair_evidence]=
            run(repair,AssertionMode::repaired,corrected);
        require(repair_evidence.assertion_repair_probes_correct&&
            repair_result.assertion_probes.size()==repaired_assertion_inputs.size()&&
            repair_evidence.exit_code==1,
            "The repaired formula was accepted without two independent native MSVC executions");
        // These are the exact sources retained by the authenticated format
        // archive, not current starters substituted for a student's draft.
        for(const auto& [block,mode]:{
            std::pair{&active,AssertionMode::enabled_abort},
            std::pair{&disabled,AssertionMode::disabled},
            std::pair{&repair,AssertionMode::repaired}}) {
            const auto saved=mode==AssertionMode::repaired
                ?replace_once(*block->source_format_original_initial_code,source,revised)
                :*block->source_format_original_initial_code;
            const auto [saved_request,saved_result,saved_evidence]=run(*block,mode,saved);
            require(saved_request.source_utf8==saved,
                "A pre-format editor was replaced by the current authored starter");
            if(mode==AssertionMode::enabled_abort)
                require(saved_evidence.assertion_abort_verified&&
                    saved_result.execution.exit_code==0xC0000409u,
                    "The pre-format source did not retain its authentic MSVC abort");
            else if(mode==AssertionMode::disabled)
                require(saved_evidence.exit_code==1&&!saved_evidence.assertion_abort_verified&&
                    assertion_native_lines("Cajas=1\n",saved_result.execution.stdout_bytes),
                    "The pre-format NDEBUG source lost the original calculation or public rejection");
            else
                require(saved_evidence.assertion_repair_probes_correct&&
                    saved_result.assertion_probes.size()==repaired_assertion_inputs.size()&&
                    saved_evidence.exit_code==1,
                    "The pre-format repair bypassed an independent native observation");
        }
        CppRunRequest copied=repair_request;
        for(const auto* original:{&repair.initial_code,&*repair.source_format_original_initial_code}) {
            copied.request_id=++counter;copied.revision=500+counter;
            copied.source_utf8=replace_once(*original,source,"const int cajas = 2;");
            require(compiler_runtime_request_allowed(repair,copied),
                "The deliberately overfitted candidate did not reach the semantic evaluation");
            const auto candidate=run_cpp(copied);
            require(candidate.error_utf8.empty()&&candidate.compilation.succeeded(),
                "Could not compile a current or pre-format U3L9 counterexample");
            const auto outcome=capture_cpp_execution(copied,candidate);
            require(!outcome.assertion_repair_probes_correct&&
                evaluate_cpp_output_check(*repair.output_check,outcome,copied.revision).verdict
                    !=OutputVerdict::correct,
                "Hardcoded prepared output passed without genuine independent calculations");
        }
        auto removed=corrected;
        constexpr std::string_view assertion="assert(cajas * piezas_por_caja >= piezas);";
        const auto marker=removed.find(assertion);
        require(marker!=removed.npos,"Source assertion no longer exists");
        removed.erase(marker,assertion.size());
        copied.source_utf8=std::move(removed);
        require(!compiler_runtime_request_allowed(repair,copied),
            "Removing assert bypassed the authenticated source boundary");
        const auto historical_repair=replace_once(
            *repair.source_format_original_initial_code,source,revised);
        for(const auto& invalid:{
            replace_once(historical_repair,assertion,""),
            replace_once(historical_repair,"return hubo_rechazos ? 1 : 0;","return 0;"),
            replace_once(historical_repair,revised,"const int cajas = 2; std::cout << \"forged\";")}) {
            copied=repair_request;copied.source_utf8=invalid;
            require(!compiler_runtime_request_allowed(repair,copied),
                "A historical format archive permitted changing assert, main or the expression boundary");
        }
        copied=disabled_request;
        copied.source_utf8=replace_once(*disabled.source_format_original_initial_code,"#define NDEBUG\n","");
        require(!compiler_runtime_request_allowed(disabled,copied),
            "Removing NDEBUG borrowed the historical disabled-assertion objective");
        copied=active_request;
        copied.source_utf8="#define NDEBUG\n"+*active.source_format_original_initial_code;
        require(!compiler_runtime_request_allowed(active,copied),
            "Adding NDEBUG borrowed the historical active-assertion objective");
        copied=active_request;
        copied.backend=static_cast<CppRunRequest::Backend>(1);
        require(!compiler_runtime_request_allowed(active,copied),
            "An unsupported backend could still be substituted for native MSVC evidence");
        std::cout<<"PASS native U3L9 current and authenticated pre-format abort, NDEBUG, repaired probes, full/progress contracts, exact sources, stale protection and hostile counterexamples\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL MSVC native assertion integration: "<<e.what()<<'\n';
        return 1;
    }
}
