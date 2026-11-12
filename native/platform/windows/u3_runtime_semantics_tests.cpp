#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    require(in.good(),"The original U3 lesson asset is missing");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
std::string replace_once(std::string source,std::string_view old,std::string_view replacement) {
    const auto at=source.find(old);
    require(at!=source.npos&&source.find(old,at+old.size())==source.npos,
        "A U3 starter no longer has its original unique diagnostic body");
    source.replace(at,old.size(),replacement);
    return source;
}
void insert(JsonArray& records,std::string_view identity,std::string_view kind){
    JsonObject record;
    record.Insert(L"activity",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
    record.Insert(L"kind",JsonValue::CreateStringValue(winrt::to_hstring(kind)));
    records.Append(record);
}
struct Declared {
    Lesson original,lesson,metadata;
};
Declared with_reviewed_runtime(const std::filesystem::path& path,bool rejection) {
    const auto bytes=read(path);
    auto original=load_lesson(path,"es");
    auto root=JsonObject::Parse(winrt::to_hstring(bytes));
    require(!root.HasKey(L"compilerRuntimeChecks"),
        "Work has already published the U3 declaration; use its current contract");
    JsonArray records;
    insert(records,"block_0003_0003",rejection?
        "semanticRejectionBoxesV1":"observationCompleteV1");
    insert(records,"block_0006_0003",rejection?
        "semanticRejectionBalanceV1":"observationDifferencesV1");
    root.Insert(L"compilerRuntimeChecks",records);
    const auto revised=winrt::to_string(root.Stringify());
    auto lesson=parse_lesson_json(revised,"es");
    auto metadata=parse_lesson_progress_metadata(revised,"es");
    require(original.id==lesson.id&&lesson.id==metadata.id&&
        original.state_layout.lineage==lesson.state_layout.lineage&&
        original.state_layout.lineage==metadata.state_layout.lineage&&
        original.state_layout.signatures==lesson.state_layout.signatures&&
        original.state_layout.signatures==metadata.state_layout.signatures,
        "Adding a U3 runtime objective changed the original state identity");
    for(const auto id:{"block_0003_0003","block_0006_0003"}){
        const auto at=stable_block_position(lesson,id);
        const auto before=stable_block_position(original,id);
        const auto projection=stable_block_position(metadata,id);
        require(at&&before&&projection&&*at==*before&&*at==*projection,
            "The original U3 editor moved after a runtime declaration");
        const auto& owner=lesson.pages[at->page].blocks[at->block];
        const auto& old=original.pages[before->page].blocks[before->block];
        const auto& catalog=metadata.pages[projection->page].blocks[projection->block];
        require(owner.output_check&&catalog.output_check&&!old.output_check&&
            owner.initial_code==old.initial_code&&
            exercise_key(lesson,owner)==exercise_key(original,old)&&
            draft_key("programming",lesson,owner)==draft_key("programming",original,old)&&
            compiler_requires_completion(owner),
            "The U3 review changed a starter or lost mandatory completion");
    }
    return {std::move(original),std::move(lesson),std::move(metadata)};
}
std::string boxes(std::string source) {
    return replace_once(std::move(source),R"cpp(destino << "Error\n";)cpp",
        R"cpp(destino << "Cajas, peticion " << peticion << ", piezas " << piezas
            << ": rango permitido de 0 a 20; revise la cantidad.\n";)cpp");
}
std::string balances(std::string source) {
    return replace_once(std::move(source),R"cpp(destino << "Error de saldo\n";)cpp",
        R"cpp(destino << "Saldo, peticion " << peticion << ", cobros " << cobros
            << ", devoluciones " << devoluciones
            << ": rango permitido de 0 a 9; revise ambos datos.\n";)cpp");
}
std::string reports(std::string original,ReportMode mode) {
    constexpr std::string_view marker="int main() {";
    const auto at=original.find(marker);
    require(at!=original.npos&&original.find(marker,at+marker.size())==original.npos,
        "The original U3 report has no unique main");
    const auto body=R"cpp(
    std::string linea;
    int lineas{0};
    int validos{0};
    int coinciden{0};
    int difieren{0};
    int formato{0};
    const bool mostrar_todos{SHOW_ALL};
    while (std::getline(std::cin, linea)) {
        ++lineas;
        std::string caso;
        int esperado{0};
        int obtenido{0};
        if (!leer_registro(linea, caso, esperado, obtenido)) {
            ++formato;
            std::cerr << "Linea=" << lineas << ":FORMATO\n";
            continue;
        }
        const bool coincide{esperado == obtenido};
        ++validos;
        if (coincide) { ++coinciden; }
        else { ++difieren; }
        if (mostrar_todos || !coincide) {
            std::cout << "Linea=" << lineas << " Caso=" << caso
                      << " Esperado=" << esperado << " Obtenido=" << obtenido
                      << " " << (coincide ? "COINCIDE" : "DIFIERE") << "\n";
        }
    }
    if (std::cin.bad() || !std::cin.eof()) {
        std::cerr << "Fallo del origen\n";
        return 1;
    }
    std::cout << "Resumen: validos=" << validos << ";coinciden=" << coinciden
              << ";difieren=" << difieren << ";formato=" << formato << "\n";
    return (difieren > 0 || formato > 0) ? 1 : 0;)cpp";
    auto replacement=std::string(body);
    const auto all=replacement.find("SHOW_ALL");
    require(all!=replacement.npos,"The real report fixture lost its display selector");
    replacement.replace(all,8,mode==ReportMode::complete?"true":"false");
    const auto candidate=original.substr(0,at+marker.size())+replacement+"\n}\n";
    require(report_main_only_edit(original,candidate),
        "A correct U3 report changed something outside main");
    return candidate;
}

