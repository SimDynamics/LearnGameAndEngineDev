#include "core/lesson.hpp"
#include "core/assertion_runtime_check.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
using namespace lgaed::curriculum;

void require(bool success,std::string_view reason) {
    if(!success)throw std::runtime_error(std::string(reason));
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
        const auto& active=lesson.pages[2].blocks[3];
        const auto& disabled=lesson.pages[3].blocks[7];
        const auto& repair=lesson.pages[4].blocks[3];
        for(const auto& [block,mode,identity]:{
            std::tuple{&active,AssertionMode::enabled_abort,"block_0002_0003"},
            std::tuple{&disabled,AssertionMode::disabled,"block_0003_0007"},
            std::tuple{&repair,AssertionMode::repaired,"block_0004_0007"}}) {
            require(block->stable_id==identity&&block->output_check&&
                block->output_check->assertion_check==mode&&
                block->output_check->cases.size()==1,
                "U3L9 lost a sealed native assertion objective");
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
        auto corrected=repair.initial_code;
        constexpr std::string_view source="const int cajas = piezas / piezas_por_caja;";
        const auto start=corrected.find(source);
        require(start!=corrected.npos&&corrected.find(source,start+1)==corrected.npos,
            "The original U3L9 formula cannot be uniquely repaired");
        corrected.replace(start,source.size(),
            "const int cajas = (piezas + piezas_por_caja - 1) / piezas_por_caja;");
        const auto [repair_request,repair_result,repair_evidence]=
            run(repair,AssertionMode::repaired,corrected);
        require(repair_evidence.assertion_repair_probes_correct&&
            repair_result.assertion_probes.size()==repaired_assertion_inputs.size()&&
            repair_evidence.exit_code==1,
            "The repaired formula was accepted without two independent native MSVC executions");
        auto hardcoded=repair.initial_code;
        hardcoded.replace(start,source.size(),"const int cajas = 2;");
        CppRunRequest copied=repair_request;
        copied.request_id=++counter;copied.revision=500+counter;
        copied.source_utf8=hardcoded;
        require(compiler_runtime_request_allowed(repair,copied),
            "The deliberately overfitted candidate did not reach the semantic evaluation");
        const auto candidate=run_cpp(copied);
        require(candidate.error_utf8.empty()&&candidate.compilation.succeeded(),
            "Could not compile the hardcoded original U3L9 counterexample");
        const auto outcome=capture_cpp_execution(copied,candidate);
        require(!outcome.assertion_repair_probes_correct&&
            evaluate_cpp_output_check(*repair.output_check,outcome,copied.revision).verdict
                !=OutputVerdict::correct,
            "Hardcoded prepared output passed without genuine independent calculations");
        auto removed=corrected;
        constexpr std::string_view assertion="assert(cajas * piezas_por_caja >= piezas);";
        const auto marker=removed.find(assertion);
        require(marker!=removed.npos,"Source assertion no longer exists");
        removed.erase(marker,assertion.size());
        copied.source_utf8=std::move(removed);
        require(!compiler_runtime_request_allowed(repair,copied),
            "Removing assert bypassed the authenticated source boundary");
        copied=active_request;
        copied.backend=CppRunRequest::Backend::wasi;
        require(!compiler_runtime_request_allowed(active,copied),
            "A WASI runner could still be substituted for native MSVC evidence");
        std::cout<<"PASS native U3L9 original abort, NDEBUG, repaired probes, stale protection and hostile counterexamples\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL MSVC native assertion integration: "<<e.what()<<'\n';
        return 1;
    }
}
