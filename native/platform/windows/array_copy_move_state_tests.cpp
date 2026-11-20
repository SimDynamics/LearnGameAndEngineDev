// Windows StateStore and lesson-loader QA for the opt-in U6L11 array-copy-move policy.
// This models receipt compatibility; the independent C++ compiler probe
// is run separately and never replaced by the modeled public output.
#include "core/cpp_array_copy_move_check.hpp"
#include "core/lesson.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::test {
namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
const Block& owner(const Lesson& lesson,std::string_view id) {
    const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"U6L11 original owner not found");
    return lesson.pages[at->page].blocks[at->block];
}
state::StateData reopen(const state::StateStore& previous) {
    state::StateStore independent(previous.root().parent_path());
    return independent.load();
}
} // namespace

int array_copy_move_state_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    constexpr std::string_view id="block_0020_0002";
    const auto path=content.parent_path()/
        "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
        "lesson_copiar_y_mover_arrays_sin_desajustar_la_cantidad.json";
    const auto original=load_lesson(path);
    const auto& before=owner(original,id);
    require(before.type==BlockType::cpp_compiler&&before.output_check&&
            before.output_check->cases.size()==3&&
            !before.array_copy_move_probe,
        "Published U6L11 source or original cases changed unexpectedly");
    const auto& first_case=before.output_check->cases.front();
    require(!first_case.outputs.empty(),"Published U6L11 first case absent");

    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto declaration=JsonObject::Parse(winrt::hstring{LR"array({
        "activity":"block_0020_0002",
        "className":"Tramo",
        "ownerMember":"datos_",
        "countMember":"cantidad_",
        "copyParameter":"otra",
        "moveParameter":"otra",
        "quantityMethod":"cantidad",
        "accessMethod":"acceso",
        "configureMethod":"configurar",
        "readMethod":"leer",
        "writeMethod":"escribir",
        "clearMethod":"vaciar",
        "sumMethod":"sumar"
    })array"});
    JsonArray checks;
    checks.Append(declaration);
    root.Insert(L"compilerArrayCopyMoveChecks",checks);
    const auto revised_source=winrt::to_string(root.Stringify());
    const auto revised=parse_lesson_json(revised_source,"es");
    const auto progress=parse_lesson_progress_metadata(revised_source,"es");
    const auto& editor=owner(revised,id);
    const auto& compact=owner(progress,id);
    require(editor.array_copy_move_probe&&
            compact.array_copy_move_probe&&
            editor.array_copy_move_probe==
                compact.array_copy_move_probe,
        "Progress and full loader disagree on U6L11 policy");
    require(editor.initial_code==before.initial_code&&
            editor.stable_id==before.stable_id&&
            editor.address.page==before.address.page&&
            editor.address.block==before.address.block&&
            editor.address.progress_page==before.address.progress_page&&
            editor.address.addition_namespace==before.address.addition_namespace&&
            editor.output_check&&
            editor.output_check->cases.size()==before.output_check->cases.size()&&
            JsonObject::Parse(winrt::to_hstring(editor.raw_json)).Stringify()==
                JsonObject::Parse(winrt::to_hstring(before.raw_json)).Stringify()&&
            revised.state_layout.signatures==original.state_layout.signatures&&
            progress.state_layout.signatures==original.state_layout.signatures,
        "Additional policy changed published U6L11 source, answers or owners");

    constexpr std::string_view copy_signature=
        "Tramo& Tramo::operator=(const Tramo& otra) {";
    constexpr std::string_view move_signature=
        "Tramo& Tramo::operator=(Tramo&& otra) noexcept {";
    auto program=before.initial_code;
    const auto copy_at=program.find(copy_signature);
    require(copy_at!=program.npos,"Original U6L11 copy assignment missing");
    const auto copy_end=program.find("\n}\nTramo& Tramo::operator=(Tramo&&",
        copy_at+copy_signature.size());
    require(copy_end!=program.npos,"Original U6L11 copy body not delimited");
    program.replace(copy_at+copy_signature.size(),
        copy_end-copy_at-copy_signature.size(),R"cpp(
    if(this==&otra)return *this;
    Tramo candidate(otra);
    intercambiar(candidate);
    return *this;)cpp");
    const auto move_at=program.find(move_signature);
    require(move_at!=program.npos,"Original U6L11 move assignment missing");
    const auto move_end=program.find("\n}\nbool Tramo::configurar",
        move_at+move_signature.size());
    require(move_end!=program.npos,"Original U6L11 move body not delimited");
    program.replace(move_at+move_signature.size(),
        move_end-move_at-move_signature.size(),R"cpp(
    if(this==&otra)return *this;
    datos_=std::move(otra.datos_);
    cantidad_=std::exchange(otra.cantidad_,0);
    return *this;)cpp");
    require(cpp_array_copy_move_matches(
                *editor.array_copy_move_probe,program)&&
            cpp_array_copy_move_private_driver(
                *editor.array_copy_move_probe,program).has_value(),
        "An equivalent U6L11 pair of assignments was rejected by the source gate");

    CppRunRequest request;
    request.request_id=611;
    request.revision=20;
    request.source_utf8=program;
    request.stdin_utf8=first_case.standard_input;
    FreshExecution observed;
    observed.source_revision=request.revision;
    observed.compilation_succeeded=true;
    observed.execution_succeeded=true;
    observed.execution_completed=true;
    observed.exit_code=0;
    observed.standard_input=request.stdin_utf8;
    observed.standard_output=first_case.outputs.front();
    observed.executable=first_case.executable;

    // First, establish an authentic *old* stdout-only completion receipt.
    state::StateData data;
    state::prepare_lesson_state(data,original);
    state::Draft draft;
    draft.source=program;
    draft.input=request.stdin_utf8;
    state::put_draft(data,curriculum::draft_key("programming",original,before),draft);
    data.preferences["qa-unrelated"]["source"]=std::string("unchanged");
    require(record_compiler_puzzle_case(data,original,before,request,observed,
                "Original U6L11 stdout-only proof")&&
            compiler_puzzle_completed(data,original,before),
        "Old U6L11 output receipt was not created");
    const auto historical=data.preferences.at("native_windows");
    const auto snapshot=data;
    state::StateStore store(qa/"array-copy-move-state");
    require(store.load().preferences.empty(),"U6L11 private QA state was not fresh");
    store.save(data);
    data=reopen(store);
    require(data==snapshot,"Historical U6L11 bytes changed on reopen");
    state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);
    require(!compiler_puzzle_completed(data,revised,editor),
        "Historical stdout silently credited the new array ownership and reads");

    const auto unchanged=data;
    request.array_copy_move_probe=editor.array_copy_move_probe;
    require(compiler_runtime_request_allowed(editor,request),
        "Valid U6L11 code rejected by the live Windows request gate");
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"public stdout only")&&data==unchanged,
        "U6L11 public stdout alone earned the private objective");
    observed.array_copy_move_probe_correct=true;
    observed.array_copy_move_request_fingerprint="unrelated";
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"forged unrelated capture")&&data==unchanged,
        "U6L11 unbound private result altered historical state");
    observed.array_copy_move_request_fingerprint=
        cpp_array_copy_move_request_fingerprint(request);
    require(!observed.array_copy_move_request_fingerprint.empty()&&
            record_compiler_puzzle_case(data,revised,editor,request,observed,
                "Genuine independent move witness")&&
            compiler_puzzle_completed(data,revised,editor),
        "U6L11 bound private witness not credited");
    const auto& stored=data.preferences.at("native_windows");
    for(const auto& [key,value]:historical) {
        const auto found=stored.find(key);
        require(found!=stored.end()&&found->second==value,
            "New U6L11 seal modified an old output receipt");
    }
    const auto key="compiler-puzzle/v1/"+revised.locale+"/"+revised.id+"/"+
        exercise_key(revised,editor)+"/"+
        state::sha256(reference_objective_frame(revised,editor))+
        "/array-copy-move/v1";
    require(stored.size()==historical.size()+1&&
            stored.contains(key)&&std::get_if<std::string>(&stored.at(key)),
        "U6L11 did not append exactly one independent seal");
    const auto saved=state::get_draft(data,
        curriculum::draft_key("programming",revised,editor),editor.initial_code,
        editor.standard_input.value_or(""));
    require(saved.source==draft.source&&saved.input==draft.input&&
            state::string_value(data,"qa-unrelated","source")=="unchanged",
        "U6L11 damaged a previously saved draft or another course");

    auto tampered=data;
    tampered.preferences["native_windows"][key]=std::string(64,'0');
    const auto before_reject=tampered;
    bool declined=false;
    try {state::validate_lesson_state(tampered,revised);}
    catch(const std::runtime_error&) {declined=true;}
    require(declined&&tampered==before_reject&&
            !compiler_puzzle_completed(tampered,revised,editor),
        "Forged U6L11 copy seal was accepted or changed saved data");
    auto missing=data;
    missing.preferences["native_windows"].erase(key);
    require(!compiler_puzzle_completed(missing,revised,editor),
        "Missing U6L11 proof inherited historical stdout completion");
    store.save(data);
    const auto persisted=reopen(store);
    require(persisted==data&&compiler_puzzle_completed(persisted,revised,editor),
        "Genuine U6L11 independent seal did not survive StateStore reopen");
    state::validate_lesson_state(persisted,revised);
    require(load_lesson(path).raw_json==original.raw_json,
        "U6L11 native QA changed the published curriculum");
    std::cout<<"PASS U6L11 private StateStore: original owner/source and cases, "
        "full/progress loaders, preserved historical stdout, forged/missing "
        "proof rejected, independent seal appended, saved draft retained.\n";
    return 0;
}
} // namespace lgaed::test
