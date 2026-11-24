// Focused Windows authoring, capture and durable-receipt QA. Phase records in
// this file are deliberately simulated: real MSVC execution of every source
// variant belongs to local_dependency_view_tests. No personal profile is used.
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
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

constexpr std::string_view activity="p1u3l6_reparar_resto";
constexpr std::string_view reference="p1u3l6_ejemplo_resto";
constexpr std::string_view published_sha256=
    "9d7ca6b5d82b2dac1809ab01b8b308f9e81027c4c4c6ce796502b42cda7ca8ea";
constexpr std::string_view reference_sha256=
    "134ba6d6c94ba286a38d7db970af6df951668d3dee629225c977364c9561bbce";

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
const Block& owner(const Lesson& lesson) {
    const auto at=stable_block_position(lesson,activity);
    require(at.has_value(),"The authenticated guarded-arithmetic owner is missing");
    return lesson.pages.at(at->page).blocks.at(at->block);
}
std::string read_text(const std::filesystem::path& path) {
    const auto bytes=state::read_bytes(path);
    return std::string(bytes.begin(),bytes.end());
}
std::string canonical(std::string_view text) {
    return winrt::to_string(JsonObject::Parse(winrt::to_hstring(text)).Stringify());
}
std::string replace_once(std::string text,std::string_view before,std::string_view after) {
    const auto at=text.find(before);
    require(at!=text.npos,"A guarded-arithmetic fixture token is missing");
    text.replace(at,before.size(),after);
    return text;
}
struct Fixture {
    Lesson before,after,metadata;
    std::string source,fixed;
};
Fixture load_fixture(const std::filesystem::path& path) {
    const auto manifest=JsonObject::Parse(winrt::to_hstring(read_text(path)));
    require(manifest.GetNamedNumber(L"version")==1&&
            manifest.GetNamedString(L"kind")==L"guarded-local-arithmetic"&&
            manifest.GetNamedString(L"publishedCommit")==
                L"08a11877a2d2c0bf96a846660e3baea69e53f758",
        "The guarded-arithmetic manifest is not the pinned publication");
    const auto lessons=manifest.GetNamedArray(L"lessons");
    require(lessons.Size()==1,"Guarded-arithmetic QA must contain one complete lesson");
    const auto entry=lessons.GetObjectAt(0);
    require(entry.GetNamedString(L"id")==L"u3l6"&&
            entry.GetNamedString(L"before")==L"u3l6/before.json"&&
            entry.GetNamedString(L"after")==L"u3l6/after.json",
        "Guarded-arithmetic QA changed its bounded fixture paths");
    const auto before=read_text(path.parent_path()/"u3l6/before.json");
    const auto after=read_text(path.parent_path()/"u3l6/after.json");
    require(state::sha256(before)==published_sha256&&
            winrt::to_string(entry.GetNamedString(L"beforeSha256"))==published_sha256&&
            state::sha256(after)==winrt::to_string(entry.GetNamedString(L"afterSha256")),
        "Guarded-arithmetic QA changed an authenticated fixture byte");
    Fixture fixture{parse_lesson_json(before,"es"),parse_lesson_json(after,"es"),
        parse_lesson_progress_metadata(after,"es"),{}, {}};
    const auto plans=entry.GetNamedArray(L"plans");
    require(plans.Size()==1,"Guarded-arithmetic QA changed the original compiler owner");
    const auto plan=plans.GetObjectAt(0);
    require(winrt::to_string(plan.GetNamedString(L"activity"))==activity&&
            winrt::to_string(plan.GetNamedString(L"reference"))==reference&&
            plan.GetNamedNumber(L"originalProbeCount")==3,
        "Guarded-arithmetic QA lost the original observation prefix");
    fixture.fixed=winrt::to_string(plan.GetNamedString(L"fixedSource"));
    require(state::sha256(fixture.fixed)==winrt::to_string(plan.GetNamedString(L"fixedSourceSha256")),
        "The archived false-positive program changed");
    const auto at=stable_block_position(fixture.before,reference);
    require(at.has_value(),"The authentic guarded-arithmetic reference is missing");
    fixture.source=fixture.before.pages.at(at->page).blocks.at(at->block).code;
    require(state::sha256(fixture.source)==reference_sha256&&fixture.source!=fixture.fixed,
        "Guarded-arithmetic QA requires distinct reference and historical programs");

    const auto& old=owner(fixture.before);
    const auto& current=owner(fixture.after);
    const auto& projection=owner(fixture.metadata);
    require(old.local_int_check&&current.local_int_check&&
            old.local_int_check->probes.size()==3&&current.local_int_check->probes.size()==5&&
            projection.local_int_check==current.local_int_check&&
            std::equal(old.local_int_check->probes.begin(),old.local_int_check->probes.end(),
                current.local_int_check->probes.begin()),
        "Both loaders must preserve exactly the three published probes and two appended witnesses");
    for(std::size_t index=0;index<current.local_int_check->probes.size();++index) {
        const auto& probe=current.local_int_check->probes[index];
        require(probe.forbid_arithmetic==(index>=3),"A guarded flag changed the published prefix");
        if(index>=3)require(probe.variable=="capacidad"&&probe.replacement==(index==3?0:-2)&&
                probe.value_type==CppLocalValueType::integer&&!probe.after_update&&
                !probe.expected_increase&&probe.additional_replacements.empty(),
            "The two guarded observations changed their authored scope");
    }
    const auto restored=JsonObject::Parse(winrt::to_hstring(after));
    const auto probes=restored.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0).GetNamedArray(L"probes");
    probes.RemoveAtEnd();probes.RemoveAtEnd();
    require(winrt::to_string(restored.Stringify())==canonical(before)&&
            old.output_check&&current.output_check&&old.output_check->cases.size()==1&&
            current.output_check->cases.size()==1&&
            same_output_case_requirements(old.output_check->cases.front(),current.output_check->cases.front())&&
            draft_key("programming",fixture.before,old)==draft_key("programming",fixture.after,current)&&
            reference_objective_frame(fixture.after,current)==reference_objective_frame(fixture.metadata,projection),
        "The private suffix changed curriculum, a public case, a draft identity or a progress epoch");
    return fixture;
}

