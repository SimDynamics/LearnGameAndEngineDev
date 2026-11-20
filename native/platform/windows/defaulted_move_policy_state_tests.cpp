// End-to-end Windows metadata and StateStore compatibility for a new U6L6
// defaulted-move proof. This models the state/receipt boundary only; the real
// GCC/Clang/MSVC probe suite compiles and executes the original C++ separately.
#include "core/cpp_defaulted_move_policy_check.hpp"
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
    require(at.has_value(),"U6L6 stable original owner not found");
    return lesson.pages[at->page].blocks[at->block];
}
state::StateData reopen(const state::StateStore& previous) {
    state::StateStore independent(previous.root().parent_path());
    return independent.load();
}
} // namespace

int defaulted_move_policy_state_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    constexpr std::string_view id="block_0018_0002";
    const auto path=content.parent_path()/
        "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
        "lesson_elegir_copia_y_movimiento_de_una_clase.json";
    const auto original=load_lesson(path);
    const auto& before=owner(original,id);
    require(before.type==BlockType::cpp_compiler&&before.output_check&&
            before.output_check->cases.size()==3&&
            !before.defaulted_move_policy_probe,
        "Unexpected change to the original U6L6 compiler lesson");
    const auto& first_case=before.output_check->cases.front();
    require(!first_case.outputs.empty(),
        "Original U6L6 mandatory first output case disappeared");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto spec=JsonObject::Parse(winrt::hstring{LR"u6l6({
        "activity":"block_0018_0002",
        "className":"Ficha",
        "memberName":"dato_",
        "putMethod":"poner",
        "changeMethod":"cambiar",
        "clearMethod":"vaciar"
    })u6l6"});
    JsonArray declarations;
    declarations.Append(spec);
    root.Insert(L"compilerDefaultedMovePolicyChecks",declarations);
    const auto reviewed=winrt::to_string(root.Stringify());
    const auto revised=parse_lesson_json(reviewed,"es");
    const auto compact=parse_lesson_progress_metadata(reviewed,"es");
    const auto& editor=owner(revised,id);
    const auto& metadata=owner(compact,id);
    require(editor.defaulted_move_policy_probe&&metadata.defaulted_move_policy_probe&&
            editor.defaulted_move_policy_probe==metadata.defaulted_move_policy_probe,
        "Full and progress loaders disagree on the authored U6L6 ownership policy");
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
            compact.state_layout.signatures==original.state_layout.signatures,
        "The optional U6L6 metadata changed original code, answers or state owners");

    const auto public_section=before.initial_code.find("public:\n");
    require(public_section!=std::string::npos,
        "Original class has no public move-policy authoring section");
    auto source=before.initial_code;
    source.insert(public_section+std::string_view("public:\n").size(),R"cpp(
    Ficha() = default;
    Ficha(Ficha&&) = default;
    Ficha& operator=(Ficha&&) = default;
)cpp");
    require(cpp_defaulted_move_policy_matches(
                *editor.defaulted_move_policy_probe,source)&&
            cpp_defaulted_move_policy_probe_source(
                *editor.defaulted_move_policy_probe,source).has_value(),
        "The legal U6L6 source does not generate its independent runtime probe");

    CppRunRequest request;
    request.request_id=613;
    request.revision=18;
    request.source_utf8=source;
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
    // Public output is deliberately modeled for the receipt migration; it is
    // not asserted as proof that the newly generated move is correct.
    state::StateData data;
    state::prepare_lesson_state(data,original);
    state::Draft saved_draft;
    saved_draft.source=source;
    saved_draft.input=request.stdin_utf8;
    const auto draft_id=curriculum::draft_key("programming",original,before);
    state::put_draft(data,draft_id,saved_draft);
    data.preferences["qa-unrelated"]["source"]=std::string("unchanged");
    require(record_compiler_puzzle_case(data,original,before,request,observed,
                "Old stdout-only acceptance")&&
            compiler_puzzle_completed(data,original,before),
        "Could not model the original U6L6 historical output receipt");
    const auto original_receipts=data.preferences.at("native_windows");
    const auto original_snapshot=data;
    state::StateStore store(qa/"defaulted-move-policy-state");
    require(store.load().preferences.empty(),"Private QA storage not initially empty");
    store.save(data);
    data=reopen(store);
    require(data==original_snapshot,"The old receipt changed on durable reopen");
    state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);
    require(!compiler_puzzle_completed(data,revised,editor),
        "An old stdout result incorrectly earned new move ownership credit");

    const auto unchanged=data;
    request.defaulted_move_policy_probe=editor.defaulted_move_policy_probe;
    require(compiler_runtime_request_allowed(editor,request),
        "A legal defaulted-move source was rejected by the runtime gate");
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"missing private proof")&&data==unchanged,
        "Public stdout alone changed the new owner policy state");
    observed.defaulted_move_policy_probe_correct=true;
    observed.defaulted_move_policy_request_fingerprint="unrelated-capture";
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"forged private proof")&&data==unchanged,
        "An unbound private move witness changed the student's progress");
    observed.defaulted_move_policy_request_fingerprint=
        cpp_defaulted_move_policy_request_fingerprint(request);
    require(!observed.defaulted_move_policy_request_fingerprint.empty()&&
            record_compiler_puzzle_case(data,revised,editor,request,
                observed,"Genuine independent move proof")&&
            compiler_puzzle_completed(data,revised,editor),
        "The authenticated additional move-policy receipt was not recorded");
    const auto& updated=data.preferences.at("native_windows");
    for(const auto& [key,value]:original_receipts) {
        const auto iter=updated.find(key);
        require(iter!=updated.end()&&iter->second==value,
            "Defaulted move adoption rewrote an original output receipt");
    }
    const auto key="compiler-puzzle/v1/"+revised.locale+"/"+revised.id+"/"+
        exercise_key(revised,editor)+"/"+
        state::sha256(reference_objective_frame(revised,editor))+
        "/defaulted-move-policy/v1";
    require(updated.size()==original_receipts.size()+1&&
            updated.contains(key)&&std::get_if<std::string>(&updated.at(key)),
        "New move-policy adoption did not append exactly one typed seal");
    const auto restored=state::get_draft(data,
        curriculum::draft_key("programming",revised,editor),editor.initial_code,
        editor.standard_input.value_or(""));
    require(restored.source==saved_draft.source&&
            restored.input==saved_draft.input&&
            state::string_value(data,"qa-unrelated","source")=="unchanged",
        "Move-policy adoption damaged the original student draft or other state");

    auto tampered=data;
    tampered.preferences["native_windows"][key]=std::string(64,'0');
    const auto old_tampered=tampered;
    bool declined=false;
    try {state::validate_lesson_state(tampered,revised);}
    catch(const std::runtime_error&) {declined=true;}
    require(declined&&tampered==old_tampered&&
            !compiler_puzzle_completed(tampered,revised,editor),
        "A forged new policy receipt was imported or changed the historical data");
    auto missing=data;
    missing.preferences["native_windows"].erase(key);
    require(!compiler_puzzle_completed(missing,revised,editor),
        "An absent move-policy seal was silently promoted from old stdout");
    store.save(data);
    const auto persisted=reopen(store);
    require(persisted==data&&compiler_puzzle_completed(persisted,revised,editor),
        "The independent U6L6 owner seal did not survive StateStore reopen");
    state::validate_lesson_state(persisted,revised);
    require(load_lesson(path).raw_json==original.raw_json,
        "Native QA altered the published U6L6 lesson");
    std::cout<<"PASS U6L6 genuine move-policy StateStore: exact original owner, "
        "full/progress loaders, historical stdout unpromoted, forged/missing "
        "witness rejected, new seal appended, drafts and old receipts unchanged.\n";
    return 0;
}
} // namespace lgaed::test
