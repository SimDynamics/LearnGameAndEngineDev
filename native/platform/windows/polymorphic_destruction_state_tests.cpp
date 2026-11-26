// Windows StateStore and lesson/progress-loader regression for independent
// polymorphic destruction. All source and stored history stay in isolated QA.
#include "core/cpp_polymorphic_destruction_check.hpp"
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
    require(at.has_value(),"Original U8L5 editor not found");
    return lesson.pages[at->page].blocks[at->block];
}
state::StateData reopen(const state::StateStore& store) {
    state::StateStore independent(store.root().parent_path());
    return independent.load();
}
} // namespace

int polymorphic_destruction_state_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    constexpr std::string_view id="block_0020_0002";
    const auto path=content.parent_path()/
        "cpp_language_memory_oop/composicion_y_herencia_publica/"
        "lesson_destruir_una_derivada_mediante_la_base.json";
    const auto live=load_lesson(path);
    const auto original=lgaed::qa::load_adoption_predecessor(path);
    const auto& before=owner(original,id);
    require(before.type==BlockType::cpp_compiler&&before.output_check&&
            before.output_check->cases.size()==5&&
            !before.polymorphic_destruction_probe,
        "Original U8L5 source or five output cases changed");
    const auto& first_case=before.output_check->cases.front();
    require(!first_case.outputs.empty(),"U8L5 original first-case oracle absent");

    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto declaration=JsonObject::Parse(winrt::hstring{LR"goal({
        "activity":"block_0020_0002",
        "baseClass":"Medida",
        "derivedClass":"MedidaIdentificada",
        "cycleClass":"Ciclo",
        "ownedClass":"Ficha",
        "ownedMember":"ficha_",
        "factoryFunction":"crear_medida",
        "writeMethod":"escribir",
        "numberParameter":"numero",
        "codeParameter":"codigo",
        "cycleParameter":"ciclo",
        "baseAlive":"bases_vivas",
        "derivedAlive":"derivadas_vivas",
        "ownedAlive":"fichas_vivas",
        "baseDestroyed":"bases_destruidas",
        "derivedDestroyed":"derivadas_destruidas",
        "ownedDestroyed":"fichas_destruidas"
    })goal"});
    JsonArray checks;
    checks.Append(declaration);
    root.Insert(L"compilerPolymorphicDestructionChecks",checks);
    const auto revised_source=lgaed::qa::adopted_or_projected_source(live,
        winrt::to_string(root.Stringify()),L"compilerPolymorphicDestructionChecks");
    const auto revised=parse_lesson_json(revised_source,"es");
    const auto progress=parse_lesson_progress_metadata(revised_source,"es");
    const auto& editor=owner(revised,id);
    const auto& compact=owner(progress,id);
    require(editor.polymorphic_destruction_probe&&
            compact.polymorphic_destruction_probe&&
            editor.polymorphic_destruction_probe==
                compact.polymorphic_destruction_probe,
        "U8L5 full/progress readers disagree on the original compiler goal");
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
        "U8L5 root goal changed original source, cases, answers or addresses");

    const auto& policy=*editor.polymorphic_destruction_probe;
    const auto spans=cpp_multi_body_detail::spans(
        policy.editable.original_source,policy.editable.bodies,false);
    require(spans&&spans->size()==3,
        "U8L5 original three bodies were not recognized");
    auto program=before.initial_code;
    const std::array<std::string,3> replacements{{
        "\n    --ciclo_.bases_vivas;\n    ++ciclo_.bases_destruidas;\n",
        "\n    --balance_.derivadas_vivas;\n    ++balance_.derivadas_destruidas;\n",
        "\n    return std::make_unique<MedidaIdentificada>(numero,codigo,ciclo);\n"
    }};
    for(std::size_t i=replacements.size();i!=0;) {
        --i;
        const auto span=spans->at(i);
        program.replace(span.begin,span.end-span.begin,replacements[i]);
    }
    require(cpp_polymorphic_destruction_matches(policy,program)&&
            cpp_polymorphic_destruction_private_driver(policy,program).has_value(),
        "Valid U8L5 destructor/factory bodies failed the independent source gate");

    CppRunRequest request;
    request.request_id=805;
    request.revision=21;
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

    state::StateData data;
    state::prepare_lesson_state(data,original);
    state::Draft draft;
    draft.source=program;
    draft.input=request.stdin_utf8;
    state::put_draft(data,draft_key("programming",original,before),draft);
    data.preferences["qa-unrelated"]["source"]=std::string("unchanged");
    require(record_compiler_puzzle_case(data,original,before,request,observed,
                "Historical U8L5 public output")&&
            compiler_puzzle_completed(data,original,before),
        "Original first-case stdout was not credited");
    const auto historical=data.preferences.at("native_windows");
    const auto snapshot=data;
    state::StateStore store(qa/"polymorphic-destruction-state");
    require(store.load().preferences.empty(),"Private U8L5 QA state was not fresh");
    store.save(data);
    data=reopen(store);
    require(data==snapshot,"Original U8L5 state changed on independent reopen");
    state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);
    require(!compiler_puzzle_completed(data,revised,editor),
        "Legacy stdout incorrectly inherited the new polymorphic proof");

    const auto unchanged=data;
    request.polymorphic_destruction_probe=editor.polymorphic_destruction_probe;
    require(compiler_runtime_request_allowed(editor,request),
        "Valid U8L5 source rejected by the Windows request guard");
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"public stdout only")&&data==unchanged,
        "U8L5 public stdout earned a false private destructor seal");
    observed.polymorphic_destruction_probe_correct=true;
    observed.polymorphic_destruction_request_fingerprint="unrelated";
    require(!record_compiler_puzzle_case(data,revised,editor,request,
                observed,"unrelated private capture")&&data==unchanged,
        "U8L5 forged request fingerprint changed old state");
    observed.polymorphic_destruction_request_fingerprint=
        cpp_polymorphic_destruction_request_fingerprint(request);
    require(!observed.polymorphic_destruction_request_fingerprint.empty()&&
            record_compiler_puzzle_case(data,revised,editor,request,observed,
                "New independent derived-owner witness")&&
            compiler_puzzle_completed(data,revised,editor),
        "U8L5 authentic independent witness did not unlock first case");

    const auto& stored=data.preferences.at("native_windows");
    for(const auto& [key,value]:historical) {
        const auto found=stored.find(key);
        require(found!=stored.end()&&found->second==value,
            "U8L5 witness altered an old output receipt");
    }
    const auto key="compiler-puzzle/v1/"+revised.locale+"/"+revised.id+"/"+
        exercise_key(revised,editor)+"/"+
        state::sha256(reference_objective_frame(revised,editor))+
        "/polymorphic-destruction/v1";
    require(stored.size()==historical.size()+1&&stored.contains(key)&&
            std::get_if<std::string>(&stored.at(key)),
        "U8L5 did not append exactly one independent receipt");
    const auto saved=state::get_draft(data,
        draft_key("programming",revised,editor),editor.initial_code,
        editor.standard_input.value_or(""));
    require(saved.source==draft.source&&saved.input==draft.input&&
            state::string_value(data,"qa-unrelated","source")=="unchanged",
        "U8L5 corrupted a draft or unrelated progress");

    auto tampered=data;
    tampered.preferences["native_windows"][key]=std::string(64,'0');
    const auto before_reject=tampered;
    bool declined=false;
    try {state::validate_lesson_state(tampered,revised);}
    catch(const std::runtime_error&) {declined=true;}
    require(declined&&tampered==before_reject&&
            !compiler_puzzle_completed(tampered,revised,editor),
        "U8L5 forged receipt was accepted or rewrote history");
    auto missing=data;
    missing.preferences["native_windows"].erase(key);
    require(!compiler_puzzle_completed(missing,revised,editor),
        "U8L5 missing private receipt inherited a historical success");
    store.save(data);
    const auto persisted=reopen(store);
    require(persisted==data&&compiler_puzzle_completed(persisted,revised,editor),
        "U8L5 genuine proof did not survive StateStore reopen");
    state::validate_lesson_state(persisted,revised);
    require(load_lesson(path).raw_json==live.raw_json,
        "U8L5 native QA modified Work's curricular JSON");
    std::cout<<"PASS U8L5 source-bound StateStore: original five cases, "
        "full/progress loaders, old stdout untouched, forged evidence rejected, "
        "first case accredited, draft and unrelated data preserved.\n";
    return 0;
}
} // namespace lgaed::test