void rejects(std::string_view raw,std::string_view scenario) {
    for(const bool progress:{false,true}) {
        bool rejected=false;
        try {
            if(progress)(void)parse_lesson_progress_metadata(raw,"es");
            else (void)parse_lesson_json(raw,"es");
        }catch(const std::exception&) {rejected=true;}
        require(rejected,"Invalid guarded authoring accepted by "+
            std::string(progress?"progress":"full")+" loader: "+std::string(scenario));
    }
}
void accepts(std::string_view raw) {
    const auto full=parse_lesson_json(raw,"es");
    const auto progress=parse_lesson_progress_metadata(raw,"es");
    require(owner(full).local_int_check&&owner(full).local_int_check==owner(progress).local_int_check,
        "The two native readers disagree on an allowed guarded spelling");
}
void validate_schema(const Fixture& fixture) {
    const auto raw=canonical(fixture.after.raw_json);
    accepts(fixture.before.raw_json);accepts(raw);
    for(const std::string_view value:{"false","null","0","1","\"true\"","[]","{}"})
        rejects(replace_once(raw,"\"forbidArithmetic\":true","\"forbidArithmetic\":"+std::string(value)),
            "the option must be explicit true");
    for(const std::string_view duplicate:{
            "\"forbidArithmetic\":false,\"forbidArithmetic\":true",
            "\"forbidArithmetic\":true,\"forbidArithmetic\":true",
            "\"forbidArithm\\u0065tic\":true,\"forbidArithmetic\":true"})
        rejects(replace_once(raw,"\"forbidArithmetic\":true",duplicate),"duplicate guarded option");
    for(unsigned scenario=0;scenario<7;++scenario) {
        const auto changed=JsonObject::Parse(winrt::to_hstring(raw));
        const auto probe=changed.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0)
            .GetNamedArray(L"probes").GetObjectAt(3);
        switch(scenario) {
        case 0:probe.Insert(L"afterUpdate",JsonValue::CreateBooleanValue(false));break;
        case 1:probe.Insert(L"afterUpdate",JsonValue::CreateBooleanValue(true));break;
        case 2:probe.Insert(L"expectedIncrease",JsonValue::CreateNumberValue(1));break;
        case 3:probe.Insert(L"additionalReplacements",JsonArray{});break;
        case 4:probe.Insert(L"additionalReplacements",JsonArray::Parse(
            L"[{\"variable\":\"total\",\"replacement\":30}]"));break;
        case 5:probe.Insert(L"valueType",JsonValue::CreateStringValue(L"char"));
            probe.Insert(L"replacement",JsonValue::CreateStringValue(L"C"));break;
        case 6:probe.Insert(L"valueType",JsonValue::CreateStringValue(L"bool"));
            probe.Insert(L"replacement",JsonValue::CreateBooleanValue(false));break;
        }
        rejects(winrt::to_string(changed.Stringify()),"incompatible guarded field "+std::to_string(scenario));
    }
    rejects(replace_once(raw,"\"forbidArithmetic\":true","\"forbidArithmetics\":true"),
        "unknown guarded spelling");
    accepts(replace_once(raw,"\"forbidArithmetic\":true","\"valueType\":\"int\",\"forbidArithmetic\":true"));
    accepts(replace_once(raw,"\"forbidArithmetic\":true","\"forbidArithm\\u0065tic\":true"));
    std::cout<<"PASS guarded authoring: 18 invalid forms rejected by both loaders; explicit int, "
        "escaped spelling and the untouched legacy prefix accepted.\n";
}

