// Real LessonView controls for the sixteen authored local/input objectives.
// Each isolated profile earns its historical receipt through the BEFORE UI,
// then opens the reviewed model without rewriting either curricular file.
#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "core/profile.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
using curriculum::Block;
using curriculum::BlockType;
using curriculum::Lesson;
using curriculum::StatePosition;

std::string phase="fixture loading";
ULONGLONG started=0;
std::uint64_t request_sequence=400000;

void check(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(phase+": "+std::string(message));
}
void within_budget() {
    check(GetTickCount64()-started<1100000,"The isolated UI scenario exceeded its global time bound");
}
std::string bytes(const std::filesystem::path& path) {
    const auto value=state::read_bytes(path);
    return std::string(value.begin(),value.end());
}
std::string json_string(const JsonObject& object,std::wstring_view key) {
    return winrt::to_string(object.GetNamedString(winrt::hstring(key)));
}
std::string canonical(std::string_view raw) {
    return winrt::to_string(JsonObject::Parse(wide(raw)).Stringify());
}
std::string normalized(std::string_view value) {
    return curriculum::cpp_local_int_canonical_stdout(value);
}
StatePosition position(const Lesson& lesson,std::string_view identity) {
    const auto found=curriculum::stable_block_position(lesson,identity);
    check(found.has_value(),"A manifest identity is absent from its authenticated lesson");
    return *found;
}
const Block& block_at(const Lesson& lesson,std::string_view identity) {
    const auto found=position(lesson,identity);
    return lesson.pages.at(found.page).blocks.at(found.block);
}
std::size_t private_contracts(const Block& block) {
    return static_cast<std::size_t>(block.local_int_check.has_value())+
        static_cast<std::size_t>(block.local_string_check.has_value())+
        static_cast<std::size_t>(block.input_check.has_value())+
        static_cast<std::size_t>(block.local_role_check.has_value());
}

