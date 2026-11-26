// Windows StateStore and lesson-loader QA for the opt-in U6L8 move-invariant policy.
// This models receipt compatibility; the independent C++ compiler probe
// is run separately and never replaced by the modeled public output.
#include "core/cpp_move_assignment_invariant_check.hpp"
#include "core/cpp_member_body_check.hpp"
#include "core/lesson.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/published_adoption_lesson_test_source.hpp"
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
    require(at.has_value(),"U6L8 original owner not found");
    return lesson.pages[at->page].blocks[at->block];
}
state::StateData reopen(const state::StateStore& previous) {
    state::StateStore independent(previous.root().parent_path());
    return independent.load();
}
} // namespace

int move_assignment_invariant_state_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    constexpr std::string_view id="block_0014_0002";
    const auto path=content.parent_path()/
        "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
        "lesson_conservar_invariantes_al_mover_una_clase.json";
    const auto live=load_lesson(path);
    const auto original=lgaed::qa::load_adoption_predecessor(path);
    const auto& before=owner(original,id);
    require(before.type==BlockType::cpp_compiler&&before.output_check&&
            before.output_check->cases.size()==3&&
            !before.move_assignment_invariant_probe,
        "Published U6L8 source or original cases changed unexpectedly");
    const auto& first_case=before.output_check->cases.front();
    require(!first_case.outputs.empty(),"Published U6L8 first case absent");

    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto declaration=JsonObject::Parse(winrt::hstring{LR"move({
        "activity":"block_0014_0002",
        "className":"Muestra",
        "memberName":"dato_",
        "labelMember":"etiqueta_",
        "parameterName":"otra",
        "hasMethod":"hay",
        "valueMethod":"valor",
        "labelMethod":"etiqueta",
        "accessMethod":"acceso",
        "invariantMethod":"coherente",
        "putMethod":"poner",
        "changeMethod":"cambiar",
        "retagMethod":"reetiquetar",
        "clearMethod":"vaciar"
    })move"});
    JsonArray checks;
    checks.Append(declaration);
    root.Insert(L"compilerMoveAssignmentInvariantChecks",checks);
    const auto revised_source=lgaed::qa::adopted_or_projected_source(live,
        winrt::to_string(root.Stringify()),L"compilerMoveAssignmentInvariantChecks");
    const auto revised=parse_lesson_json(revised_source,"es");
    const auto progress=parse_lesson_progress_metadata(revised_source,"es");
    const auto& editor=owner(revised,id);
    const auto& compact=owner(progress,id);
    require(editor.move_assignment_invariant_probe&&
            compact.move_assignment_invariant_probe&&
            editor.move_assignment_invariant_probe==
                compact.move_assignment_invariant_probe,
        "Progress and full loader disagree on U6L8 policy");
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
        "Additional policy changed published U6L8 source, answers or owners");

    constexpr std::string_view signature=
        "Muestra& Muestra::operator=(Muestra&& otra)";
    auto program=before.initial_code;
    const CppMemberBodyCheck edit_check{program,std::string(signature)};
    const auto body=cpp_member_body_span(edit_check);
    require(body.has_value(),"Original U6L8 move body not delimited");
    program.replace(body->begin,body->end-body->begin,R"cpp(
    if (this == &otra) return *this;
    dato_ = std::move(otra.dato_);
    etiqueta_ = std::exchange(otra.etiqueta_, 0);
    return *this;
)cpp");
    require(cpp_member_body_preserves_source(edit_check,program),
        "U6L8 QA changed source outside its move-assignment body");
    require(cpp_move_invariant_matches(
                *editor.move_assignment_invariant_probe,program)&&
            cpp_move_invariant_private_driver(
                *editor.move_assignment_invariant_probe,program).has_value(),
        "An equivalent U6L8 move was rejected before independent execution");

    CppRunRequest request;
    request.request_id=615;
    request.revision=14;
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
                "Original U6L8 stdout-only proof")&&
            compiler_puzzle_completed(data,original,before),
        "Old U6L8 output receipt was not created");
    const auto historical=data.preferences.at("native_windows");
    const auto snapshot=data;
    state::StateStore store(qa/"move-invariant-state");
    require(store.load().preferences.empty(),"U6L8 private QA state was not fresh");
    store.save(data);
    data=reopen(store);
    require(data==snapshot,"Historical U6L8 bytes changed on reopen");
    state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);
    require(!compiler_puzzle_completed(data,revised,editor),
        "Historical stdout silently credited the new move invariants");

    const auto unchanged=data;
    request.move_assignment_invariant_probe=editor.move_assignment_invariant_probe;
    require(compiler_runtime_request_allowed(editor,request),
        "Valid U6L8 code rejected by the live Windows request gate");
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"public stdout only")&&data==unchanged,
        "U6L8 public stdout alone earned the private objective");
    observed.move_assignment_invariant_probe_correct=true;
    observed.move_assignment_invariant_request_fingerprint="unrelated";
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"forged unrelated capture")&&data==unchanged,
        "U6L8 unbound private result altered historical state");
    observed.move_assignment_invariant_request_fingerprint=
        cpp_move_assignment_invariant_request_fingerprint(request);
    require(!observed.move_assignment_invariant_request_fingerprint.empty()&&
            record_compiler_puzzle_case(data,revised,editor,request,observed,
                "Genuine independent move witness")&&
            compiler_puzzle_completed(data,revised,editor),
        "U6L8 bound private witness not credited");
    const auto& stored=data.preferences.at("native_windows");
    for(const auto& [key,value]:historical) {
        const auto found=stored.find(key);
        require(found!=stored.end()&&found->second==value,
            "New U6L8 seal modified an old output receipt");
    }
    const auto key="compiler-puzzle/v1/"+revised.locale+"/"+revised.id+"/"+
        exercise_key(revised,editor)+"/"+
        state::sha256(reference_objective_frame(revised,editor))+
        "/move-invariant/v1";
    require(stored.size()==historical.size()+1&&
            stored.contains(key)&&std::get_if<std::string>(&stored.at(key)),
        "U6L8 did not append exactly one independent seal");
    const auto saved=state::get_draft(data,
        curriculum::draft_key("programming",revised,editor),editor.initial_code,
        editor.standard_input.value_or(""));
    require(saved.source==draft.source&&saved.input==draft.input&&
            state::string_value(data,"qa-unrelated","source")=="unchanged",
        "U6L8 damaged a previously saved draft or another course");

    auto tampered=data;
    tampered.preferences["native_windows"][key]=std::string(64,'0');
    const auto before_reject=tampered;
    bool declined=false;
    try {state::validate_lesson_state(tampered,revised);}
    catch(const std::runtime_error&) {declined=true;}
    require(declined&&tampered==before_reject&&
            !compiler_puzzle_completed(tampered,revised,editor),
        "Forged U6L8 copy seal was accepted or changed saved data");
    auto missing=data;
    missing.preferences["native_windows"].erase(key);
    require(!compiler_puzzle_completed(missing,revised,editor),
        "Missing U6L8 proof inherited historical stdout completion");
    store.save(data);
    const auto persisted=reopen(store);
    require(persisted==data&&compiler_puzzle_completed(persisted,revised,editor),
        "Genuine U6L8 independent seal did not survive StateStore reopen");
    state::validate_lesson_state(persisted,revised);
    require(load_lesson(path).raw_json==live.raw_json,
        "U6L8 native QA changed the published curriculum");
    std::cout<<"PASS U6L8 private StateStore: original owner/source and cases, "
        "full/progress loaders, preserved historical stdout, forged/missing "
        "proof rejected, independent seal appended, saved draft retained.\n";
    return 0;
}
} // namespace lgaed::test