CppRunRequest request_for(const Lesson& lesson,std::string source) {
    CppRunRequest request;
    request.request_id=31;request.revision=47;
    request.source_utf8=std::move(source);
    request.stdin_utf8=owner(lesson).standard_input.value_or("");
    request.project_key=draft_key("programming",lesson,owner(lesson));
    request.local_int_check=owner(lesson).local_int_check;
    return request;
}
CppRunResult simulated_observation(const CppRunRequest& request,const Block& block) {
    CppRunResult result;
    result.request_id=request.request_id;result.revision=request.revision;
    result.compilation.status=CppPhaseStatus::completed;result.compilation.exit_code=0;
    result.execution.status=CppPhaseStatus::completed;result.execution.exit_code=0;
    result.execution.stdout_bytes=block.output_check->cases.front().outputs.front();
    result.local_int_request_fingerprint=cpp_local_int_request_fingerprint(request);
    require(!result.local_int_request_fingerprint.empty(),"The controlled capture requires a derivable private plan");
    for(const auto& probe:request.local_int_check->probes) {
        const auto variant=cpp_local_int_override(request.source_utf8,probe);
        require(variant.has_value(),"The controlled capture lost a private source variant");
        CppLocalIntEvidence evidence{state::sha256(*variant),result.compilation,result.execution};
        evidence.execution.stdout_bytes=probe.expected_stdout.front();
        result.local_int_evidence.push_back(std::move(evidence));
    }
    return result;
}
std::string field(std::string_view value) {return std::to_string(value.size())+':'+std::string(value);}
std::string legacy_fingerprint(const CppRunRequest& request) {
    // The published scalar preimage, independent of the new guarded domain.
    auto frame=std::string("compiler-local-int-dependency-v1/")+field(request.project_key)+
        field(request.source_utf8)+field(request.stdin_utf8);
    for(const auto& probe:request.local_int_check->probes) {
        require(!probe.forbid_arithmetic&&!probe.after_update&&!probe.expected_increase&&
                probe.additional_replacements.empty()&&probe.value_type==CppLocalValueType::integer,
            "The legacy fingerprint fixture left its published scalar scope");
        const auto variant=cpp_local_int_override(request.source_utf8,probe);
        require(variant.has_value(),"A published scalar variant is no longer derivable");
        frame+=field(probe.variable)+field(std::to_string(probe.replacement))+field(state::sha256(*variant));
        for(const auto& output:probe.expected_stdout)frame+=field(output);
    }
    return state::sha256(frame);
}
std::string receipt_key(const Lesson& lesson,const Block& block) {
    return "compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+exercise_key(lesson,block)+'/'+
        state::sha256(reference_objective_frame(lesson,block))+"/local-int/v2/";
}
std::string legacy_seal(const std::string& key,const CppRunRequest& request) {
    auto frame=std::string("compiler-local-int-complete-v1/")+field(key)+field(compiler_program_frame(request));
    for(const auto& probe:request.local_int_check->probes) {
        frame+=field(probe.variable)+field(std::to_string(probe.replacement));
        for(const auto& output:probe.expected_stdout)frame+=field(output);
    }
    return state::sha256(frame);
}
void preserve(const state::StateData& after,const state::StateData& before) {
    for(const auto& [domain,values]:before.preferences)
        for(const auto& [key,value]:values)
            require(after.preferences.at(domain).at(key)==value,
                "A guarded attempt changed historical bytes or unrelated progress");
}
state::StateData reopen_isolated(const state::StateData& saved,std::wstring_view scenario) {
    const auto label=L"guarded-arithmetic-"+std::wstring(scenario)+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    const auto environment=resolve_windows_environment({ProfileId::Codex,label,false});
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),
        "Guarded persistence QA would open an existing or personal profile");
    initialize_profile_directories(environment.paths);
    {
        state::StateStore writer(environment.paths.root);
        require(writer.empty(),"A guarded QA profile unexpectedly has prior state");
        (void)writer.load();writer.save(saved);
    }
    state::StateStore reader(environment.paths.root);
    require(!reader.empty(),"Guarded persistence QA did not commit an active state file");
    return reader.load();
}