struct Solution { std::string activity,source; };
struct InputCounterexample {
    std::string label,source;
    std::set<std::string> failed_inputs;
};
struct GuardedArithmeticSource {
    std::string label,source;
    bool accepted=false;
    std::optional<std::uint32_t> guard_exit;
};
struct JointCounterexampleSource {
    std::string label,source;
    std::size_t failed_probe=0;
};
struct Plan {
    std::string activity,reference,fixed_source;
    std::optional<std::string> no_plan_source;
    std::vector<InputCounterexample> input_counterexamples;
    std::vector<GuardedArithmeticSource> arithmetic_sources{};
    std::vector<JointCounterexampleSource> joint_counterexamples{};
};
struct Fixture {
    std::string id;
    std::filesystem::path before_path,after_path;
    std::string before_sha256,after_sha256;
    Lesson before,after;
    std::vector<Solution> solutions;
    std::vector<Plan> plans;
    bool joint_local_int=false;
    bool input_decisions=false;
    bool active_loops=false;
    bool guarded_arithmetic=false;
};
const std::string& solution_for(const Fixture& fixture,std::string_view identity) {
    const auto found=std::ranges::find(fixture.solutions,identity,&Solution::activity);
    check(found!=fixture.solutions.end(),"A nonfocal compiler lacks an audited real solution");
    return found->source;
}
std::filesystem::path fixture_path(const std::filesystem::path& root,std::string_view relative) {
    const auto path=std::filesystem::path(relative);
    check(!path.empty()&&!path.is_absolute(),"The fixture manifest needs a relative file path");
    for(const auto& component:path)check(component!=L"..","The fixture path leaves its export directory");
    return root/path;
}
void verify_only_root_adoption(const Fixture& fixture) {
    auto normalized_after=JsonObject::Parse(wide(fixture.after.raw_json));
    const auto original=JsonObject::Parse(wide(fixture.before.raw_json));
    std::set<std::string> owners;
    for(const auto& plan:fixture.plans)check(owners.insert(plan.activity).second,"Repeated focal owner");
    std::size_t removed=0;
    for(const auto key:{L"compilerLocalIntChecks",L"compilerLocalStringChecks",
                       L"compilerInputChecks",L"compilerLocalRoleChecks"}) {
        if(!normalized_after.HasKey(key))continue;
        const auto declarations=normalized_after.GetNamedArray(key);
        for(std::uint32_t index=declarations.Size();index>0;--index) {
            if(owners.contains(json_string(declarations.GetObjectAt(index-1),L"activity"))) {
                declarations.RemoveAt(index-1);++removed;
            }
        }
        if(declarations.Size()==0&&!original.HasKey(key))normalized_after.Remove(key);
    }
    check(removed==fixture.plans.size()&&
            winrt::to_string(normalized_after.Stringify())==canonical(fixture.before.raw_json),
        "The reviewed fixture changed something other than its declared private owner roots");
    check(fixture.before.compiler_puzzles_required&&fixture.after.compiler_puzzles_required,
        "UI QA changed the published required-compiler policy");
    for(const auto& plan:fixture.plans) {
        const auto& former=block_at(fixture.before,plan.activity);
        const auto& current=block_at(fixture.after,plan.activity);
        const auto& former_reference=block_at(fixture.before,plan.reference);
        const auto& current_reference=block_at(fixture.after,plan.reference);
        check(curriculum::reference_activity(fixture.before,former_reference)==
                std::optional(position(fixture.before,plan.activity))&&
                curriculum::reference_activity(fixture.after,current_reference)==
                std::optional(position(fixture.after,plan.activity)),
            "The manifest reference is not authored for this focal owner");
        check(former.type==BlockType::cpp_compiler&&current.type==BlockType::cpp_compiler&&
                former.output_check&&current.output_check&&!current.output_check->cases.empty()&&
                private_contracts(former)==0&&private_contracts(current)==1&&
                former.address.page==current.address.page&&former.address.block==current.address.block&&
                canonical(former.raw_json)==canonical(current.raw_json)&&
                curriculum::draft_key("programming",fixture.before,former)==
                    curriculum::draft_key("programming",fixture.after,current)&&
                solution_for(fixture,plan.activity)!=plan.fixed_source,
            "A focal owner moved, changed public payload or already had its new private check");
    }
    std::set<std::string> compiler_ids;
    for(const auto& page:fixture.after.pages)for(const auto& block:page.blocks)
        if(block.type==BlockType::cpp_compiler)compiler_ids.insert(block.stable_id);
    check(compiler_ids.size()==fixture.solutions.size(),"The fixture does not enumerate every actual compiler");
    for(const auto& solution:fixture.solutions)
        check(compiler_ids.erase(solution.activity)==1,"Repeated or unrelated prerequisite solution");
    check(compiler_ids.empty(),"An authentic compiler lacks its solution");
}
struct JointReplacementOracle { std::string_view variable;std::int32_t replacement; };
struct JointProbeOracle {
    std::string_view variable;
    std::int32_t replacement;
    std::vector<JointReplacementOracle> additional;
    std::string_view expected;
};
struct JointCounterexampleOracle {
    std::string_view label,sha256;
    std::size_t failed_probe;
    std::string_view negative_output;
};
struct JointPublication {
    std::string_view id,activity,reference,other_activity,other_reference;
    std::string_view publication_sha256,published_commit;
    std::size_t original_probes;
    std::vector<JointProbeOracle> additions;
    std::vector<JointCounterexampleOracle> counterexamples;
    std::string_view primary_counterexample;
};
const JointPublication& joint_publication(std::string_view id) {
    static const std::array<JointPublication,3> publications{{
        {"u3l7","p1u3l7_reparar_prioridad","p1u3l7_ejemplo_prioridad",
         "p1u3l7_construir_despacho","p1u3l7_ejemplo_despacho",
         "132e947149b4388fcb0881792dc4054e2592f1a15c64a5c0f491d0f570ad541f",
         "a92895f2b00242dbd338ab0a386ea8ceeda9edf8",3,
         {{"solicitadas",6,{{"codigo_permiso",1}},"Pendiente: 2"}},
         {{"missing_pending","1c58b10a3c7a7f1a4f30f2b9e47d5efdb348bdd4c5f8a85c4b9117121056e25a",3,"Completo"}},
         "missing_pending"},
        {"u3l8","block_0002_0002","p1u3l8_ejemplo_original",
         "p1u3l8_cambiar_minimo","p1u3l8_ejemplo_minimo",
         "faf26c9f5443a650c06fd5b73c882de3ba7cf03885cb0758202cd58257ec98a1",
         "a92895f2b00242dbd338ab0a386ea8ceeda9edf8",8,
         {{"fichas",0,{{"capacidad",0}},"Datos invalidos"}},
         {{"zero_before_invalid","488bd22b229fd020fdfd307d13c5d08f719f27faed1ab9a319a5faa809b3f4b8",8,"Sin trabajo"}},
         "zero_before_invalid"},
        {"u3l5","p1u3l5_reparar_parentesis","p1u3l5_ejemplo_reparacion",
         "p1u3l5_construir_acceso","p1u3l5_ejemplo_acceso",
         "d5d696e9f88ccdaa9b997d00bcd93c677f8e6af861eaef82e9409d3374725c67",
         "08a11877a2d2c0bf96a846660e3baea69e53f758",3,
         {{"pase",0,{{"invitacion",1},{"apertura",1}},"Entrada permitida\nDatos: 0 1 1"},
          {"pase",0,{{"apertura",1}},"Entrada denegada\nDatos: 0 0 1"}},
         {{"omits_invitation","f7496334ce93aa0daf943cd962a11659b50df77f3d231aea7e3742fc9ae8ae1a",3,"Entrada denegada\nDatos: 0 1 1"},
          {"open_only","66951ed1a36227f4723585864efb290d2c35a8576db57d124bc3f05a97f7b8f1",4,"Entrada permitida\nDatos: 0 0 1"}},
         "open_only"},
    }};
    const auto found=std::ranges::find(publications,id,&JointPublication::id);
    check(found!=publications.end(),"Unknown joint-variable publication");
    return *found;
}
const JointCounterexampleOracle& joint_counterexample_oracle(const JointPublication& publication,
        std::string_view label) {
    const auto found=std::ranges::find(publication.counterexamples,label,&JointCounterexampleOracle::label);
    check(found!=publication.counterexamples.end(),"Unknown joint-variable counterexample");
    return *found;
}
void read_joint_plan(const Fixture& fixture,const JsonObject& entry,Plan& plan) {
    const auto& pin=joint_publication(fixture.id);
    if(fixture.id=="u3l5") {
        check(entry.GetNamedNumber(L"originalProbeCount")==static_cast<double>(pin.original_probes),
            "The admission plan changed its three historical observations");
        std::set<std::string> labels;
        for(const auto& raw:entry.GetNamedArray(L"counterexamples")) {
            const auto value=raw.GetObject();
            JointCounterexampleSource example{json_string(value,L"label"),json_string(value,L"source")};
            const auto& expected=joint_counterexample_oracle(pin,example.label);
            check(labels.insert(example.label).second,"Repeated admission counterexample");
            example.failed_probe=expected.failed_probe;
            check(value.GetNamedNumber(L"failedProbeIndex")==static_cast<double>(expected.failed_probe)&&
                    state::sha256(example.source)==expected.sha256&&
                    json_string(value,L"sourceSha256")==expected.sha256,
                "An admission counterexample changed its independently pinned source or failure index");
            plan.joint_counterexamples.push_back(std::move(example));
        }
    } else {
        const auto& expected=joint_counterexample_oracle(pin,pin.primary_counterexample);
        plan.joint_counterexamples.push_back({std::string(expected.label),plan.fixed_source,expected.failed_probe});
    }
    check(plan.joint_counterexamples.size()==pin.counterexamples.size(),
        "The joint plan lost a required independent counterexample");
    for(const auto& example:plan.joint_counterexamples) {
        const auto& expected=joint_counterexample_oracle(pin,example.label);
        check(state::sha256(example.source)==expected.sha256&&
                (example.label!=pin.primary_counterexample||example.source==plan.fixed_source),
            "The joint UI source differs from its independently pinned primary counterexample");
    }
}
void verify_only_joint_append(const Fixture& fixture) {
    const auto& pin=joint_publication(fixture.id);
    check(fixture.before_sha256==pin.publication_sha256&&
            state::sha256(fixture.before.raw_json)==pin.publication_sha256&&
            fixture.plans.size()==1&&fixture.solutions.size()==2,
        "Joint-variable QA lost its independent complete-publication pin or owner census");
    const auto& plan=fixture.plans.front();
    check(plan.activity==pin.activity&&plan.reference==pin.reference&&!plan.no_plan_source&&
            state::sha256(plan.fixed_source)==joint_counterexample_oracle(pin,pin.primary_counterexample).sha256,
        "The joint-variable counterexample or owner differs from the independently pinned scenario");
    const auto& old=block_at(fixture.before,pin.activity);
    const auto& current=block_at(fixture.after,pin.activity);
    const auto before_metadata=windows::parse_lesson_progress_metadata(fixture.before.raw_json,"es");
    const auto after_metadata=windows::parse_lesson_progress_metadata(fixture.after.raw_json,"es");
    check(old.local_int_check&&current.local_int_check&&
            private_contracts(old)==1&&private_contracts(current)==1&&
            old.local_int_check->probes.size()==pin.original_probes&&
            current.local_int_check->probes.size()==pin.original_probes+pin.additions.size()&&
            std::equal(old.local_int_check->probes.begin(),old.local_int_check->probes.end(),
                       current.local_int_check->probes.begin())&&
            std::ranges::all_of(old.local_int_check->probes,
                [](const auto& probe){return probe.additional_replacements.empty();})&&
            block_at(before_metadata,pin.activity).local_int_check==old.local_int_check&&
            block_at(after_metadata,pin.activity).local_int_check==current.local_int_check,
        "Both loaders must preserve every old probe and append exactly the independent joint observations");
    for(std::size_t index=0;index<pin.additions.size();++index) {
        const auto& added=current.local_int_check->probes.at(pin.original_probes+index);
        const auto& expected=pin.additions[index];
        check(added.variable==expected.variable&&added.replacement==expected.replacement&&
                added.value_type==curriculum::CppLocalValueType::integer&&!added.after_update&&
                !added.expected_increase&&!added.forbid_arithmetic&&
                added.additional_replacements.size()==expected.additional.size()&&
                added.expected_stdout==std::vector<std::string>{std::string(expected.expected),std::string(expected.expected)+'\n'},
            "An appended joint observation differs from its independent oracle");
        for(std::size_t item=0;item<expected.additional.size();++item)
            check(added.additional_replacements[item].variable==expected.additional[item].variable&&
                    added.additional_replacements[item].replacement==expected.additional[item].replacement,
                "A jointly replaced variable or its value differs from its independent oracle");
    }
    auto restored=JsonObject::Parse(wide(fixture.after.raw_json));
    std::size_t found_owner=0;
    for(const auto& value:restored.GetNamedArray(L"compilerLocalIntChecks")) {
        const auto entry=value.GetObject();
        if(json_string(entry,L"activity")!=pin.activity)continue;
        auto probes=entry.GetNamedArray(L"probes");
        check(probes.Size()==pin.original_probes+pin.additions.size(),"The JSON probe suffix differs from the parsed contract");
        for(std::size_t index=0;index<pin.additions.size();++index)probes.RemoveAt(probes.Size()-1);
        ++found_owner;
    }
    check(found_owner==1&&winrt::to_string(restored.Stringify())==canonical(fixture.before.raw_json)&&
            fixture.before.compiler_puzzles_required&&fixture.after.compiler_puzzles_required&&
            old.output_check&&current.output_check&&old.output_check->cases.size()==1&&
            current.output_check->cases.size()==1&&current.output_check->cases.front().standard_input.empty(),
        "The joint fixture changed an old field, public case, archive or required-compiler policy");
    for(const auto& [activity,reference]:std::array{
            std::pair{pin.activity,pin.reference},std::pair{pin.other_activity,pin.other_reference}}) {
        const auto& before=block_at(fixture.before,activity);
        const auto& after=block_at(fixture.after,activity);
        const auto& projection=block_at(after_metadata,activity);
        check(before.type==BlockType::cpp_compiler&&after.type==BlockType::cpp_compiler&&
                canonical(before.raw_json)==canonical(after.raw_json)&&
                before.address.page==after.address.page&&before.address.block==after.address.block&&
                after.address.page==projection.address.page&&after.address.block==projection.address.block&&
                after.initial_code==projection.initial_code&&after.standard_input==projection.standard_input&&
                curriculum::draft_key("programming",fixture.before,before)==
                    curriculum::draft_key("programming",fixture.after,after)&&
                solution_for(fixture,activity)==block_at(fixture.before,reference).code&&
                curriculum::reference_activity(fixture.after,block_at(fixture.after,reference))==
                    std::optional(position(fixture.after,activity)),
            "The joint fixture changed a real compiler, source, stable address, draft or authored reference");
    }
}
struct InputDecisionPublication {
    std::string_view id,activity,reference,sha256,public_cases_sha256;
    std::size_t public_cases,private_probes,same_output_probes,counterexamples;
    std::array<std::size_t,8> private_indices;
};
const InputDecisionPublication& input_decision_publication(std::string_view id, bool active_loops=false) {
    if(active_loops) {
        static constexpr std::array<InputDecisionPublication,2> active{{
            {"u4l1","block_0006_0002","block_0006_0006",
             "b03928e0df3ee47371acb6b9c27560986d0be3b9b000d28ec9aa89494b65202b",
             "aa62f480ddcb8ced4b0c6377a99e0ac0451d4fe91b5ec6b413b53cd038aed00d",4,3,0,0,{1,2,3}},
            {"u4l2","block_0006_0005","block_0007_0002",
             "1df1821c5413cb148af787147b55b81a8580d567e7d71cf3ebbadcc8c2a219b9",
             "70a0adf8ba5e11cc67d993e8608a9921c4674f31bab6a16f9ed42879a97e79bf",4,3,0,0,{1,2,3}},
        }};
        const auto found=std::ranges::find(active,id,&InputDecisionPublication::id);
        check(found!=active.end(),"Unknown active loop publication");
        return *found;
    }
    static constexpr std::array<InputDecisionPublication,8> publications{{
        {"u3l3","p1u3l3_construir_limite_peso","p1u3l3_ejemplo_limite_peso",
         "9c43940696d0fda803f49378e93250d0746c5009bde1050d119e37487c9ab308",
         "2792a215ba646efdcb567b9491da7a8492df78b5947caa25b5a9b8e462b72830",4,3,0,0,{1,2,3}},
        {"u3l4","p1u3l4_construir_alerta","p1u3l4_ejemplo_lectura",
         "b1c1a2400861c1ed7348de8caeb15b49fb30cfe07341bf82839ec898ea699f70",
         "eb0a16c2709de2582fa8977bac6088472dcff31ec2c3b540cea8eb32b07e6498",4,3,0,0,{1,2,3}},
        {"u3l5","p1u3l5_construir_acceso","p1u3l5_ejemplo_acceso",
         "d5d696e9f88ccdaa9b997d00bcd93c677f8e6af861eaef82e9409d3374725c67",
         "469baf3bed39fcd61f54569eb2133a629a0db81a3559e31ac1c8a0caca79e651",8,7,4,2,{1,2,3,4,5,6,7}},
        {"u3l6","p1u3l6_construir_reparto","p1u3l6_ejemplo_reparto",
         "9d7ca6b5d82b2dac1809ab01b8b308f9e81027c4c4c6ce796502b42cda7ca8ea",
         "8c022a85b982a5ae964d222262b28d93f0eff6f2ce85d96aafc3307e30e73863",7,6,1,1,{1,2,3,4,5,6}},
        {"u3l7","p1u3l7_construir_despacho","p1u3l7_ejemplo_despacho",
         "adc084181b89193086d4c3e121587738d93f9d644a53841ddde31333097b7342",
         "4f6a28945c17dd6f58ff2c7cf8d1bae17e5280073d19dc86aa1fba3e636c754a",9,8,2,2,{1,2,3,4,5,6,7,8}},
        {"u3l8","p1u3l8_cambiar_minimo","p1u3l8_ejemplo_minimo",
         "2d0756fe639c3591e8db77ffe3a085a7a7ff8a49e24e0175767f6df5d4b1640c",
         "8cce475973ba0bbc20664c6542354f968317087bc1c5e9da6ddd7b78df095c3c",13,7,0,0,{1,2,3,4,5,11,12}},
        {"u4l1","p1u4l1_construir_recorrido","p1u4l1_ejemplo_recorrido",
         "f9538d2daa7c65ef5e94c58e5527fb75f4bd0855fb057e02de2e3cfa173a961b",
         "aa62f480ddcb8ced4b0c6377a99e0ac0451d4fe91b5ec6b413b53cd038aed00d",4,3,0,0,{1,2,3}},
        {"u4l2","p1u4l2_construir_descendente","p1u4l2_ejemplo_descendente",
         "435a7f8f740f41e1696e3e5807fd7e4fc5fc03a814943b0b8257c4d6b2b76074",
         "70a0adf8ba5e11cc67d993e8608a9921c4674f31bab6a16f9ed42879a97e79bf",4,3,0,0,{1,2,3}},
    }};
    const auto found=std::ranges::find(publications,id,&InputDecisionPublication::id);
    check(found!=publications.end(),"Unknown input-decision publication");
    return *found;
}
struct InputCounterexamplePin {
    std::string_view lesson,label,sha256;
    std::array<std::string_view,2> failed_inputs;
};
const InputCounterexamplePin& input_counterexample_pin(std::string_view lesson,std::string_view label) {
    static constexpr std::array<InputCounterexamplePin,5> pins{{
        {"u3l5","open_only","08617cb383f53753e637affad2ab7109d3625edd576c953c083dbe7860af0c67",{"0 0 1"}},
        {"u3l5","invitation_ignores_closure","1dbdbfa204c1a982a6da4c11424d61742340f4dbc679d12667f3c3361ca7cf9c",{"0 1 0","1 1 0"}},
        {"u3l6","nonzero_instead_of_positive","71c3cf883440d0a83aa6a0c3f781a64eba22a7d6ef9acec6cbef237260c17c30",{"28 -1"}},
        {"u3l7","no_upper_bound","51f2ddd82a9e9ac603678b4ad301d7f928a05f8de377e9cb5bc07d0e0fa1f481",{"13 1"}},
        {"u3l7","zero_allowed_with_permission","ab162394020c8eef4bdcffc6a9ee0dbab8db87ed6d7067f028204c15215722f3",{"0 1"}},
    }};
    const auto found=std::ranges::find_if(pins,[&](const auto& pin){return pin.lesson==lesson&&pin.label==label;});
    check(found!=pins.end(),"Unknown input-decision invariant counterexample");
    return *found;
}
bool shares_first_output(const curriculum::CppInputProbe& probe,const curriculum::OutputCase& first) {
    return std::ranges::any_of(probe.outputs,[&](const auto& output) {
        return std::ranges::any_of(first.outputs,[&](const auto& expected) {
            return curriculum::cpp_input_normalize_newlines(output)==curriculum::cpp_input_normalize_newlines(expected);
        });
    });
}
void read_input_decision_plan(const Fixture& fixture,const JsonObject& entry,Plan& plan) {
    const auto& pin=input_decision_publication(fixture.id,fixture.active_loops);
    const auto& owner=block_at(fixture.before,plan.activity);
    const auto public_cases=entry.GetNamedArray(L"publicCases");
    // This pin hashes the author's deterministic JSON export. The following
    // comparison also checks every value against the authenticated lesson,
    // without depending on WinRT's object ordering or whitespace serializer.
    check(owner.output_check&&owner.output_check->input_comparison==curriculum::TextComparison::tokens&&
            json_string(entry,L"inputComparison")=="tokens"&&public_cases.Size()==pin.public_cases&&
            owner.output_check->cases.size()==pin.public_cases&&
            json_string(entry,L"publicCasesSha256")==pin.public_cases_sha256,
        "The input-decision manifest changed the token comparison or public case census");
    for(std::uint32_t index=0;index<public_cases.Size();++index) {
        const auto item=public_cases.GetObjectAt(index);
        std::vector<std::string> outputs;
        for(const auto& value:item.GetNamedArray(L"outputs"))outputs.push_back(winrt::to_string(value.GetString()));
        const auto& authored=owner.output_check->cases[index];
        check(json_string(item,L"standardInput")==authored.standard_input&&outputs==authored.outputs,
            "The input-decision manifest changed an authentic public input, output or order");
    }
    std::set<std::string> labels;
    for(const auto& raw:entry.GetNamedArray(L"invariantCounterexamples")) {
        const auto item=raw.GetObject();InputCounterexample example;
        example.label=json_string(item,L"label");example.source=json_string(item,L"source");
        const auto& expected=input_counterexample_pin(fixture.id,example.label);
        check(labels.insert(example.label).second&&state::sha256(example.source)==expected.sha256&&
                json_string(item,L"sourceSha256")==expected.sha256,
            "An invariant counterexample differs from its independent source pin");
        for(const auto& value:item.GetNamedArray(L"failedInputs"))
            check(example.failed_inputs.insert(winrt::to_string(value.GetString())).second,
                "A counterexample repeats its predicted failing input");
        std::set<std::string> failures;
        for(const auto value:expected.failed_inputs)if(!value.empty())failures.emplace(value);
        check(example.failed_inputs==failures,"An invariant counterexample changed its independently predicted failures");
        plan.input_counterexamples.push_back(std::move(example));
    }
    check(plan.input_counterexamples.size()==pin.counterexamples,
        "The input-decision manifest omitted an independently falsifiable invariant");
}
void verify_input_decision_adoption(const Fixture& fixture) {
    const auto& pin=input_decision_publication(fixture.id,fixture.active_loops);
    check(fixture.before_sha256==pin.sha256&&state::sha256(fixture.before.raw_json)==pin.sha256&&
            fixture.plans.size()==1&&fixture.solutions.size()==2,
        "Input-decision QA lost its complete publication pin or two authentic editors");
    verify_only_root_adoption(fixture);
    const auto& plan=fixture.plans.front();
    const auto& owner=block_at(fixture.after,plan.activity);
    const auto before_metadata=windows::parse_lesson_progress_metadata(fixture.before.raw_json,"es");
    const auto after_metadata=windows::parse_lesson_progress_metadata(fixture.after.raw_json,"es");
    check(plan.activity==pin.activity&&plan.reference==pin.reference&&!plan.no_plan_source&&
            owner.input_check&&owner.input_check->probes.size()==pin.private_probes&&
            owner.input_check->allow_same_output==(pin.same_output_probes>0)&&
            !block_at(before_metadata,pin.activity).input_check&&
            block_at(after_metadata,pin.activity).input_check==owner.input_check&&
            solution_for(fixture,plan.activity)==block_at(fixture.before,pin.reference).code,
        "Both loaders must preserve the input owner/reference and adopt only its exact opt-in plan");
    std::size_t same=0;
    for(std::size_t index=0;index<pin.private_probes;++index) {
        const auto& probe=owner.input_check->probes[index];
        const auto& expected=owner.output_check->cases.at(pin.private_indices[index]);
        check(probe.standard_input==expected.standard_input&&probe.outputs==expected.outputs,
            "A private input-decision observation differs from its selected authentic public case");
        same+=shares_first_output(probe,owner.output_check->cases.front());
    }
    check(same==pin.same_output_probes&&same<pin.private_probes,
        "Input-decision QA lost an invariant or its required distinct-output contrast");
    for(const auto& solution:fixture.solutions) {
        const auto& original=block_at(fixture.before,solution.activity);
        const auto& current=block_at(fixture.after,solution.activity);
        const auto& metadata=block_at(after_metadata,solution.activity);
        const auto relation=std::ranges::find(fixture.before.reference_after,solution.activity,&curriculum::ReferenceAfter::activity);
        check(relation!=fixture.before.reference_after.end()&&
                std::ranges::any_of(relation->blocks,[&](const auto& identity) {
                    const auto& reference=block_at(fixture.before,identity);
                    return reference.type==BlockType::code&&reference.language=="cpp"&&reference.code==solution.source;
                })&&current.address.page==metadata.address.page&&current.address.block==metadata.address.block&&
                original.local_int_check==current.local_int_check&&current.local_int_check==metadata.local_int_check&&
                current.input_check==metadata.input_check&&current.initial_code==metadata.initial_code&&
                current.standard_input==metadata.standard_input,
            "Input-decision QA changed an authentic solution, local repair, address or progress-loader contract");
    }
}
void verify_only_guarded_append(const Fixture& fixture) {
    constexpr std::string_view identity="p1u3l6_reparar_resto";
    constexpr std::string_view reference="p1u3l6_ejemplo_resto";
    constexpr std::string_view digest="9d7ca6b5d82b2dac1809ab01b8b308f9e81027c4c4c6ce796502b42cda7ca8ea";
    check(fixture.id=="u3l6"&&fixture.before_sha256==digest&&
            state::sha256(fixture.before.raw_json)==digest&&fixture.plans.size()==1&&
            fixture.solutions.size()==2,"Arithmetic QA lost its authentic U3L6 publication or editor census");
    const auto& plan=fixture.plans.front();
    check(plan.activity==identity&&plan.reference==reference&&!plan.no_plan_source&&
            plan.arithmetic_sources.size()==10,"Arithmetic QA changed its exact owner or source matrix");
    const auto& old=block_at(fixture.before,identity);
    const auto& current=block_at(fixture.after,identity);
    const auto before_metadata=windows::parse_lesson_progress_metadata(fixture.before.raw_json,"es");
    const auto after_metadata=windows::parse_lesson_progress_metadata(fixture.after.raw_json,"es");
    check(old.local_int_check&&current.local_int_check&&private_contracts(old)==1&&
            private_contracts(current)==1&&old.local_int_check->probes.size()==3&&
            current.local_int_check->probes.size()==5&&
            std::equal(old.local_int_check->probes.begin(),old.local_int_check->probes.end(),
                current.local_int_check->probes.begin())&&
            block_at(before_metadata,identity).local_int_check==old.local_int_check&&
            block_at(after_metadata,identity).local_int_check==current.local_int_check,
        "Both loaders must preserve the three original probes and append the same arithmetic witnesses");
    for(std::size_t index=0;index<5;++index) {
        const auto& probe=current.local_int_check->probes[index];
        check(probe.forbid_arithmetic==(index>=3),"The arithmetic policy changed a published observation");
        if(index<3)continue;
        check(probe.variable=="capacidad"&&probe.replacement==(index==3?0:-2)&&
                probe.value_type==curriculum::CppLocalValueType::integer&&!probe.after_update&&
                !probe.expected_increase&&probe.additional_replacements.empty()&&
                probe.expected_stdout==std::vector<std::string>{"0\nCapacidad invalida","0\nCapacidad invalida\n"},
            "A safe omission scenario differs from the independently specified invalid-capacity oracle");
    }
    auto restored=JsonObject::Parse(wide(fixture.after.raw_json));
    std::size_t changed=0;
    for(const auto& raw:restored.GetNamedArray(L"compilerLocalIntChecks")) {
        const auto entry=raw.GetObject();
        if(json_string(entry,L"activity")!=identity)continue;
        auto probes=entry.GetNamedArray(L"probes");
        check(probes.Size()==5,"The raw arithmetic suffix differs from the parsed model");
        probes.RemoveAtEnd();probes.RemoveAtEnd();++changed;
    }
    check(changed==1&&winrt::to_string(restored.Stringify())==canonical(fixture.before.raw_json)&&
            fixture.before.compiler_puzzles_required&&fixture.after.compiler_puzzles_required&&
            old.output_check&&current.output_check&&old.output_check->cases.size()==1&&
            current.output_check->cases.size()==1&&current.output_check->cases.front().standard_input.empty(),
        "The arithmetic fixture altered a public case, prior field, archive or required-editor policy");
    for(const auto& [activity,example]:std::array{
            std::pair{identity,reference},
            std::pair{std::string_view("p1u3l6_construir_reparto"),std::string_view("p1u3l6_ejemplo_reparto")}}) {
        const auto& before=block_at(fixture.before,activity);
        const auto& after=block_at(fixture.after,activity);
        const auto& projection=block_at(after_metadata,activity);
        check(canonical(before.raw_json)==canonical(after.raw_json)&&
                before.address.page==after.address.page&&before.address.block==after.address.block&&
                after.initial_code==projection.initial_code&&after.standard_input==projection.standard_input&&
                after.local_int_check==projection.local_int_check&&
                curriculum::draft_key("programming",fixture.before,before)==
                    curriculum::draft_key("programming",fixture.after,after)&&
                solution_for(fixture,activity)==block_at(fixture.before,example).code&&
                curriculum::reference_activity(fixture.after,block_at(fixture.after,example))==
                    std::optional(position(fixture.after,activity)),
            "The arithmetic fixture changed an authentic source, draft address, reference or progress projection");
    }
    auto reversed=solution_for(fixture,identity);
    const std::string from="(capacidad > 0) && (total % capacidad == 0)";
    const auto at=reversed.find(from);
    check(at!=std::string::npos&&reversed.find(from,at+from.size())==std::string::npos,
        "The pinned reference lost its unique short-circuit expression");
    reversed.replace(at,from.size(),"(total % capacidad == 0) && (capacidad > 0)");
    check(plan.fixed_source==reversed,"The UI counterexample no longer inverts the actual authored condition");
}
void read_guarded_arithmetic_plan(const Fixture& fixture,const JsonObject& entry,Plan& plan) {
    check(entry.GetNamedNumber(L"originalProbeCount")==3,"Arithmetic QA changed the original observation count");
    const std::map<std::string,bool> expected{{"reference",true},{"initial",false},
        {"reversed_and",false},{"eager_and",false},{"precomputed",false},
        {"guarded_if",true},{"guarded_ternary",true},{"negated_or",true},{"keyword_and",true},
        {"primitive_casts",true}};
    std::set<std::string> seen;
    for(const auto& value:entry.GetNamedArray(L"sources")) {
        const auto item=value.GetObject();
        GuardedArithmeticSource source{json_string(item,L"label"),json_string(item,L"source"),
            item.GetNamedBoolean(L"accepted"),{}};
        check(expected.contains(source.label)&&seen.insert(source.label).second&&
                source.accepted==expected.at(source.label)&&!source.source.empty()&&
                state::sha256(source.source)==json_string(item,L"sourceSha256"),
            "An arithmetic example changed its label, expected verdict or source bytes");
        const bool traps=source.label=="reversed_and"||source.label=="eager_and"||source.label=="precomputed";
        check(item.HasKey(L"guardExit")==traps,"Arithmetic rejection lacks its exact safe-stop oracle");
        if(traps) {
            check(item.GetNamedNumber(L"guardExit")==91,"A safe arithmetic stop is not exit 91");
            source.guard_exit=91;
        }
        if(source.label=="reference")check(source.source==solution_for(fixture,plan.activity),
            "The positive arithmetic reference differs from the authentic publication");
        if(source.label=="initial")check(source.source==block_at(fixture.before,plan.activity).initial_code,
            "The initial arithmetic failure is not the real learner source");
        if(source.label=="reversed_and")check(source.source==plan.fixed_source,
            "The UI and direct arithmetic counterexamples diverge");
        plan.arithmetic_sources.push_back(std::move(source));
    }
    check(seen.size()==expected.size(),"The arithmetic source matrix is incomplete");
}
std::vector<Fixture> load_manifest(const std::filesystem::path& manifest,bool joint=false,bool input_decisions=false,
        bool guarded_arithmetic=false, bool active_loops=false) {
    windows::ensure_winrt_apartment();
    const auto document=JsonObject::Parse(wide(bytes(manifest)));
    check(document.GetNamedNumber(L"version")==1&&
            json_string(document,L"publishedCommit")==
                (active_loops?"2fcfad809e6a854dc35f4ce6c19fc1f1ab3e9c5b":
                     (input_decisions||guarded_arithmetic)?"08a11877a2d2c0bf96a846660e3baea69e53f758":
                    joint?"a92895f2b00242dbd338ab0a386ea8ceeda9edf8":"2ca32b2337178d1effa3a2c81759e2493cd746d0")&&
            (!joint||json_string(document,L"kind")=="joint-local-int")&&
            (!input_decisions||(!joint&&!guarded_arithmetic&&json_string(document,L"kind")== (active_loops?"active-loop-input-decision":"input-decision")))&&
            (!guarded_arithmetic||(!joint&&!input_decisions&&json_string(document,L"kind")=="guarded-local-arithmetic")),
        "The UI manifest is not the audited main publication and schema");
    std::vector<Fixture> result;
    std::set<std::string> names;
    std::size_t owner_count=0;
    for(const auto& raw:document.GetNamedArray(L"lessons")) {
        const auto item=raw.GetObject();
        Fixture fixture;
        fixture.joint_local_int=joint;
        fixture.input_decisions=input_decisions;
        fixture.active_loops=active_loops;
        fixture.guarded_arithmetic=guarded_arithmetic;
        fixture.id=json_string(item,L"id");
        check(names.insert(fixture.id).second,"Repeated lesson manifest entry");
        if(joint) {
            const auto& pin=joint_publication(fixture.id);
            check(json_string(item.HasKey(L"publishedCommit")?item:document,L"publishedCommit")==pin.published_commit,
                "The joint lesson differs from its independently pinned publication commit");
        }
        fixture.before_path=fixture_path(manifest.parent_path(),json_string(item,L"before"));
        fixture.after_path=fixture_path(manifest.parent_path(),json_string(item,L"after"));
        fixture.before_sha256=json_string(item,L"beforeSha256");
        fixture.after_sha256=json_string(item,L"afterSha256");
        check(state::sha256(state::read_bytes(fixture.before_path))==fixture.before_sha256&&
                state::sha256(state::read_bytes(fixture.after_path))==fixture.after_sha256,
            "A lesson fixture differs from its pinned manifest bytes");
        fixture.before=windows::load_lesson(fixture.before_path);
        fixture.after=windows::load_lesson(fixture.after_path);
        for(const auto& value:item.GetNamedArray(L"solutions")) {
            const auto entry=value.GetObject();
            Solution solution{json_string(entry,L"activity"),json_string(entry,L"source")};
            check(!solution.source.empty()&&state::sha256(solution.source)==json_string(entry,L"sourceSha256"),
                "An audited solution differs from its manifest source hash");
            fixture.solutions.push_back(std::move(solution));
        }
        for(const auto& value:item.GetNamedArray(L"plans")) {
            const auto entry=value.GetObject();
            Plan plan{json_string(entry,L"activity"),json_string(entry,L"reference"),
                json_string(entry,L"fixedSource"),std::nullopt,{}};
            check(!plan.fixed_source.empty()&&state::sha256(plan.fixed_source)==json_string(entry,L"fixedSourceSha256"),
                "A fixed-report counterexample differs from its pinned source");
            if(entry.HasKey(L"noPlanSource")) {
                plan.no_plan_source=json_string(entry,L"noPlanSource");
                check(state::sha256(*plan.no_plan_source)==json_string(entry,L"noPlanSourceSha256"),
                    "The no-plan counterexample differs from its pinned source");
            }
            if(input_decisions)read_input_decision_plan(fixture,entry,plan);
            if(guarded_arithmetic)read_guarded_arithmetic_plan(fixture,entry,plan);
            if(joint)read_joint_plan(fixture,entry,plan);
            fixture.plans.push_back(std::move(plan));++owner_count;
        }
        if(guarded_arithmetic)verify_only_guarded_append(fixture);
        else if(input_decisions)verify_input_decision_adoption(fixture);
        else if(joint)verify_only_joint_append(fixture);else verify_only_root_adoption(fixture);
        result.push_back(std::move(fixture));
    }
    if(guarded_arithmetic)check(result.size()==1&&owner_count==1&&names==std::set<std::string>{"u3l6"},
        "The arithmetic UI manifest must retain exactly its one authenticated repair owner");
    else if(active_loops)check(result.size()==2&&owner_count==2&&names==std::set<std::string>{"u4l1","u4l2"},
        "Active loop QA must cover both independent owners and six private observations");
    else if(input_decisions)check(result.size()==8&&owner_count==8&&
            names==std::set<std::string>{"u3l3","u3l4","u3l5","u3l6","u3l7","u3l8","u4l1","u4l2"},
        "The input-decision manifest must contain exactly eight independent owners and forty private observations");
    else if(joint)check(result.size()==3&&owner_count==3&&names==std::set<std::string>{"u3l5","u3l7","u3l8"},
        "The joint UI manifest must preserve all three authenticated lessons and owners");
    else check(result.size()==9&&owner_count==16,"The UI manifest must cover nine lessons and sixteen focal owners");
    return result;
}