void actual_msvc_case(const Lesson& lesson,std::string_view identity,std::string source,
        const std::filesystem::path& projects,std::uint64_t request_id) {
    const auto at=stable_block_position(lesson,identity);
    require(at.has_value(),"A U3 runtime editor is missing");
    const auto& block=lesson.pages[at->page].blocks[at->block];
    require(block.output_check&&block.compiler_workflow_check,
        "The U3 editor lacks its exact in-memory authored objective");
    auto request=CppRunRequest{};
    request.request_id=request_id;
    request.revision=101+request_id;
    request.source_utf8=std::move(source);
    request.stdin_utf8=block.output_check->cases.front().standard_input;
    request.projects_directory=projects;
    request.compile_timeout=std::chrono::seconds(55);
    request.execution_timeout=std::chrono::seconds(5);
    if(block.output_check->rejection_operation)
        request.rejection_probe=block.output_check->rejection_operation;
    if(block.output_check->observation_report)
        request.observation_probe=block.output_check->observation_report;
    require(compiler_runtime_request_allowed(block,request),
        "The exact student edit failed the U3 source-isolation contract");
    state::StateData qaState;
    state::prepare_lesson_state(qaState,lesson);
    require(!compiler_puzzle_completed(qaState,lesson,block),
        "A fresh U3 objective inherited an old stdout-only achievement");
    const auto result=run_cpp(request);
    require(result.error_utf8.empty()&&result.compilation.succeeded(),
        "Real MSVC did not compile the U3 candidate: "+
        result.error_utf8+result.compilation.stderr_bytes);
    const auto captured=capture_cpp_execution(request,result);
    require(captured.execution_completed&&captured.exit_code==
        block.output_check->cases.front().exit_code,
        "The real U3 executable did not finish with the authored normal status");
    const auto expected=evaluate_cpp_output_check(*block.output_check,captured,request.revision);
    require(expected.verdict==OutputVerdict::correct,
        "The real MSVC capture failed stdout, stderr or the independent semantic probe: "+
        expected.feedback);
    require(block.output_check->rejection_operation?captured.rejection_probe_correct:
        captured.report_probe_correct,"A semantic test passed without real independent probe evidence");
    // Even after a verified execution, a failure of the separate probe does
    // not earn progress. This is a synthetic negative capture, never a claim
    // about the actual compiled student's program.
    auto forged=captured;
    forged.rejection_probe_correct=false;
    forged.report_probe_correct=false;
    require(evaluate_cpp_output_check(*block.output_check,forged,request.revision).verdict
            ==OutputVerdict::incorrect,
        "Successful stdout alone bypassed the required U3 independent runtime proof");
    require(!record_compiler_puzzle_case(qaState,lesson,block,request,forged,"fake probe")&&
        !compiler_puzzle_completed(qaState,lesson,block),
        "U3 accepted a fabricated independent probe receipt");
    require(record_compiler_puzzle_case(qaState,lesson,block,request,captured,"correct")&&
        compiler_puzzle_completed(qaState,lesson,block),
        "The first real authored U3 case did not unlock mandatory curricular progress");
    const auto restored=state::decode(state::encode(qaState));
    require(compiler_puzzle_completed(restored,lesson,block)&&
        compiler_puzzle_case_count(restored,lesson,block)==1,
        "U3 lost the sealed first-case completion or made optional cases mandatory");
    std::cout<<"PASS real MSVC U3 "<<identity<<", first case and independent probe\n";
}
} // namespace

int main() try {
    const LaunchOptions options{ProfileId::Codex,
        L"u3-runtime-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    const auto root=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles";
    const auto diagnostic=with_reviewed_runtime(
        root/"lesson_diagnosticos_para_corregir_una_peticion.json",true);
    const auto report=with_reviewed_runtime(
        root/"lesson_automatizar_la_revision_sin_borrar_evidencia.json",false);
    const auto projects=environment.paths.projects/L"U3 pruebas semánticas MSVC";
    const auto editor=[](const Lesson& lesson,std::string_view id)->const Block& {
        const auto position=stable_block_position(lesson,id);
        require(position.has_value(),"Missing U3 reviewer owner");
        return lesson.pages[position->page].blocks[position->block];
    };
    actual_msvc_case(diagnostic.lesson,"block_0003_0003",
        boxes(editor(diagnostic.original,"block_0003_0003").initial_code),projects,1);
    actual_msvc_case(diagnostic.lesson,"block_0006_0003",
        balances(editor(diagnostic.original,"block_0006_0003").initial_code),projects,2);
    actual_msvc_case(report.lesson,"block_0003_0003",
        reports(editor(report.original,"block_0003_0003").initial_code,ReportMode::complete),projects,3);
    actual_msvc_case(report.lesson,"block_0006_0003",
        reports(editor(report.original,"block_0006_0003").initial_code,ReportMode::differences),projects,4);
    std::cout<<"PASS four original U3 editors with real MSVC, semantic probes, sealed progress\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<"FAIL real U3 curricular runtime: "<<error.what()<<'\n';
    return 1;
}