void validate_capture_and_receipts(const Fixture& fixture) {
    const auto& old=owner(fixture.before);
    const auto& current=owner(fixture.after);
    const auto old_request=request_for(fixture.before,fixture.fixed);
    const auto request=request_for(fixture.after,fixture.source);
    const auto historical_observation=simulated_observation(old_request,old);
    const auto observation=simulated_observation(request,current);
    const auto historical_capture=capture_cpp_execution(old_request,historical_observation);
    const auto capture=capture_cpp_execution(request,observation);
    require(historical_capture.local_int_check_correct&&capture.local_int_check_correct&&
            compiler_runtime_request_allowed(current,request)&&
            !compiler_runtime_request_allowed(current,old_request)&&
            cpp_local_int_request_fingerprint(old_request)==legacy_fingerprint(old_request),
        "The controlled capture lost legacy compatibility or guarded contract binding");
    auto plain=old;plain.local_int_check.reset();
    require(reference_objective_frame(fixture.before,old)==reference_objective_frame(fixture.before,plain)&&
            reference_objective_frame(fixture.before,old)!=reference_objective_frame(fixture.after,current),
        "An ordinary scalar epoch changed, or a guarded epoch reused its old identity");

    auto changed=request;
    changed.local_int_check->probes.back().forbid_arithmetic=false;
    auto changed_block=current;changed_block.local_int_check=changed.local_int_check;
    require(!cpp_local_int_request_fingerprint(changed).empty()&&
            cpp_local_int_request_fingerprint(changed)!=observation.local_int_request_fingerprint&&
            reference_objective_frame(fixture.after,changed_block)!=reference_objective_frame(fixture.after,current)&&
            !capture_cpp_execution(changed,observation).local_int_check_correct,
        "An ordinary probe flag within a guarded plan was omitted from binding");
    auto delayed=request;
    delayed.source_utf8=replace_once(delayed.source_utf8,"int capacidad = 6;","int capacidad;\n    capacidad = 6;");
    require(cpp_local_int_request_fingerprint(delayed).empty(),
        "A guarded probe accepted a target without an initialized declaration");

    for(unsigned scenario=0;scenario<12;++scenario) {
        auto invalid=observation;
        auto& last=invalid.local_int_evidence.back();
        switch(scenario) {
        case 0:last.execution.exit_code=91;break;
        case 1:last.compilation.exit_code=1;break;
        case 2:last.execution.status=CppPhaseStatus::timed_out;break;
        case 3:last.execution.stderr_bytes="unexpected stderr";break;
        case 4:last.execution.stdout_bytes="wrong output";break;
        case 5:last.instrumented_source_sha256="foreign";break;
        case 6:invalid.local_int_evidence.pop_back();break;
        case 7:invalid.local_int_request_fingerprint="foreign";break;
        case 8:std::swap(invalid.local_int_evidence[3],invalid.local_int_evidence[4]);break;
        case 9:++invalid.request_id;break;
        case 10:++invalid.revision;break;
        case 11:invalid.error_utf8="controlled infrastructure failure";break;
        }
        const auto rejected=capture_cpp_execution(request,invalid);
        require(!rejected.local_int_check_correct,"A malformed guarded capture was accepted: "+std::to_string(scenario));
        if(scenario==0)require(rejected.local_int_check_feedback.find("condición protectora")!=std::string::npos&&
                rejected.local_int_check_feedback.find("resto")!=std::string::npos,
            "A guarded failure lost its targeted pedagogical feedback");
    }

    state::StateData saved;
    saved.preferences["learning_progress"]["guarded_qa_other_course"]=std::int64_t{17};
    require(record_compiler_puzzle_case(saved,fixture.before,old,old_request,historical_capture,
                "Controlled historical three-probe capture"),
        "The controlled historical receipt was not created");
    const auto old_key=receipt_key(fixture.before,old);
    const auto old_seal=state::string_value(saved,"native_windows",old_key+"local-int/v1");
    require(old_seal==legacy_seal(old_key,old_request),"The published scalar completion seal changed");
    saved=reopen_isolated(saved,L"before");
    const auto historical=saved;
    validate_compiler_puzzle_state(saved,fixture.before,old);
    validate_compiler_puzzle_state(saved,fixture.after,current);
    prepare_compiler_puzzle_revisions(saved,fixture.after);
    require(compiler_puzzle_completed(saved,fixture.before,old)&&
            !compiler_puzzle_completed(saved,fixture.after,current)&&
            compiler_puzzle_case_count(saved,fixture.after,current)==0,
        "Historical three-probe credit was lost or promoted to a guarded witness");
    preserve(saved,historical);
    const auto before_attempt=saved;
    const auto fixed_request=request_for(fixture.after,fixture.fixed);
    require(!record_compiler_puzzle_case(saved,fixture.after,current,fixed_request,historical_capture,
                "Stale historical capture")&&saved==before_attempt,
        "A historical capture earned or changed the new guarded epoch");

    auto fixed_observation=simulated_observation(fixed_request,current);
    for(std::size_t index=3;index<fixed_observation.local_int_evidence.size();++index)
        fixed_observation.local_int_evidence[index].execution.exit_code=91;
    const auto failure=capture_cpp_execution(fixed_request,fixed_observation);
    require(!failure.local_int_check_correct&&
            !record_compiler_puzzle_case(saved,fixture.after,current,fixed_request,failure,
                "Controlled blocked arithmetic")&&saved==before_attempt&&
            record_compiler_puzzle_review(saved,fixture.after,current,fixed_request,failure,
                failure.local_int_check_feedback)&&
            !compiler_puzzle_review(saved,fixture.after,current,fixed_request).empty(),
        "A blocked arithmetic observation earned credit or lost its resumable review");
    preserve(saved,historical);
    require(record_compiler_puzzle_case(saved,fixture.after,current,request,capture,
                "Controlled complete guarded capture")&&
            compiler_puzzle_completed(saved,fixture.after,current)&&
            compiler_puzzle_case_count(saved,fixture.after,current)==1&&
            compiler_puzzle_completed(saved,fixture.before,old)&&
            !compiler_puzzle_completed(saved,fixture.after,changed_block),
        "A complete guarded witness failed to earn exactly the first public case");
    preserve(saved,historical);
    const auto key=receipt_key(fixture.after,current);
    const auto seal=state::string_value(saved,"native_windows",key+"local-int/v1");
    require(!seal.empty()&&key!=old_key&&seal!=old_seal,
        "Guarded and historical completions shared a seal or storage epoch");
    auto forged=saved;
    forged.preferences["native_windows"][key+"local-int/v1"]=old_seal;
    const auto forged_before=forged;
    bool rejected=false;
    try {validate_compiler_puzzle_state(forged,fixture.after,current);}
    catch(const std::exception&) {rejected=true;}
    require(rejected&&forged==forged_before,"A legacy seal substituted for a guarded receipt");

    const auto restored=reopen_isolated(saved,L"earned");
    require(restored==saved&&compiler_puzzle_completed(restored,fixture.after,current)&&
            compiler_puzzle_completed(restored,fixture.before,old)&&
            compiler_puzzle_case_count(restored,fixture.after,current)==1,
        "A real StateStore reopen lost an earned guarded or historical receipt");
    auto reset=restored;
    reset_compiler_puzzle(reset,fixture.after,current);
    prepare_compiler_puzzle_revisions(reset,fixture.after);
    require(!compiler_puzzle_completed(reset,fixture.after,current)&&
            compiler_puzzle_case_count(reset,fixture.after,current)==0&&
            compiler_puzzle_completed(reset,fixture.before,old)&&
            state::string_value(reset,"native_windows",key+"local-int/v1").empty(),
        "Reset revived guarded credit or revoked a historical scalar receipt");
    preserve(reset,historical);
    const auto reset_reopened=reopen_isolated(reset,L"reset");
    require(reset_reopened==reset&&!compiler_puzzle_completed(reset_reopened,fixture.after,current)&&
            compiler_puzzle_completed(reset_reopened,fixture.before,old),
        "Reset was not durable or historical completion was rewritten");
    std::cout<<"PASS guarded capture/state: 12 malformed controlled captures rejected; source/flag binding, "
        "legacy preimages, separate seals, one public case, rejected-attempt feedback, three real isolated "
        "disk reopens and epoch-only Reset. Phase observations were simulated; no student runtime is asserted.\n";
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    try {
        require(argc==2,"Usage: guarded_arithmetic_state_tests <manifest.json>");
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        const std::filesystem::path path=argv[1];
        const auto manifest=read_text(path);
        const auto fixture=load_fixture(path);
        validate_schema(fixture);
        validate_capture_and_receipts(fixture);
        require(read_text(path)==manifest&&read_text(path.parent_path()/"u3l6/before.json")==fixture.before.raw_json&&
                read_text(path.parent_path()/"u3l6/after.json")==fixture.after.raw_json,
            "Guarded state QA rewrote an authenticated source or manifest");
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL guarded arithmetic state: "<<error.what()<<'\n';return 1;
    }
}