void prepare_quizzes(state::StateData& data,const Lesson& lesson,std::size_t through) {
    // Authored answers evaluated by the real core are QA setup only. No claim
    // is made that these nonfocal quizzes were answered through their controls.
    for(std::size_t page=0;page<=through;++page)for(const auto& block:lesson.pages.at(page).blocks) {
        if(const auto old=state::get_response(data,lesson,block);old&&old->correct)continue;
        state::Response response;
        switch(block.type) {
        case BlockType::multiple_choice:
            check(block.correct_index.has_value(),"A prerequisite choice has no authored answer");
            response.selected=static_cast<std::int64_t>(*block.correct_index);
            response.correct=curriculum::evaluate_choice(block,*block.correct_index).correct;
            break;
        case BlockType::fill_code:
        case BlockType::output_prediction:
            check(!block.answers.empty(),"A prerequisite text quiz has no authored answer");
            response.answer=block.answers.front();
            response.correct=curriculum::evaluate_text_answer(block,response.answer).correct;
            break;
        case BlockType::order_code:
            for(const auto index:block.correct_order)response.order.push_back(static_cast<std::int64_t>(index));
            response.correct=curriculum::evaluate_order(block,block.correct_order).correct;
            break;
        case BlockType::inline_fill_code: {
            JsonArray answers;
            for(const auto& answer:block.answers)answers.Append(JsonValue::CreateStringValue(wide(answer)));
            JsonObject raw;raw.Insert(L"answers",answers);response.raw_json=winrt::to_string(raw.Stringify());
            response.correct=curriculum::evaluate_hole_answers(block,block.answers).correct;
            break;
        }
        case BlockType::drag_fill_code: {
            JsonArray slots;
            for(const auto slot:block.correct_order)slots.Append(JsonValue::CreateNumberValue(static_cast<double>(slot)));
            JsonObject raw;raw.Insert(L"slots",slots);response.raw_json=winrt::to_string(raw.Stringify());
            response.correct=curriculum::evaluate_drag_answers(block,block.correct_order).correct;
            break;
        }
        case BlockType::math_puzzle:
            check(false,"A mathematical prerequisite needs its own explicitly audited preparation");
            break;
        default:continue;
        }
        check(response.correct,"An authored nonfocal quiz answer failed its real evaluator");
        state::mark_response_reviewed(response,block);
        state::put_response(data,lesson,block,response);
    }
}
windows::CppRunRequest request_for(const WindowsEnvironment& environment,const Lesson& lesson,
        const Block& block,std::string source,std::string input) {
    windows::CppRunRequest request;
    request.request_id=++request_sequence;request.revision=request_sequence;
    request.project_key=curriculum::draft_key("programming",lesson,block);
    request.projects_directory=environment.paths.projects;
    request.source_utf8=std::move(source);request.stdin_utf8=std::move(input);
    request.compile_timeout=std::chrono::seconds(60);request.execution_timeout=std::chrono::seconds(8);
    request.local_int_check=block.local_int_check;request.local_string_check=block.local_string_check;
    request.input_check=block.input_check;request.local_role_check=block.local_role_check;
    return request;
}
void verify_input_decision_counterexamples(const Fixture& fixture,const Plan& plan,std::size_t ordinal) {
    if(plan.input_counterexamples.empty())return;
    phase=fixture.id+"/"+plan.activity+" preparing independent invariant captures";
    const auto& owner=block_at(fixture.after,plan.activity);
    check(fixture.input_decisions&&owner.input_check&&owner.input_check->allow_same_output,
        "Invariant counterexamples require their exact opt-in input owner");
    const auto& first=owner.output_check->cases.front();
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"input-decision-capture-"+std::to_wstring(GetCurrentProcessId())+L"-"+
            std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(ordinal),false});
    check(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),"Invariant QA would reuse an existing profile");
    initialize_profile_directories(environment.paths);
    for(const auto& example:plan.input_counterexamples) {
        phase=fixture.id+"/"+plan.activity+" executing invariant counterexample "+example.label;
        within_budget();
        const auto request=request_for(environment,fixture.after,owner,example.source,first.standard_input);
        check(windows::compiler_runtime_request_allowed(owner,request),
            "An invariant counterexample was rejected by request shape instead of its native execution");
        const auto result=windows::run_cpp(request);
        const auto captured=windows::capture_cpp_execution(request,result);
        const auto fingerprint=windows::cpp_input_request_fingerprint(request);
        check(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
                result.execution.stderr_bytes.empty()&&!fingerprint.empty()&&
                result.input_request_fingerprint==fingerprint&&captured.input_request_fingerprint==fingerprint&&
                captured.source_revision==request.revision&&!captured.input_check_correct&&
                windows::evaluate_cpp_output_check(*owner.output_check,captured,request.revision).verdict==
                    curriculum::OutputVerdict::correct&&
                result.input_evidence.size()==owner.input_check->probes.size()&&
                state::sha256(state::read_bytes(result.run_directory/L"program.exe"))==result.input_executable_sha256,
            "The invariant negative needs a real public success and complete private captures from the same executable");
        auto legacy_policy=request;legacy_policy.input_check->allow_same_output=false;
        check(!windows::cpp_input_request_fingerprint(legacy_policy).empty()&&
                windows::cpp_input_request_fingerprint(legacy_policy)!=fingerprint,
            "Opt-in input evidence reused the legacy request domain");
        std::set<std::string> failed;
        for(std::size_t index=0;index<owner.input_check->probes.size();++index) {
            const auto& probe=owner.input_check->probes[index];const auto& evidence=result.input_evidence[index];
            check(evidence.source_sha256==state::sha256(request.source_utf8)&&
                    evidence.standard_input==probe.standard_input&&
                    evidence.executable_sha256==result.input_executable_sha256&&
                    evidence.execution.succeeded()&&evidence.execution.stderr_bytes.empty(),
                "An invariant observation borrowed another source/input/executable or failed to execute normally");
            if(!curriculum::cpp_input_expected_output(evidence.execution.stdout_bytes,probe)) {
                check(shares_first_output(probe,first),
                    "An invariant negative failed a distinct-output contrast instead of its missing boundary or condition");
                failed.insert(probe.standard_input);
            }
        }
        check(failed==example.failed_inputs,
            "The actual invariant failure matrix differs from its independently pinned oracle");
        check(bytes(result.run_directory/L"main.cpp")==request.source_utf8&&
                bytes(result.run_directory/L"stdin.bin")==first.standard_input&&
                bytes(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
                bytes(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "Private invariant observations changed the original public source, input or console files");
        std::cout<<"PASS input invariant "<<fixture.id<<'/'<<plan.activity<<'/'<<example.label
            <<": public first case and every distinct-output contrast pass; only "<<failed.size()
            <<" selected invariant input(s) reject, with exact source/executable/request evidence and restored public files.\n";
    }
}
void verify_guarded_arithmetic_sources(const Fixture& fixture,const Plan& plan,std::size_t ordinal) {
    const auto& owner=block_at(fixture.after,plan.activity);
    check(fixture.guarded_arithmetic&&owner.local_int_check&&owner.local_int_check->probes.size()==5,
        "Arithmetic captures need the exact extended local contract");
    const auto& first=owner.output_check->cases.front();
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"guarded-arithmetic-capture-"+std::to_wstring(GetCurrentProcessId())+L"-"+
            std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(ordinal),false});
    check(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),"Arithmetic QA would reuse an existing profile");
    initialize_profile_directories(environment.paths);
    for(const auto& example:plan.arithmetic_sources) {
        phase=fixture.id+"/"+plan.activity+" observing real arithmetic source "+example.label;
        within_budget();
        const auto request=request_for(environment,fixture.after,owner,example.source,first.standard_input);
        check(windows::compiler_runtime_request_allowed(owner,request),
            "An arithmetic example lacks a safe plan; the regression must reach the actual worker");
        const auto result=windows::run_cpp(request);
        const auto captured=windows::capture_cpp_execution(request,result);
        const auto fingerprint=windows::cpp_local_int_request_fingerprint(request);
        check(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
                result.execution.stderr_bytes.empty()&&!fingerprint.empty()&&
                captured.source_revision==request.revision&&
                result.local_int_request_fingerprint==fingerprint&&
                captured.local_int_request_fingerprint==fingerprint&&result.local_int_evidence.size()==5,
            "Arithmetic QA did not capture the original program and all five real private phases");
        const bool public_correct=windows::evaluate_cpp_output_check(*owner.output_check,captured,
            request.revision).verdict==curriculum::OutputVerdict::correct;
        check(public_correct==(example.label!="initial")&&
                (public_correct&&captured.local_int_check_correct)==example.accepted,
            "The actual arithmetic source verdict differs from the independent acceptance matrix");
        auto legacy=request;legacy.local_int_check=block_at(fixture.before,plan.activity).local_int_check;
        check(!windows::cpp_local_int_request_fingerprint(legacy).empty()&&
                windows::cpp_local_int_request_fingerprint(legacy)!=fingerprint,
            "The forbidden-arithmetic objective reused the historical observation domain");
        std::size_t safe_stops=0;
        for(std::size_t index=0;index<owner.local_int_check->probes.size();++index) {
            const auto& probe=owner.local_int_check->probes[index];
            const auto& evidence=result.local_int_evidence[index];
            const auto instrumented=curriculum::cpp_local_int_override(request.source_utf8,probe);
            check(instrumented&&evidence.instrumented_source_sha256==state::sha256(*instrumented)&&
                    evidence.compilation.succeeded()&&
                    evidence.execution.status==windows::CppPhaseStatus::completed&&
                    evidence.execution.exit_code&&evidence.execution.stderr_bytes.empty(),
                "An arithmetic phase failed compilation, borrowed source bytes or ended abnormally");
            if(index>=3&&example.guard_exit) {
                check(*evidence.execution.exit_code==*example.guard_exit&&!evidence.execution.succeeded(),
                    "Forbidden arithmetic did not stop at exit 91 before the operator; a divide exception is not evidence");
                ++safe_stops;
            }else {
                check(evidence.execution.succeeded(),"An admissible arithmetic phase unexpectedly failed to run");
                if(example.label!="initial"||index>=3)
                    check(curriculum::cpp_local_int_expected_output(probe,evidence.execution.stdout_bytes),
                        "A valid arithmetic form or prior contrast changed its independently authored output");
            }
        }
        check(safe_stops==(example.guard_exit?2u:0u),
            "Arithmetic QA must demonstrate both zero and negative safe-stop observations");
        check(bytes(result.run_directory/L"main.cpp")==request.source_utf8&&
                bytes(result.run_directory/L"stdin.bin")==first.standard_input&&
                bytes(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
                bytes(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "Arithmetic instrumentation changed the learner source, input or public execution artifacts");
        std::cout<<"PASS guarded arithmetic source "<<example.label<<": original public result and three legacy probes; "
            <<(example.guard_exit?"both forbidden scenarios stop at 91 before / or %":
                example.accepted?"both forbidden scenarios skip arithmetic and retain exact output":
                    "authentic initial code remains rejected by its public/legacy expectations")
            <<"; five exact variant hashes and real MSVC captures, restored public files.\n";
    }
}
void verify_joint_counterexamples(const Fixture& fixture,const Plan& plan,
        const WindowsEnvironment& environment) {
    const auto& pin=joint_publication(fixture.id);
    const auto& block=block_at(fixture.after,plan.activity);
    const auto& first=block.output_check->cases.front();
    for(const auto& example:plan.joint_counterexamples) {
        phase=fixture.id+"/"+plan.activity+" isolating the real joint counterexample "+example.label;
        const auto& expected=joint_counterexample_oracle(pin,example.label);
        const auto request=request_for(environment,fixture.after,block,example.source,first.standard_input);
        check(windows::compiler_runtime_request_allowed(block,request),
            "The joint counterexample was rejected by request shape instead of its real execution");
        const auto result=windows::run_cpp(request);
        const auto captured=windows::capture_cpp_execution(request,result);
        check(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
                result.execution.stderr_bytes.empty()&&
                windows::evaluate_cpp_output_check(*block.output_check,captured,request.revision).verdict==
                    curriculum::OutputVerdict::correct&&
                captured.source_revision==request.revision&&!captured.local_int_check_correct&&
                !captured.local_int_request_fingerprint.empty()&&
                captured.local_int_request_fingerprint==windows::cpp_local_int_request_fingerprint(request)&&
                result.local_int_request_fingerprint==captured.local_int_request_fingerprint&&
                result.local_int_evidence.size()==pin.original_probes+pin.additions.size(),
            "The joint counterexample needs a real public success and complete private captures from this request");
        for(std::size_t index=0;index<block.local_int_check->probes.size();++index) {
            const auto& probe=block.local_int_check->probes[index];
            const auto& evidence=result.local_int_evidence[index];
            const auto instrumented=curriculum::cpp_local_int_override(request.source_utf8,probe);
            check(instrumented&&evidence.instrumented_source_sha256==state::sha256(*instrumented)&&
                    evidence.compilation.succeeded()&&evidence.execution.succeeded()&&
                    evidence.execution.stderr_bytes.empty()&&
                    curriculum::cpp_local_int_expected_output(probe,evidence.execution.stdout_bytes)==
                        (index!=example.failed_probe),
                "A joint counterexample failed a different observation or borrowed another source");
            if(index==example.failed_probe)
                check(normalized(evidence.execution.stdout_bytes)==expected.negative_output,
                    "The joint scenario did not produce the independently expected incorrect observation");
        }
        check(bytes(result.run_directory/L"main.cpp")==request.source_utf8&&
                bytes(result.run_directory/L"stdin.bin")==first.standard_input&&
                bytes(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
                bytes(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "The joint observations changed the public source, stdin or saved console");
        std::cout<<"PASS joint capture "<<fixture.id<<'/'<<plan.activity<<'/'<<example.label<<": "
            <<pin.original_probes<<" original single-variable probes and public output pass; only appended probe "
            <<example.failed_probe<<" rejects; all source hashes and real MSVC phases verified, public files restored.\n";
    }
}
void prepare_compilers(state::StateData& data,const Fixture& fixture,const Lesson& lesson,
        const Plan& plan,std::size_t through,const WindowsEnvironment& environment) {
    // Each nonfocal receipt comes from an actual native compilation/execution,
    // carrying all four private contracts when present in this exact model.
    for(std::size_t page=0;page<=through;++page)for(const auto& block:lesson.pages.at(page).blocks) {
        if(block.type!=BlockType::cpp_compiler||block.stable_id==plan.activity||
           windows::compiler_puzzle_completed(data,lesson,block))continue;
        within_budget();
        check(block.output_check&&!block.output_check->cases.empty(),"A nonfocal compiler has no public case");
        const auto request=request_for(environment,lesson,block,solution_for(fixture,block.stable_id),
            block.output_check->cases.front().standard_input);
        check(windows::compiler_runtime_request_allowed(block,request),"A nonfocal request lacks its actual authored contract");
        const auto result=windows::run_cpp(request);
        const auto captured=windows::capture_cpp_execution(request,result);
        check(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
                (!block.local_int_check||captured.local_int_check_correct)&&
                (!block.local_string_check||captured.local_string_check_correct)&&
                (!block.input_check||captured.input_check_correct)&&
                (!block.local_role_check||captured.local_role_check_correct)&&
                windows::evaluate_cpp_output_check(*block.output_check,captured,request.revision).verdict==
                    curriculum::OutputVerdict::correct&&
                windows::record_compiler_puzzle_case(data,lesson,block,request,captured,
                    "QA setup: actual nonfocal compiler and its authored private checks")&&
                windows::compiler_puzzle_completed(data,lesson,block),
            "A real nonfocal prerequisite failed: "+block.stable_id+" "+result.error_utf8+
                result.compilation.stderr_bytes);
    }
}

void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT)continue;
        if(!dispatch_lesson_enter(message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
}
HWND descendant(HWND parent,int id) {
    struct Search { int id;HWND result=nullptr; } search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL {
        auto& current=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)!=current.id)return TRUE;
        current.result=item;return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
std::string window_text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(window,value.data(),static_cast<int>(value.size()))));
    return utf8(value);
}
struct View {
    WindowsEnvironment environment;
    Lesson lesson;
    std::string activity;
    HWND host=nullptr,view=nullptr;
    std::size_t executions=0,finishes=0;

