#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
using namespace std::chrono_literals;
void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);require(bool(input),"Missing owned diagnostic QA artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::string path_utf8(const std::filesystem::path& path) {
    const auto bytes=path.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
std::string localized_diagnostic(std::string stream,std::string_view code) {
    const auto occurrence=stream.find(" "+std::string(code)+':');
    if(occurrence==stream.npos)return stream;
    const auto close=stream.rfind(')',occurrence);require(close!=stream.npos,"The localized diagnostic has no source coordinates");
    const auto end=stream.find('\n',occurrence);
    stream.replace(close+1,(end==stream.npos?stream.size():end)-(close+1),": diagnostique "+std::string(code)+": rejet sémantique indépendant de la langue.");
    return stream;
}
struct Case {std::string_view file,identity,statement,code;};
constexpr std::array<Case,5> cases{{
    {"lesson_consultas_const_y_objetos_constantes.json","block_0006_0006","fija.cambiar_a(4,2);","C2662"},
    {"lesson_valores_por_defecto_con_significado.json","block_0012_0008","Contador no_permitido = 3;","C2440"},
    {"lesson_valores_por_defecto_con_significado.json","block_0012_0011","leer(3);","C2664"},
    {"lesson_valores_por_defecto_con_significado.json","block_0014_0011","destino = 3;","C2679"},
    {"lesson_argumentos_con_tipos_que_expresan_su_funcion.json","block_0006_0006","const Caja mal{Alto{4}, Ancho{3}};","C2440"}
}};
void never_executed(const CppRunResult& result) {
    require(result.execution.status==CppPhaseStatus::not_started&&!result.execution.exit_code&&
        result.configuration.status==CppPhaseStatus::not_started&&result.artifact.empty()&&!result.executable&&result.target_type.empty(),
        "Diagnostic compilation linked an executable or invoked an execution/configuration phase");
    if(!result.run_directory.empty())for(const auto& entry:std::filesystem::recursive_directory_iterator(result.run_directory))
        require(entry.path().extension()!=L".exe"&&entry.path().extension()!=L".dll"&&
            entry.path().filename()!=L"execution-stdout.bin","A diagnostic-only run produced an executable or execution stream");
}
}