    View(const Fixture& fixture,const Plan& plan,std::size_t ordinal)
        :environment(resolve_windows_environment({ProfileId::Codex,
            L"local-dependency-view-"+std::to_wstring(GetCurrentProcessId())+L"-"+
                std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(ordinal),false})),
         lesson(fixture.before),activity(plan.activity) {
        check(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
                !std::filesystem::exists(environment.paths.root),"UI QA would open an existing profile");
        initialize_profile_directories(environment.paths);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden local dependency QA",
            WS_OVERLAPPEDWINDOW,0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the real hidden Win32 host");
    }
    ~View() {
        if(view){(void)flush_lesson_view(view);DestroyWindow(view);}
        if(host)DestroyWindow(host);
    }
    void open() {
        within_budget();
        check(!view,"A second LessonView would duplicate a running worker");
        LessonViewHooks hooks;
        // Observe the real Finish action without replacing its guards or
        // closing the host needed for subsequent reopen and Reset checks.
        hooks.lesson_completed=[this](HWND){++finishes;};
        view=create_lesson_view(host,environment,lesson,std::move(hooks));
        check(view!=nullptr,"Cannot create the production LessonView");
        MoveWindow(view,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Hidden UI QA exposed a desktop window");
    }
    void close() {
        check(view&&flush_lesson_view(view),"Cannot flush the real lesson before closing");
        DestroyWindow(view);view=nullptr;pump();
    }
    void reopen() {close();open();}
    state::StateData saved() {
        check(view&&flush_lesson_view(view),"Cannot flush the real learner controls");
        state::StateStore store(environment.paths.root);return store.load();
    }
    void write_closed(const state::StateData& data) {
        check(!view,"QA setup cannot rewrite state beneath a live LessonView");
        state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
    }
    HWND maybe_control(std::size_t block,int part) const {
        return descendant(view,1000+static_cast<int>(block)*100+part);
    }
    HWND control(int part) const {
        const auto at=position(lesson,activity);
        check(lesson_page_state(view).index==at.page,"A focal control was requested on another page");
        const auto result=maybe_control(at.block,part);
        check(result!=nullptr,"Missing real focal control part="+std::to_string(part));
        return result;
    }
    std::string text(int part) const {return edit_text(control(part));}
    void replace(int part,std::string_view value) {
        const auto field=control(part);
        check(IsWindowEnabled(field),"The focal source/input field is unexpectedly disabled");
        CHARRANGE all{0,-1};SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
        const auto native=wide(value);
        SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(native.c_str()));pump();
        check(normalized(edit_text(field))==normalized(value),"The real edit changed supplied source/input characters");
    }
    void input(std::string_view value) {
        const auto at=position(lesson,activity);
        if(maybe_control(at.block,15))replace(15,value);
        else check(value.empty(),"An authored input case has no actual input control");
    }
    void select_source() {
        CHARRANGE range{3,11};SendMessageW(control(10),EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));pump();
    }
    std::set<std::filesystem::path> directories() const {
        std::set<std::filesystem::path> result;
        const auto parent=environment.paths.projects/L"cpp-runs";
        if(std::filesystem::exists(parent))for(const auto& entry:std::filesystem::directory_iterator(parent))
            if(entry.is_directory())result.insert(entry.path());
        return result;
    }
    void run(std::string_view source,const curriculum::OutputCase& output) {
        within_budget();
        const auto before=directories();
        const auto button=control(11);
        check(IsWindowEnabled(button)&&window_text(button)=="Compilar, ejecutar y comprobar",
            "The actual combined compiler action is unavailable");
        SendMessageW(button,BM_CLICK,0,0);
        const auto deadline=GetTickCount64()+230000;
        do {
            pump();within_budget();
            if(IsWindowEnabled(button)&&directories()!=before)break;
            MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
        }while(GetTickCount64()<deadline);
        check(IsWindowEnabled(button)&&directories()!=before,"The native UI worker did not finish within its bound");
        const auto status=text(34);
        check(status=="Ejecución terminada (código 0)."||
                status=="Caso ejecutado. Aún falta acreditar el primer caso obligatorio de este editor."||
                status.starts_with("Comprobación obligatoria: "),
            "The public native execution did not complete normally: "+status);
        std::vector<std::filesystem::path> created;
        for(const auto& directory:directories())if(!before.contains(directory))created.push_back(directory);
        check(created.size()==1,"One real button action did not create exactly one public run");
        const auto& directory=created.front();
        const auto compiler=std::filesystem::path(build::educational_vc_root)/L"bin/Hostx64/x64/cl.exe";
        const auto toolchain=bytes(directory/L"toolchain.txt");
        check(toolchain.starts_with("MSVC=")&&toolchain.find("compiler="+utf8(compiler.wstring())+'\n')!=toolchain.npos&&
                std::filesystem::is_regular_file(directory/L"program.exe")&&
                std::filesystem::file_size(directory/L"program.exe")>0,
            "The button did not use the configured native compiler and a real executable");
        const auto stdout_bytes=bytes(directory/L"execution-stdout.bin");
        check(bytes(directory/L"main.cpp")==source&&bytes(directory/L"stdin.bin")==output.standard_input&&
                bytes(directory/L"execution-stderr.bin").empty()&&
                normalized(text(14))==normalized(stdout_bytes)&&
                std::ranges::any_of(output.outputs,[&](const auto& expected){return normalized(expected)==normalized(stdout_bytes);}),
            "A private trial changed the public source, stdin, stdout or visible console");
        check(!IsWindowVisible(host),"The real worker exposed the hidden QA host");
        ++executions;
    }
    void navigate_to(std::size_t destination) {
        auto current=lesson_page_state(view).index;
        if(destination<current) {
            check(navigate_lesson_view(view,destination),"Cannot return to the real focal page");pump();
        }else while(current<destination) {
            check(navigate_lesson_view(view,++current),"An earned first case did not release actual page navigation");pump();
        }
        check(lesson_page_state(view).index==destination,"Navigation did not reach its real target page");
    }
    bool can_continue() const {
        const auto page=lesson_page_state(view);
        if(page.index+1<page.count)return page.can_next;
        const auto options=lesson_options_snapshot(view);
        check(!page.can_next&&options.finish_visible,
            "The final page lost its actual Finish action or invented a next page");
        return options.finish_enabled;
    }
    void finish() {
        const auto page=lesson_page_state(view);
        const auto previous=finishes;
        check(page.index+1==page.count&&can_continue()&&
                invoke_lesson_option(view,LessonOptionAction::finish),
            "An earned final objective did not release the real Finish action");
        pump();
        check(finishes==previous+1,"The real Finish action did not invoke its observed completion hook once");
    }
    void reset() {
        const auto button=control(12);
        check(IsWindowEnabled(button),"The actual compiler Reset action is unavailable");
        SendMessageW(button,BM_CLICK,0,0);pump();
        check(text(10)==block_at(lesson,activity).initial_code&&text(4).empty()&&text(14).empty(),
            "Reset did not restore this editor's initial source and clear its own verdict/console");
    }
};

std::string receipt_prefix(const Lesson& lesson,const Block& block) {
    return "compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+curriculum::exercise_key(lesson,block)+'/';
}
state::Preferences receipts(const state::StateData& data,std::string_view prefix) {
    state::Preferences result;
    if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
        for(const auto& [key,value]:domain->second)if(key.starts_with(prefix))result.emplace(key,value);
    return result;
}
void retained_pairs(const state::StateData& data,const state::Preferences& earlier) {
    for(const auto& [key,value]:earlier)
        check(data.preferences.at("native_windows").at(key)==value,
            "An operation rewrote a historical receipt: "+key);
}
state::Preferences achievement_seals(const state::StateData& data,std::string_view prefix) {
    auto result=receipts(data,prefix);
    std::erase_if(result,[](const auto& entry) {
        const auto& key=entry.first;
        return key.find("/review/")!=key.npos||
            !(key.ends_with("/completed")||key.ends_with("/feedback")||key.ends_with("/witness")||
              key.ends_with("/local-int/v1")||key.ends_with("/local-string/v1"));
    });
    return result;
}
void same_draft(const state::Draft& left,const state::Draft& right) {
    check(left.has_saved&&right.has_saved&&left.source==right.source&&left.input==right.input&&
            left.selection_start==right.selection_start&&left.selection_end==right.selection_end,
        "Closing/reopening changed draft source, stdin or selection");
}
void check_feedback(View& view,bool correct) {
    const auto feedback=view.text(4);
    check(feedback.starts_with(correct?"✓ Correcto":"✗ Incorrecto"),"The visible verdict differs from the actual private outcome");
    const auto focal=position(view.lesson,view.activity);
    bool found=false;
    for(const auto& region:lesson_reading_regions(view.view))if(region.target==view.control(4)) {
        found=true;
        check(region.block_index==focal.block&&region.feedback&&region.correct==correct&&
                region.source_text==feedback,"Visible feedback and its real reading metadata diverged");
    }
    check(found,"The actual feedback lacks its reading region");
    if(correct)return;
    check(feedback.size()>std::string_view("✗ Incorrecto").size()&&
            feedback.find("CMake")==feedback.npos&&feedback.find("todos los archivos originales")==feedback.npos,
        "A scalar/input rejection omitted its guide or claimed an unrelated CMake requirement");
    const auto& block=block_at(view.lesson,view.activity);
    const auto absent=[&](std::string_view value) {
        if(value.empty())return;
        check(feedback.find(value)==feedback.npos&&view.text(34).find(value)==std::string::npos,
            "The public guide disclosed a private oracle or trial input");
    };
    if(block.local_int_check)for(const auto& probe:block.local_int_check->probes)
        for(const auto& output:probe.expected_stdout)absent(output);
    if(block.local_string_check)for(const auto& probe:block.local_string_check->probes)
        for(const auto& output:probe.expected_stdout)absent(output);
    if(block.input_check)for(const auto& probe:block.input_check->probes) {
        absent(probe.standard_input);for(const auto& output:probe.outputs)absent(output);
    }
    if(block.local_role_check)for(const auto& role:block.local_role_check->roles)
        for(const auto& probe:role.probes)for(const auto& output:probe.expected_stdout)absent(output);
    absent("lgaed_role_probe_");absent("lgaed_int_probe_");
}
void check_reader_goal(View& view,bool completed) {
    const auto focal=position(view.lesson,view.activity);
    bool found=false;
    for(const auto& region:lesson_reading_regions(view.view))
        if(region.block_index==focal.block&&region.required_compiler&&!region.feedback) {
            found=true;check(region.solved==completed,"The real reader metadata granted or lost the private objective");
        }
    check(found,"The actual compiler lacks its required reader metadata");
}
void reference_visible(View& view,const Plan& plan) {
    const auto at=position(view.lesson,plan.reference);
    check(lesson_page_state(view.view).index==at.page,"Reference visibility was queried on another page");
    const auto& reference=block_at(view.lesson,plan.reference);
    const auto field=view.maybe_control(at.block,1);
    check(field&&(GetWindowLongPtrW(field,GWL_STYLE)&WS_VISIBLE)&&edit_text(field)==reference.code,
        "The earned reference is not rendered from its authentic code");
    // The reader names a code example; its source stays in the visual control
    // and is deliberately excluded from the narration script.
    const auto caption=view.maybe_control(at.block,16);
    const auto regions=lesson_reading_regions(view.view);
    check(caption&&(GetWindowLongPtrW(caption,GWL_STYLE)&WS_VISIBLE)&&
            std::ranges::any_of(regions,[&](const auto& region) {
                return region.block_index==at.block&&region.target==caption&&region.passive_label&&
                    region.source_text=="Ejemplo de código C más más.";
            })&&std::ranges::none_of(regions,[&](const auto& region){return region.target==field;}),
        "The earned reference lost its passive reading caption or exposed code as narration");
}
void blocked(View& view,const Plan& plan,std::size_t bookmark) {
    const auto target=position(view.lesson,plan.activity);
    check(lesson_page_state(view.view).index==target.page&&!view.can_continue(),
        "A legacy or optional-only receipt released the new required objective");
    const auto options=lesson_options_snapshot(view.view);
    if(bookmark>target.page) {
        check(!navigate_lesson_view(view.view,bookmark)&&lesson_page_state(view.view).index==target.page,
            "Navigation bypassed an unverified local/input dependency");
        check(std::ranges::find(options.available_pages,bookmark)==options.available_pages.end(),
            "The actual Pages gate admitted the saved later bookmark");
    }else {
        check(target.page+1==view.lesson.pages.size()&&options.finish_visible&&!options.finish_enabled,
            "The last page did not preserve its pending Finish gate");
        const auto previous=view.finishes;
        check(!invoke_lesson_option(view.view,LessonOptionAction::finish),
            "The real Finish action accepted an unverified final objective");
        pump();check(view.finishes==previous,"A blocked Finish action invoked the completion hook");
    }
    const auto reference=position(view.lesson,plan.reference);
    if(reference.page==target.page) {
        check(!view.maybe_control(reference.block,1),"A same-page reference was exposed before its private objective");
        const auto regions=lesson_reading_regions(view.view);
        check(std::ranges::none_of(regions,[&](const auto& region){return region.block_index==reference.block;}),
            "A blocked same-page reference leaked through reading metadata");
    }
    check_reader_goal(view,false);
}