int main() {
    const LaunchOptions options{ProfileId::Codex,L"diagnostic-runner-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    const auto projects=environment.paths.projects/L"Código & (diagnóstico)";
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido";
    std::uint64_t sequence=0;unsigned passed=0,failed=0;
    const auto test=[&](std::string_view name,const std::function<void()>& action){try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    const auto request=[&](const Case& item){
        const auto lesson=load_lesson(folder/item.file);const auto position=curriculum::stable_block_position(lesson,item.identity);
        require(position.has_value(),"The diagnostic QA lost an actual U9 editor");
        CppRunRequest value;value.request_id=++sequence;value.revision=17+sequence;value.projects_directory=projects;
        value.source_utf8=lesson.pages[position->page].blocks[position->block].initial_code;
        CppDiagnosticRequest objective;objective.control_source_utf8=value.source_utf8;objective.expected_error_code=item.code;
        const auto insertion=value.source_utf8.rfind("    return 0;");require(insertion!=value.source_utf8.npos,"The diagnostic QA lost the insertion before main's return");
        objective.first_line=objective.last_line=1+static_cast<std::uint32_t>(std::count(value.source_utf8.begin(),value.source_utf8.begin()+insertion,'\n'));
        value.source_utf8.insert(insertion,"    "+std::string(item.statement)+'\n');value.diagnostic=std::move(objective);return value;
    };
    std::array<std::string,cases.size()> original_assets;
    for(std::size_t index=0;index<cases.size();++index)original_assets[index]=state::sha256(read(folder/cases[index].file));
    for(const auto& item:cases)test("actual U9 semantic rejection "+std::string(item.identity)+" / "+std::string(item.code),[&]{
        const auto value=request(item);const auto result=run_cpp(value);
        require(result.error_utf8.empty(),"Diagnostic adapter failed: "+result.error_utf8);
        require(result.diagnostic&&result.diagnostic->control.succeeded(),"The original valid program did not compile as a fresh control");
        require(expected_compile_diagnostic(value,result)&&result.diagnostic->expected_rejection,"The requested semantic rejection was not accredited: "+result.compilation.stdout_bytes+result.compilation.stderr_bytes);
        require(!result.diagnostic->errors.empty()&&std::ranges::all_of(result.diagnostic->errors,[&](const auto& error){return error.code==item.code&&error.line==value.diagnostic->first_line&&error.column>0;}),
            "The semantic proof was not attached to the inserted statement");
        require(read(result.run_directory/L"control/main.cpp")==value.diagnostic->control_source_utf8&&read(result.run_directory/L"candidate/main.cpp")==value.source_utf8,
            "Diagnostic capture rewrote a source character");
        require(read(result.run_directory/L"control/compilation-stdout.bin")==result.diagnostic->control.stdout_bytes&&
            read(result.run_directory/L"control/compilation-stderr.bin")==result.diagnostic->control.stderr_bytes&&
            read(result.run_directory/L"candidate/compilation-stdout.bin")==result.compilation.stdout_bytes&&
            read(result.run_directory/L"candidate/compilation-stderr.bin")==result.compilation.stderr_bytes,
            "Structured diagnosis rewrote a captured compiler stream");
        never_executed(result);
        auto changed=result;changed.diagnostic->expected_rejection=false;
        require(expected_compile_diagnostic(value,changed),"The execution evaluator trusted a summary boolean instead of re-evaluating evidence");
        changed=result;changed.compilation.stdout_bytes=localized_diagnostic(changed.compilation.stdout_bytes,item.code);
        changed.compilation.stderr_bytes=localized_diagnostic(changed.compilation.stderr_bytes,item.code);
        require(expected_compile_diagnostic(value,changed),"Translated severity/prose changed the invariant diagnostic objective");
        changed=result;++changed.request_id;require(!expected_compile_diagnostic(value,changed),"A different request accredited the diagnostic");
        changed=result;++changed.revision;require(!expected_compile_diagnostic(value,changed),"A stale revision accredited the diagnostic");
        auto other=value;other.source_utf8+="\n// edited source";require(!expected_compile_diagnostic(other,result),"Changed source reused the old rejection");
        other=value;other.source_files={"other.cpp"};require(!expected_compile_diagnostic(other,result),"A different source selection reused the single-file rejection");
        other=value;other.stdin_utf8="ignored input";require(!expected_compile_diagnostic(other,result),"Standard input was silently ignored by a diagnostic proof");
        other=value;other.diagnostic->control_source_utf8+="\n// edited control";require(!expected_compile_diagnostic(other,result),"Changed control reused the old rejection");
        other=value;other.diagnostic->first_line+=2;other.diagnostic->last_line+=2;require(!expected_compile_diagnostic(other,result),"A diagnostic outside the required source interval accredited the objective");
        other=value;other.diagnostic->expected_error_code="C2143";require(!expected_compile_diagnostic(other,result),"An unrelated error code accredited the objective");
        changed=result;changed.diagnostic->errors[0].code="C2143";require(!expected_compile_diagnostic(value,changed),"Altered structured diagnostics overrode the raw compiler streams");
        const auto extra_error=[&](CppRunResult& copy,CppCompilerDiagnostic extra){
            copy.compilation.stderr_bytes+=path_utf8(extra.source)+'('+std::to_string(extra.line)+','+std::to_string(extra.column)+"): diagnostique "+extra.code+": autre rejet.\n";
            copy.diagnostic->errors.push_back(std::move(extra));
        };
        changed=result;auto extra=result.diagnostic->errors.front();extra.line=value.diagnostic->last_line+2;extra_error(changed,extra);
        require(!expected_compile_diagnostic(value,changed),"An additional primary error outside the introduced statement was ignored");
        changed=result;extra=result.diagnostic->errors.front();extra.source=result.run_directory/L"control/main.cpp";extra_error(changed,extra);
        require(!expected_compile_diagnostic(value,changed),"An error in another translation unit credited the required statement");
        changed=result;extra=result.diagnostic->errors.front();extra.code="C2143";extra_error(changed,extra);
        require(!expected_compile_diagnostic(value,changed),"A second unrelated primary syntax error was ignored");
        changed=result;changed.compilation.stderr_bytes+="main.cpp(1,1): avertissement C4999: diagnostic sans incidence.\n";
        require(expected_compile_diagnostic(value,changed),"A translated compiler warning was mistaken for a primary rejection");
        changed=result;changed.compilation.exit_code=0;require(!expected_compile_diagnostic(value,changed),"Successful compilation accredited an intended rejection");
        changed=result;changed.compilation.exit_code=0xc0000005u;require(!expected_compile_diagnostic(value,changed),"A compiler crash was interpreted as semantic rejection");
        changed=result;changed.error_utf8="technical failure";require(!expected_compile_diagnostic(value,changed),"A technical adapter failure accredited the rejection");
        changed=result;changed.diagnostic->control.status=CppPhaseStatus::launch_failed;require(!expected_compile_diagnostic(value,changed),"A failed control accredited the candidate");
        changed=result;changed.execution.status=CppPhaseStatus::completed;changed.execution.exit_code=0;
        require(!expected_compile_diagnostic(value,changed),"Executed code was accepted as compilation-only evidence");
        for(const auto status:{CppPhaseStatus::cancelled,CppPhaseStatus::timed_out,CppPhaseStatus::output_limit,CppPhaseStatus::launch_failed,CppPhaseStatus::not_started}){
            changed=result;changed.compilation.status=status;require(!expected_compile_diagnostic(value,changed),"An unfinished diagnostic phase accredited the rejection");}
    });
    test("valid initial source compiles without running or accrediting a rejection",[&]{
        auto value=request(cases.front());value.source_utf8=value.diagnostic->control_source_utf8;
        value.diagnostic->expected_error_code.clear();value.diagnostic->first_line=value.diagnostic->last_line=0;
        const auto result=run_cpp(value);require(result.diagnostic&&result.diagnostic->control.succeeded()&&result.compilation.succeeded(),"A valid diagnostic-editor program failed compilation");
        require(!result.diagnostic->expected_rejection&&!expected_compile_diagnostic(value,result),"A valid initial program falsely accredited a semantic rejection");
        auto forged=result;forged.diagnostic->expected_rejection=true;require(!expected_compile_diagnostic(value,forged),"A forged summary boolean accredited an initial valid program");never_executed(result);
    });
    test("removing const makes the call valid and cannot accredit the rejection",[&]{
        auto value=request(cases.front());const auto immutable=value.source_utf8.find("const Dimension fija");require(immutable!=value.source_utf8.npos,"The const contrast lost its immutable object");
        value.source_utf8.erase(immutable,6);const auto result=run_cpp(value);
        require(result.compilation.succeeded()&&!expected_compile_diagnostic(value,result),"A call on a mutable object accredited the const rejection");never_executed(result);
    });
    test("an independent syntax error cannot substitute the requested semantic rejection",[&]{
        auto value=request(cases.front());value.source_utf8="int unrelated = ;\n"+value.source_utf8;
        ++value.diagnostic->first_line;++value.diagnostic->last_line;const auto result=run_cpp(value);
        require(result.diagnostic&&result.diagnostic->control.succeeded()&&result.compilation.status==CppPhaseStatus::completed&&!result.compilation.succeeded(),"The syntax contrast did not obtain its own real compiler diagnostics");
        require(!expected_compile_diagnostic(value,result)&&!result.diagnostic->expected_rejection,"An independent syntax error credited the semantic objective");never_executed(result);
    });
    test("an invalid control preserves its compiler diagnostics and cannot accredit a candidate",[&]{
        auto value=request(cases.front());value.diagnostic->control_source_utf8="#error QA_CONTROL_REJECTED\n"+value.diagnostic->control_source_utf8;
        const auto result=run_cpp(value);require(result.diagnostic&&!result.diagnostic->control.succeeded()&&
            (result.diagnostic->control.stdout_bytes+result.diagnostic->control.stderr_bytes).find("QA_CONTROL_REJECTED")!=std::string::npos,
            "A rejected control lost its real diagnostics");require(!expected_compile_diagnostic(value,result),"A rejected control accredited a candidate");never_executed(result);
    });
    test("cancelled diagnostic request starts no executable or semantic proof",[&]{
        const auto value=request(cases.front());std::stop_source stop;stop.request_stop();const auto result=run_cpp(value,stop.get_token());
        require(result.compilation.status==CppPhaseStatus::cancelled&&result.run_directory.empty()&&!expected_compile_diagnostic(value,result),"A cancelled diagnostic started work or accredited an objective");never_executed(result);
    });
    test("diagnostic timeout and output limits are technical results without accreditation",[&]{
        auto value=request(cases.front());value.compile_timeout=1ms;auto result=run_cpp(value);
        require(result.diagnostic&&(result.diagnostic->control.status==CppPhaseStatus::timed_out||result.compilation.status==CppPhaseStatus::timed_out)&&
            !expected_compile_diagnostic(value,result),"An exceeded diagnostic time budget accredited a rejection");never_executed(result);
        value=request(cases.front());value.output_limit_bytes=1;result=run_cpp(value);
        require(result.diagnostic&&(result.diagnostic->control.status==CppPhaseStatus::output_limit||result.compilation.status==CppPhaseStatus::output_limit)&&
            !expected_compile_diagnostic(value,result),"Clipped diagnostic output accredited a rejection");never_executed(result);
    });
    test("invalid private run path cannot fall back to the application directory",[&]{
        auto value=request(cases.front());value.projects_directory=L"relative";const auto result=run_cpp(value);
        require(result.compilation.status==CppPhaseStatus::launch_failed&&!result.error_utf8.empty()&&result.run_directory.empty()&&!expected_compile_diagnostic(value,result),
            "A diagnostic path error accredited an objective or fell back to a shared directory");never_executed(result);
    });
    test("diagnostic requests reject source selections and stdin before starting work",[&]{
        auto value=request(cases.front());value.source_files={"other.cpp"};auto result=run_cpp(value);
        require(result.compilation.status==CppPhaseStatus::launch_failed&&result.run_directory.empty()&&!result.error_utf8.empty()&&
            !expected_compile_diagnostic(value,result),"A diagnostic silently ignored a nonempty source selection");never_executed(result);
        value=request(cases.front());value.stdin_utf8="ignored input";result=run_cpp(value);
        require(result.compilation.status==CppPhaseStatus::launch_failed&&result.run_directory.empty()&&!result.error_utf8.empty()&&
            !expected_compile_diagnostic(value,result),"A diagnostic silently ignored standard input");never_executed(result);
    });
    test("all five original U9 assets remain byte-exact",[&]{for(std::size_t index=0;index<cases.size();++index)
        require(state::sha256(read(folder/cases[index].file))==original_assets[index],"Diagnostic QA changed an authored asset");});
    std::cout<<"Real MSVC compilation-only diagnostics: "<<passed<<" passed, "<<failed<<" failed.\n";return failed?1:0;
}