void run_plan(const Fixture& fixture,const Plan& plan,std::size_t ordinal) {
    const bool local_extension=fixture.joint_local_int||fixture.guarded_arithmetic;
    phase=fixture.id+"/"+plan.activity+" preparing authentic BEFORE";
    const auto target=position(fixture.before,plan.activity);
    const auto reference=position(fixture.before,plan.reference);
    const bool final_page=target.page+1==fixture.before.pages.size();
    const auto bookmark=final_page?target.page:std::max(reference.page,target.page+1);
    check(bookmark<fixture.before.pages.size()&&(!final_page||reference.page==target.page),
        "The owner has neither a later bookmark nor an authored final-page reference");
    const auto& old=block_at(fixture.before,plan.activity);
    const auto& current=block_at(fixture.after,plan.activity);
    const auto& first=current.output_check->cases.front();
    const auto& source=solution_for(fixture,plan.activity);
    const auto key=curriculum::draft_key("programming",fixture.before,old);
    const auto prefix=receipt_prefix(fixture.before,old);
    View view(fixture,plan,ordinal);
    if(fixture.joint_local_int)verify_joint_counterexamples(fixture,plan,view.environment);
    state::StateData initial;state::prepare_lesson_state(initial,fixture.before);
    prepare_quizzes(initial,fixture.before,bookmark);
    prepare_compilers(initial,fixture,fixture.before,plan,bookmark,view.environment);
    initial.preferences["native_windows"]["ui_language"]=std::string("es");
    initial.preferences["local_dependency_view_unrelated"]["keep"]=std::string(" \t independent preference\n");
    std::vector<std::pair<std::string,state::Draft>> other_drafts;
    for(const auto& page:fixture.before.pages)for(const auto& block:page.blocks)
        if(block.type==BlockType::cpp_compiler&&block.stable_id!=plan.activity) {
            state::Draft draft;draft.source=block.initial_code+"\n// Saved independent learner draft.\n";
            draft.input="saved independent input\n";draft.selection_start=2;draft.selection_end=8;
            const auto other_key=curriculum::draft_key("programming",fixture.before,block);
            state::put_draft(initial,other_key,draft);
            other_drafts.emplace_back(other_key,state::get_draft(initial,other_key,""));
        }
    for(std::size_t page=0;page<target.page;++page)(void)state::mark_page_reached(initial,fixture.before,page);
    state::save_current_page(initial,fixture.before,target.page);
    view.write_closed(initial);view.open();
    check(lesson_page_state(view.view).index==target.page,"BEFORE did not open its actual focal page");
    view.replace(10,plan.fixed_source);view.input(first.standard_input);
    phase=fixture.id+"/"+plan.activity+" earning actual historical UI receipt";
    view.run(plan.fixed_source,first);check_feedback(view,true);
    check(windows::compiler_puzzle_completed(view.saved(),fixture.before,old)&&
            view.can_continue(),"A real historical first stdout failed to earn its old contract");
    view.select_source();
    if(reference.page==target.page)reference_visible(view,plan);
    if(final_page)view.finish();else view.navigate_to(bookmark);
    if(reference.page==bookmark)reference_visible(view,plan);
    const auto legacy=view.saved();
    const auto old_receipts=receipts(legacy,prefix);
    const auto old_draft=state::get_draft(legacy,key,old.initial_code,old.standard_input.value_or(""));
    check(!old_receipts.empty()&&windows::compiler_puzzle_case_count(legacy,fixture.before,old)==1&&
            old_draft.source==plan.fixed_source&&old_draft.input==first.standard_input,
        "The actual BEFORE UI did not persist its fixed source, first case and authored bookmark");
    const bool has_reference=std::ranges::any_of(fixture.before.reference_after,
        [&](const auto& relation){return relation.activity==plan.activity;})||
        std::ranges::any_of(fixture.before.activity_after,[&](const auto& relation){
            return std::ranges::find(relation.prerequisites,plan.activity)!=relation.prerequisites.end();
        });
    const auto reference_key="reference-proof/v1/"+fixture.before.locale+'/'+key;
    const auto old_reference_proof=state::string_value(legacy,"native_windows",reference_key);
    if(has_reference) {
        if(local_extension)
            check(old.local_int_check&&old_reference_proof.empty()&&
                    windows::compiler_puzzle_completed(legacy,fixture.before,old)&&
                    windows::compiler_puzzle_case_count(legacy,fixture.before,old)==1,
                "A joint BEFORE reference needs its real local receipt and no invented stdout-only proof");
        else check(!old_reference_proof.empty(),
            "The BEFORE UI failed to create the real legacy reference proof being tested");
    }
    view.close();

    phase=fixture.id+"/"+plan.activity+" reopening AFTER from the historical bookmark";
    auto prepared=legacy;state::prepare_lesson_state(prepared,fixture.after);
    prepare_quizzes(prepared,fixture.after,bookmark);
    prepare_compilers(prepared,fixture,fixture.after,plan,bookmark,view.environment);
    retained_pairs(prepared,old_receipts);
    state::Preferences other_receipts;
    const auto all_prefix="compiler-puzzle/v1/"+fixture.after.locale+'/'+fixture.after.id+'/';
    for(const auto& [entry,value]:receipts(prepared,all_prefix))
        if(!entry.starts_with(prefix))other_receipts.emplace(entry,value);
    view.write_closed(prepared);view.lesson=fixture.after;view.open();
    const auto preserved=[&](const state::StateData& saved) {
        retained_pairs(saved,old_receipts);retained_pairs(saved,other_receipts);
        check(state::string_value(saved,"native_windows",reference_key)==old_reference_proof,
            "An operation rewrote or removed the historical reference proof");
        check(saved.preferences.at("local_dependency_view_unrelated")==
                initial.preferences.at("local_dependency_view_unrelated"),"An unrelated preference changed");
        check(windows::compiler_puzzle_completed(saved,fixture.before,old)&&
                windows::compiler_puzzle_case_count(saved,fixture.before,old)==1,
            local_extension?"The genuine historical single-variable receipt disappeared":
                "The genuine historical stdout receipt disappeared");
        for(const auto& [draft_key,expected]:other_drafts)
            same_draft(state::get_draft(saved,draft_key,""),expected);
    };
    auto saved=view.saved();preserved(saved);
    same_draft(state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")),old_draft);
    check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==0&&
            !view.text(4).starts_with("✓ Correcto"),"Old stdout was promoted to the new private objective");
    blocked(view,plan,bookmark);

    phase=fixture.id+"/"+plan.activity+(fixture.guarded_arithmetic?
        " rejecting arithmetic before its guard through the actual worker":fixture.joint_local_int?
        " rejecting the joint counterexample through the actual worker":
        " rejecting fixed stdout through the actual worker");
    const auto fixed_request=request_for(view.environment,fixture.after,current,plan.fixed_source,first.standard_input);
    check(windows::compiler_runtime_request_allowed(current,fixed_request),
        "The fixed-source negative lacks a valid plan and would test only a request-shape guard");
    view.run(plan.fixed_source,first);check_feedback(view,false);
    const auto rejected_feedback=view.text(4);
    view.select_source();const auto rejected=view.saved();preserved(rejected);
    const auto rejected_draft=state::get_draft(rejected,key,current.initial_code,current.standard_input.value_or(""));
    check(!windows::compiler_puzzle_completed(rejected,fixture.after,current)&&
            windows::compiler_puzzle_case_count(rejected,fixture.after,current)==0,
        "A publicly correct fixed result acquired private credit");
    view.reopen();saved=view.saved();preserved(saved);
    same_draft(state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")),rejected_draft);
    check(view.text(4)==rejected_feedback,"The reopened private rejection lost its actual feedback");
    check_feedback(view,false);blocked(view,plan,bookmark);
    if(plan.no_plan_source) {
        phase=fixture.id+"/"+plan.activity+" showing a no-plan guide without claiming private execution";
        const auto no_plan_request=request_for(view.environment,fixture.after,current,
            *plan.no_plan_source,first.standard_input);
        check(!windows::compiler_runtime_request_allowed(current,no_plan_request),
            "The no-plan fixture unexpectedly formed a valid private local binding");
        view.replace(10,*plan.no_plan_source);view.input(first.standard_input);
        view.run(*plan.no_plan_source,first);check_feedback(view,false);
        saved=view.saved();preserved(saved);
        check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==0,
            "A source without a local plan earned private credit");
        // No private request fingerprint exists here. This checks visible
        // guidance only; persistent private rejection was checked above.
        blocked(view,plan,bookmark);
    }

    const bool optional=current.output_check->cases.size()>1;
    if(optional) {
        phase=fixture.id+"/"+plan.activity+" checking an optional input before the first case";
        const auto& trial=current.output_check->cases[1];
        view.replace(10,source);view.input(trial.standard_input);view.run(source,trial);
        saved=view.saved();preserved(saved);
        check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==1&&
                !view.text(4).starts_with("✓ Correcto"),"An optional input or its private trials completed the first case");
        blocked(view,plan,bookmark);view.reopen();saved=view.saved();preserved(saved);
        check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==1,
            "Reopening an optional-only real result fabricated completion or lost the partial case");
        blocked(view,plan,bookmark);
        // Remove the optional observation through the actual Reset button so
        // the next first-case success proves that no optional run is required.
        view.reset();saved=view.saved();preserved(saved);
        check(windows::compiler_puzzle_case_count(saved,fixture.after,current)==0,
            "Reset retained the optional observation before the first-case-only proof");
    }
    phase=fixture.id+"/"+plan.activity+" earning the reviewed goal with exactly one first case";
    view.replace(10,source);view.input(first.standard_input);view.run(source,first);
    saved=view.saved();preserved(saved);check_feedback(view,true);check_reader_goal(view,true);
    check(windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==1&&
            view.can_continue()&&
            view.text(34).find("Comprobación obligatoria: 1 de 1.")!=std::string::npos,
        "One real first case did not release the stronger goal or private trials became public cases");
    if(optional)check(view.text(34).find("Pruebas opcionales comprobadas: 0 de "+
        std::to_string(current.output_check->cases.size()-1)+'.')!=std::string::npos,
        "The first-case status treated optional input trials as requirements");
    if(reference.page==target.page)reference_visible(view,plan);
    if(final_page)view.finish();else view.navigate_to(bookmark);
    if(reference.page==bookmark)reference_visible(view,plan);
    view.navigate_to(target.page);view.select_source();
    const auto earned=view.saved();preserved(earned);
    const auto earned_draft=state::get_draft(earned,key,current.initial_code,current.standard_input.value_or(""));
    const auto earned_seals=achievement_seals(earned,prefix);
    check(!earned_seals.empty(),"The earned goal has no durable completion evidence");
    if(fixture.input_decisions&&current.input_check->allow_same_output) {
        // Query the real earned receipt against altered expectations only;
        // never relabel a worker result or save either altered contract.
        auto legacy_policy=current;legacy_policy.input_check->allow_same_output=false;
        auto altered_alternative=current;altered_alternative.input_check->probes.back().outputs.back()+=" ";
        check(!windows::compiler_puzzle_completed(earned,fixture.after,legacy_policy)&&
                !windows::compiler_puzzle_completed(earned,fixture.after,altered_alternative),
            "An actual opt-in receipt was reused under another mode or a changed accepted-output alternative");
    }
    if(fixture.guarded_arithmetic) {
        auto ordinary=current;
        for(auto& probe:ordinary.local_int_check->probes)probe.forbid_arithmetic=false;
        auto changed=current;changed.local_int_check->probes.back().replacement=-3;
        check(!windows::compiler_puzzle_completed(earned,fixture.after,ordinary)&&
                !windows::compiler_puzzle_completed(earned,fixture.after,changed),
            "The actual arithmetic receipt was reused under another policy or scenario");
    }
    if(fixture.joint_local_int&&fixture.id=="u3l5") {
        auto omitted=current;omitted.local_int_check->probes.pop_back();
        auto changed=current;
        changed.local_int_check->probes.at(joint_publication(fixture.id).original_probes)
            .additional_replacements.back().replacement=0;
        check(!windows::compiler_puzzle_completed(earned,fixture.after,omitted)&&
                !windows::compiler_puzzle_completed(earned,fixture.after,changed),
            "The admission receipt was reused with a missing scenario or a changed third variable");
    }
    view.reopen();saved=view.saved();preserved(saved);
    same_draft(state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")),earned_draft);
    check(windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==1&&
            achievement_seals(saved,prefix)==earned_seals&&view.can_continue(),
        "Closing/reopening lost the new first-case achievement or its independently stored historical receipt");
    check_feedback(view,true);
    if(optional) {
        phase=fixture.id+"/"+plan.activity+" retaining first-case credit after an optional run";
        const auto& trial=current.output_check->cases[1];
        view.input(trial.standard_input);view.run(source,trial);saved=view.saved();preserved(saved);
        check(windows::compiler_puzzle_completed(saved,fixture.after,current)&&
                windows::compiler_puzzle_case_count(saved,fixture.after,current)==2&&
                achievement_seals(saved,prefix)==earned_seals&&view.can_continue(),
            "An optional run replaced the first achievement or required its private trials as manual cases");
    }
    phase=fixture.id+"/"+plan.activity+" resetting only the new objective through its real button";
    const auto input_before_reset=state::get_draft(view.saved(),key,current.initial_code,current.standard_input.value_or("" )).input;
    view.reset();saved=view.saved();preserved(saved);
    check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==0&&
            state::get_draft(saved,key,current.initial_code,current.standard_input.value_or("")).input==input_before_reset,
        "Reset retained new credit or changed the independently saved stdin");
    blocked(view,plan,bookmark);view.reopen();saved=view.saved();preserved(saved);
    check(!windows::compiler_puzzle_completed(saved,fixture.after,current)&&
            windows::compiler_puzzle_case_count(saved,fixture.after,current)==0&&
            view.text(10)==current.initial_code&&view.text(4).empty(),
        "Reopening after Reset resurrected old credit or changed the reset source");
    blocked(view,plan,bookmark);
    if(local_extension)check(view.executions==3,
        "Each joint-variable owner requires exactly three actual button runs");
    std::cout<<"PASS real LessonView "<<fixture.id<<'/'<<plan.activity<<": "<<view.executions
        <<" actual button runs; "
        <<(local_extension?"historical single-variable receipt and ":"historical fixed stdout and ")
        <<(local_extension?"reference authorized by that receipt, later-page bookmark; ":
            (final_page?"same-page final reference / Finish gate; ":"later-page bookmark and reference proof; "))
        <<(fixture.guarded_arithmetic?"arithmetic extension blocks inherited credit; ":
            fixture.joint_local_int?"joint extension blocks inherited credit; ":"root adoption blocked; ")
        <<"private rejection retained; one first case opens reference; "
        <<(optional?"optional-before-first blocked/reset and optional-after-first retained; ":"")
        <<"separate receipts, draft/stdin/selection, actual reopen and scoped Reset.\n";
    if(fixture.joint_local_int)std::cout<<"PASS joint LessonView "<<fixture.id<<'/'<<plan.activity
        <<": original single-variable receipt retained; combined counterexample rejected; "
        <<"authentic solution earns one public case, separate current seal survives disk reopen, "
        <<"same-page reference and scoped Reset preserved; 3 real buttons plus "
        <<plan.joint_counterexamples.size()<<" direct counterexample captures.\n";
    if(fixture.guarded_arithmetic)std::cout<<"PASS guarded arithmetic LessonView "<<fixture.id<<'/'<<plan.activity
        <<": old three-probe receipt remains valid only for the old objective; inverted condition rejected; "
        <<"one actual first-case click with the authentic repair opens its reference; new policy/scenario seal, "
        <<"draft/input/selection and other receipts survive reopen and scoped Reset; three real button runs.\n";
}
} // namespace

int main(int argc,char** argv) {
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    started=GetTickCount64();
    const bool active_loops=argc>1&&std::string_view(argv[1])=="--active-loops";
    const bool input_decisions=active_loops||(argc>1&&std::string_view(argv[1])=="--input-decisions");
    const bool joint=argc>1&&std::string_view(argv[1])=="--joint-local-int";
    const bool guarded_arithmetic=argc==3&&std::string_view(argv[1])=="--guarded-arithmetic";
    if((input_decisions||joint)?(argc!=3&&argc!=4):(argc!=2&&argc!=3)) {
        std::cerr<<"Usage: local_dependency_view_tests <manifest.json> [lesson-id] | --joint-local-int <manifest.json> [lesson-id] | --guarded-arithmetic <manifest.json> | --input-decisions <manifest.json> [lesson-id] | --active-loops <manifest.json> [lesson-id]\n";return 2;
    }
    const auto apartment=OleInitialize(nullptr);
    if(FAILED(apartment))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);
    const auto rich=LoadLibraryW(L"Msftedit.dll");
    int result=0;
    try {
        check(rich!=nullptr,"Cannot load actual RichEdit controls");reader_theme::prepare_fonts();
        const auto manifest=std::filesystem::path(argv[joint||input_decisions||guarded_arithmetic?2:1]);
        const auto manifest_bytes=state::read_bytes(manifest);
        const auto fixtures=load_manifest(manifest,joint,input_decisions,guarded_arithmetic,active_loops);
        const auto filter=(input_decisions||joint)?(argc==4?std::string_view(argv[3]):std::string_view{}):
            argc==3&&!joint&&!guarded_arithmetic?std::string_view(argv[2]):std::string_view{};
        std::size_t completed=0,ordinal=0;
        for(const auto& fixture:fixtures) {
            if(!filter.empty()&&fixture.id!=filter)continue;
            std::cout<<"QA setup "<<fixture.id<<": authored nonfocal quiz answers evaluated by the core; "
                "nonfocal compilers observed by real native runs. These are not claimed as manual learner interactions.\n";
            for(const auto& plan:fixture.plans){
                ++ordinal;
                if(input_decisions)verify_input_decision_counterexamples(fixture,plan,ordinal);
                if(guarded_arithmetic)verify_guarded_arithmetic_sources(fixture,plan,ordinal);
                run_plan(fixture,plan,ordinal);++completed;
            }
        }
        check(completed>0&&(filter.empty()?completed==(guarded_arithmetic?1u:active_loops?2u:input_decisions?8u:joint?3u:16u):true),
            "The requested UI group is missing or incomplete");
        for(const auto& fixture:fixtures)
            check(state::sha256(state::read_bytes(fixture.before_path))==fixture.before_sha256&&
                    state::sha256(state::read_bytes(fixture.after_path))==fixture.after_sha256,
                "UI QA changed an authenticated curricular fixture");
        check(state::read_bytes(manifest)==manifest_bytes,"UI QA changed its manifest");
        std::cout<<"PASS local dependency View coverage: "<<completed
            <<" original owners; real Win32 controls and native worker, unchanged publications. "
            "No desktop visual or narration-playback acceptance is claimed.\n";
    }catch(const std::exception& error) {
        std::cerr<<"FAIL local dependency View: "<<error.what()<<'\n';result=1;
    }catch(const winrt::hresult_error& error) {
        std::cerr<<"FAIL local dependency View: "<<phase<<": "<<winrt::to_string(error.message())<<'\n';result=1;
    }
    if(rich)FreeLibrary(rich);OleUninitialize();return result;
}
