// Exact U2L10 local-role authoring, native MSVC observations and durable
// historical receipts. This executable uses only isolated QA profiles.
#include "core/compiler_flow.hpp"
#include "core/cpp_local_role_probe.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

constexpr std::array<std::string_view,2> owner_ids{"block_0004_0002","block_0007_0002"};
constexpr std::array<std::string_view,2> reference_ids{"p1u2l10_ejemplo_informe","block_0008_0001"};
constexpr std::array<std::string_view,2> reference_hashes{
    "aa4eb33b3a076e673d44fe5b4bcdbdb56705134f0eacfae81289f9fd90c805b8",
    "342e5e70b2e85f3292bcbb3eb35c85a7ed7ca474cd371b5bf6069dd03dc3977e"};

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
const Block& block_at(const Lesson& lesson,std::string_view identity) {
    const auto at=stable_block_position(lesson,identity);
    require(at.has_value(),"A published U2L10 block is missing");
    return lesson.pages.at(at->page).blocks.at(at->block);
}
std::string canonical(std::string_view raw) {
    return winrt::to_string(JsonObject::Parse(winrt::to_hstring(raw)).Stringify());
}
std::string file_bytes_as_string(const std::filesystem::path& path) {
    const auto bytes=state::read_bytes(path);
    return std::string(bytes.begin(),bytes.end());
}
std::string replace_once(std::string source,std::string_view before,std::string_view after) {
    const auto at=source.find(before);
    require(at!=source.npos,"An authenticated U2L10 source token is missing");
    source.replace(at,before.size(),after);
    return source;
}
std::string archived_source(const Lesson& lesson,std::string_view identity) {
    const auto root=JsonObject::Parse(winrt::to_hstring(lesson.raw_json));
    for(const auto& item:root.GetNamedObject(L"stateLayout").GetNamedArray(L"retired")) {
        const auto entry=item.GetObject();
        if(winrt::to_string(entry.GetNamedString(L"id"))==identity)
            return winrt::to_string(entry.GetNamedObject(L"block").GetNamedString(L"code"));
    }
    throw std::runtime_error("An authenticated U2L10 retired source is missing");
}
std::vector<std::string> alternatives(std::string output) {
    return {output,output+'\n'};
}
CppLocalRole make_role(std::string id,CppLocalRoleValueType type,double initial,double replacement,
        std::string output,bool after=false,std::optional<std::int32_t> increase=std::nullopt,
        std::string before={}) {
    return {std::move(id),type,initial,{{replacement,alternatives(std::move(output)),
        after,increase,std::move(before)}}};
}
std::array<CppLocalRoleCheck,2> expected_checks() {
    using Type=CppLocalRoleValueType;
    return {{
        {{make_role("first",Type::real,1.5,2.5,"Puesto 03\nMesas: 6\nCable: 4.75 m"),
          make_role("second",Type::real,2.25,4.25,"Puesto 03\nMesas: 6\nCable: 5.75 m"),
          make_role("total",Type::real,3.75,9.25,"Puesto 03\nMesas: 6\nCable: 9.25 m")}},
        {{make_role("initial",Type::integer,7,11,"Modulo 02\nInicio: 11\nAhora: 14"),
          make_role("current",Type::integer,7,2,"Modulo 02\nInicio: 7\nAhora: 12",true,3),
          make_role("saved",Type::integer,7,19,"Modulo 02\nInicio: 19\nAhora: 10",false,std::nullopt,"current")}}
    }};
}
bool same_public_case(const OutputCheck& left,const OutputCheck& right) {
    return left.input_comparison==right.input_comparison&&left.cases.size()==1&&right.cases.size()==1&&
        same_output_case_requirements(left.cases.front(),right.cases.front())&&
        left.cases.front().explanation==right.cases.front().explanation;
}
void validate_fixture(const Lesson& original,const Lesson& reviewed,const Lesson& metadata,
                      const std::filesystem::path& directory) {
    constexpr std::string_view digest="e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0";
    require(state::sha256(original.raw_json)==digest,
        "Local-role before.json is not the exact main 2ca32b23 publication");
    const auto root=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    require(root.GetNamedArray(L"compilerLocalRoleChecks").Size()==2,
        "U2L10 role QA needs exactly the two authored owners");
    const auto without=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    without.Remove(L"compilerLocalRoleChecks");
    require(winrt::to_string(without.Stringify())==canonical(original.raw_json)&&
            original.compiler_puzzles_required&&reviewed.compiler_puzzles_required&&
            metadata.compiler_puzzles_required&&original.pages.size()==11&&reviewed.pages.size()==11,
        "Local-role QA changed existing curriculum, history or the visible completion policy");
    const auto layout=root.GetNamedObject(L"stateLayout");
    require(layout.GetNamedArray(L"editorial").Size()==54&&
            layout.GetNamedArray(L"choiceRevisions").Size()==1&&
            layout.GetNamedArray(L"retired").Size()==6&&
            layout.GetNamedArray(L"retirementHistory").Size()==1,
        "Local-role QA lost the authenticated main editorial revision or archived publication");
    for(std::size_t page=0;page<original.pages.size();++page) {
        require(original.pages[page].blocks.size()==reviewed.pages[page].blocks.size(),
            "Local-role QA moved an original block");
        for(std::size_t index=0;index<original.pages[page].blocks.size();++index) {
            const auto& old=original.pages[page].blocks[index];
            const auto& now=reviewed.pages[page].blocks[index];
            const bool checked=std::ranges::find(owner_ids,now.stable_id)!=owner_ids.end();
            require(canonical(old.raw_json)==canonical(now.raw_json)&&old.stable_id==now.stable_id&&
                    old.address.page==now.address.page&&old.address.block==now.address.block&&
                    !old.local_role_check&&now.local_role_check.has_value()==checked,
                "Local-role QA changed another payload, stable address or objective");
        }
    }
    const auto expected=expected_checks();
    for(std::size_t index=0;index<owner_ids.size();++index) {
        const auto& old=block_at(original,owner_ids[index]);
        const auto& now=block_at(reviewed,owner_ids[index]);
        const auto& progress=block_at(metadata,owner_ids[index]);
        require(now.local_role_check&&*now.local_role_check==expected[index]&&
                now.local_role_check==progress.local_role_check&&
                old.initial_code==now.initial_code&&now.initial_code==progress.initial_code&&
                old.standard_input==now.standard_input&&now.standard_input==progress.standard_input&&
                old.output_check&&now.output_check&&progress.output_check&&
                same_public_case(*old.output_check,*now.output_check)&&
                same_public_case(*now.output_check,*progress.output_check)&&
                old.output_check->cases.front().standard_input.empty()&&
                old.address.page==(index==0?4u:7u)&&old.address.block==2&&
                draft_key("programming",original,old)==draft_key("programming",reviewed,now)&&
                state::sha256(block_at(original,reference_ids[index]).code)==reference_hashes[index],
            "Full/progress U2L10 models differ or the single public case/draft address changed");
    }
    const std::array<std::pair<std::string_view,std::string_view>,3> archives{{
        {"block_0005_0001","5e1ddfa0a4bb94990baf862a34d0e1883cb4ded5e9911d59ce6ac5a1e6b6d617"},
        {"block_0009_0003","5ee352e21ab022e4e9f726ee99ff7e8575ef60eb28c2312a6c738f16d97903e8"},
        {"block_0010_0003","5aa2b60aef1f6366931ef348e06a67db596ec4c54fcce033a7ecc3671d6eb8c6"}
    }};
    for(const auto& [identity,hash]:archives)
        require(state::sha256(archived_source(original,identity))==hash&&
                archived_source(original,identity)==archived_source(reviewed,identity),
            "An authentic U2L10 archived reference was changed");
    const auto manifest=JsonObject::Parse(winrt::to_hstring(file_bytes_as_string(directory/"manifest.json")));
    const auto files=manifest.GetNamedObject(L"files");
    require(winrt::to_string(manifest.GetNamedString(L"publishedCommit"))==
                "2ca32b2337178d1effa3a2c81759e2493cd746d0"&&
            winrt::to_string(files.GetNamedString(L"before.json"))==digest&&
            winrt::to_string(files.GetNamedString(L"after.json"))==state::sha256(reviewed.raw_json),
        "The local-role fixture manifest does not authenticate the loaded sources");
    std::cout<<"PASS U2L10 role fixture: exact main source, two original single-case editors, "
        "54 editorial entries, archived references, stable addresses and unchanged public payloads.\n";
}

std::string with_contract(const Lesson& original,const JsonArray& checks) {
    const auto object=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    object.Insert(L"compilerLocalRoleChecks",checks);
    return winrt::to_string(object.Stringify());
}
void reject_raw(std::string_view raw,std::string_view label) {
    for(const bool progress:{false,true}) {
        bool rejected=false;
        try {
            if(progress)(void)parse_lesson_progress_metadata(raw,"es");
            else (void)parse_lesson_json(raw,"es");
        }catch(const std::exception&) {rejected=true;}
        require(rejected,"Invalid local-role contract accepted by "+
            std::string(progress?"progress":"full")+" loader: "+std::string(label));
    }
}
void validate_schema(const Lesson& original,const Lesson& reviewed) {
    const auto root=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    const auto compact=winrt::to_string(root.GetNamedArray(L"compilerLocalRoleChecks").Stringify());
    for(unsigned scenario=0;scenario<20;++scenario) {
        const auto checks=JsonArray::Parse(winrt::to_hstring(compact));
        const auto doubles=checks.GetObjectAt(0).GetNamedArray(L"roles");
        const auto integers=checks.GetObjectAt(1).GetNamedArray(L"roles");
        const auto real=doubles.GetObjectAt(0);
        const auto first=integers.GetObjectAt(0);
        const auto updated=integers.GetObjectAt(1).GetNamedArray(L"probes").GetObjectAt(0);
        const auto snapshot=integers.GetObjectAt(2).GetNamedArray(L"probes").GetObjectAt(0);
        switch(scenario) {
        case 0:real.Insert(L"initialValue",JsonValue::CreateBooleanValue(true));break;
        case 1:first.Insert(L"initialValue",JsonValue::CreateNumberValue(7.5));break;
        case 2:first.Insert(L"initialValue",JsonValue::CreateNumberValue(2147483648.0));break;
        case 3:real.Insert(L"initialValue",JsonValue::CreateNumberValue(1000001.0));break;
        case 4:real.Insert(L"valueType",JsonValue::CreateStringValue(L"float"));break;
        case 5:updated.Insert(L"afterUpdate",JsonValue::CreateBooleanValue(false));break;
        case 6:updated.Remove(L"expectedIncrease");break;
        case 7:updated.Insert(L"expectedIncrease",JsonValue::CreateNumberValue(0));break;
        case 8:updated.Insert(L"beforeUpdateOf",JsonValue::CreateStringValue(L"initial"));break;
        case 9:updated.Insert(L"replacement",JsonValue::CreateNumberValue(0));break;
        case 10:snapshot.Insert(L"beforeUpdateOf",JsonValue::CreateStringValue(L"missing"));break;
        case 11:snapshot.Insert(L"beforeUpdateOf",JsonValue::CreateStringValue(L"saved"));break;
        case 12:snapshot.Insert(L"beforeUpdateOf",JsonValue::CreateStringValue(L"initial"));break;
        case 13:integers.GetObjectAt(2).Insert(L"valueType",JsonValue::CreateStringValue(L"double"));break;
        case 14:real.GetNamedArray(L"probes").GetObjectAt(0).Insert(L"replacement",JsonValue::CreateNumberValue(1.5));break;
        case 15:real.GetNamedArray(L"probes").GetObjectAt(0).Insert(L"outputs",JsonArray::Parse(L"[\"\"]"));break;
        case 16:real.Insert(L"id",JsonValue::CreateStringValue(L"invalid/name"));break;
        case 17:real.Insert(L"variable",JsonValue::CreateStringValue(L"tramo_a"));break;
        case 18:doubles.Append(JsonObject::Parse(real.Stringify()));break;
        case 19:real.GetNamedArray(L"probes").GetObjectAt(0).Insert(L"expectedIncrease",JsonValue::CreateNumberValue(3));break;
        }
        reject_raw(with_contract(original,checks),"scenario "+std::to_string(scenario));
    }
    const auto raw=with_contract(original,JsonArray::Parse(winrt::to_hstring(compact)));
    for(const auto& [before,after]:std::array<std::pair<std::string_view,std::string_view>,4>{{
        {"\"compilerLocalRoleChecks\":","\"compilerLocalRoleChecks\":[],\"compilerLocalRoleChecks\":"},
        {"\"id\":\"first\"","\"id\":\"wrong\",\"id\":\"first\""},
        {"\"initialValue\":1.5","\"initialValue\":99,\"initialValue\":1.5"},
        {"\"beforeUpdateOf\":\"current\"","\"beforeUpdateOf\":\"wrong\",\"beforeUpdateOf\":\"current\""}
    }})reject_raw(replace_once(raw,before,after),"duplicate raw member");
    // Integral JSON floating notation is legitimate; it must not be mistaken
    // for a bool or force an integer token outside the new numeric contract.
    const auto integral=replace_once(raw,"\"initialValue\":7","\"initialValue\":7.0");
    const auto full=parse_lesson_json(integral,"es");
    const auto progress=parse_lesson_progress_metadata(integral,"es");
    require(block_at(full,owner_ids[1]).local_role_check==block_at(reviewed,owner_ids[1]).local_role_check&&
            block_at(progress,owner_ids[1]).local_role_check==block_at(full,owner_ids[1]).local_role_check,
        "Integral floating JSON notation changed a local-role value");
    std::cout<<"PASS U2L10 role authoring: both loaders reject 24 malformed contracts; "
        "double bounds, int32, snapshots and integral numeric notation retained.\n";
}

bool has_role_witness(const state::StateData& data) {
    const auto domain=data.preferences.find("native_windows");
    if(domain==data.preferences.end())return false;
    return std::ranges::any_of(domain->second,[](const auto& item) {
        const auto text=std::get_if<std::string>(&item.second);
        return item.first.find("/local-roles/v1/")!=item.first.npos&&
            item.first.ends_with("/witness")&&text&&!text->empty();
    });
}
void preserve_preferences(const state::StateData& current,const state::StateData& before) {
    for(const auto& [domain,values]:before.preferences)
        for(const auto& [key,value]:values)
            require(current.preferences.at(domain).at(key)==value,
                "Local-role execution changed an earlier receipt or unrelated preference");
}
std::string fixed_report(const std::string& source,std::string_view output) {
    const auto begin=source.find("    std::cout");
    const auto end=source.find("    return 0;",begin);
    require(begin!=source.npos&&end!=source.npos,"The exact reference report boundary is missing");
    auto fixed=source;
    fixed.replace(begin,end-begin,"    std::cout << "+
        winrt::to_string(JsonValue::CreateStringValue(winrt::to_hstring(output)).Stringify())+";\n");
    return fixed;
}
void run_owner(const Lesson& original,const Lesson& reviewed,const Lesson& metadata,std::size_t index) {
    const auto& old=block_at(original,owner_ids[index]);
    const auto& current=block_at(reviewed,owner_ids[index]);
    const auto& projection=block_at(metadata,owner_ids[index]);
    const auto& check=*current.local_role_check;
    const auto source=block_at(original,reference_ids[index]).code;
    const auto visible=current.output_check->cases.front().outputs.front();
    const auto fixed=fixed_report(source,visible);
    require(fixed!=source,"Historical stdout and corrected local-role sources must differ");
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"local-roles-u2l10-"+std::to_wstring(index)+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),
        "Local-role QA would open an existing or personal profile");
    initialize_profile_directories(environment.paths);
    std::uint64_t sequence=70000+static_cast<std::uint64_t>(index)*100;
    const auto request_for=[&](std::string program,bool checked=true) {
        CppRunRequest request;
        request.request_id=++sequence;request.revision=sequence+1000;
        request.project_key=draft_key("programming",reviewed,current);
        request.source_utf8=std::move(program);
        request.projects_directory=environment.paths.projects;
        request.compile_timeout=std::chrono::seconds(60);
        request.execution_timeout=std::chrono::seconds(8);
        if(checked)request.local_role_check=check;
        return request;
    };
    const auto execute=[&](const CppRunRequest& request,std::string_view output,
                           std::string_view stderr_expected=std::string_view{}) {
        const auto result=run_cpp(request);
        require(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
                cpp_local_int_canonical_stdout(result.execution.stdout_bytes)==output&&
                result.execution.stderr_bytes==stderr_expected,
            "The real public U2L10 local-role observation failed: "+result.error_utf8+
                result.compilation.stderr_bytes+result.execution.stderr_bytes);
        require(file_bytes_as_string(result.run_directory/L"main.cpp")==request.source_utf8&&
                file_bytes_as_string(result.run_directory/L"stdin.bin")==request.stdin_utf8&&
                file_bytes_as_string(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
                file_bytes_as_string(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "Private local-role trials changed the original public source, input or console artifacts");
        return result;
    };
    const auto private_proof=[&](const CppRunRequest& request,const CppRunResult& result,
                                std::string_view stderr_expected=std::string_view{}) {
        const auto captured=capture_cpp_execution(request,result);
        require(compiler_runtime_request_allowed(current,request)&&
                captured.local_role_check_correct&&result.local_role_evidence&&
                !cpp_local_role_request_fingerprint(request).empty()&&
                captured.local_role_request_fingerprint==cpp_local_role_request_fingerprint(request)&&
                result.local_role_request_fingerprint==cpp_local_role_request_fingerprint(request),
            "A correct U2L10 program lacks its authenticated role observations");
        const auto plan=cpp_local_role_plan(request.source_utf8,check);
        const auto& evidence=*result.local_role_evidence;
        require(plan&&evidence.binding.size()==check.roles.size()&&evidence.observations.size()==3&&
                evidence.request_fingerprint==cpp_local_role_request_fingerprint(request)&&
                evidence.binding_fingerprint.size()==64,
            "A local-role observation lost its complete request or fixed role binding");
        std::size_t offset=0;
        for(std::size_t role_index=0;role_index<check.roles.size();++role_index) {
            require(evidence.binding[role_index]<plan->candidates.size(),"A selected role candidate is out of range");
            const auto& role=check.roles[role_index];
            for(std::size_t probe_index=0;probe_index<role.probes.size();++probe_index) {
                const auto& probe=role.probes[probe_index];
                const auto& observation=evidence.observations.at(offset++);
                std::optional<std::size_t> other;
                if(!probe.before_update_of.empty()) {
                    const auto target=std::ranges::find(check.roles,probe.before_update_of,&CppLocalRole::id);
                    require(target!=check.roles.end(),"A snapshot relation lost its authored role");
                    other=evidence.binding[static_cast<std::size_t>(target-check.roles.begin())];
                    require(*other!=evidence.binding[role_index],"A snapshot is the same object as its updated role");
                }
                const auto variant=cpp_local_role_variant(*plan,check,role_index,probe_index,
                    evidence.binding[role_index],other);
                require(variant&&observation.role_index==role_index&&observation.probe_index==probe_index&&
                        observation.instrumented_source_sha256==state::sha256(*variant)&&
                        observation.compilation.succeeded()&&observation.execution.succeeded()&&
                        observation.execution.stderr_bytes==stderr_expected&&
                        cpp_local_role_expected_output(probe,observation.execution.stdout_bytes),
                    "Private role evidence is not bound to its exact source, candidate and independent oracle");
            }
        }
        return captured;
    };

    state::StateData saved;
    state::prepare_lesson_state(saved,original);
    saved.preferences["learning_progress"]["other_course_unchanged"]=std::int64_t{42};
    saved.preferences["native_windows"]["unrelated_reader_setting"]=std::string("keep");
    std::vector<std::pair<std::string,state::Draft>> drafts;
    for(const auto& page:original.pages)for(const auto& editor:page.blocks)
        if(editor.type==BlockType::cpp_compiler) {
            state::Draft draft;
            draft.source=editor.initial_code+"// Independent unfinished learner draft.\n";
            draft.input="  saved draft input\n";
            draft.selection_start=9;draft.selection_end=27;
            const auto key=draft_key("programming",original,editor);
            state::put_draft(saved,key,draft);drafts.emplace_back(key,std::move(draft));
        }
    const auto verify_drafts=[&](const state::StateData& data) {
        for(const auto& [key,expected]:drafts) {
            const auto actual=state::get_draft(data,key,"");
            require(actual.has_saved&&actual.source==expected.source&&actual.input==expected.input&&
                    actual.selection_start==expected.selection_start&&actual.selection_end==expected.selection_end,
                "Local-role checking, reopening or reset changed an independent learner draft");
        }
    };
    const auto old_request=request_for(fixed,false);
    const auto old_result=execute(old_request,visible);
    const auto old_capture=capture_cpp_execution(old_request,old_result);
    require(record_compiler_puzzle_case(saved,original,old,old_request,old_capture,
                "Real historical U2L10 stdout from a fixed-report program")&&
            compiler_puzzle_completed(saved,original,old)&&
            compiler_puzzle_case_count(saved,original,old)==1&&!has_role_witness(saved),
        "A genuine historical U2L10 stdout receipt was not recorded");
    const auto legacy=saved;
    {
        state::StateStore writer(environment.paths.root);
        require(writer.empty(),"The local-role QA profile already contains state");
        (void)writer.load();writer.save(saved);
    }
    {
        state::StateStore reader(environment.paths.root);
        require(!reader.empty(),"The historical local-role QA save is missing");
        saved=reader.load();
    }
    state::validate_lesson_state(saved,reviewed);
    validate_compiler_puzzle_state(saved,reviewed,current);
    validate_compiler_puzzle_state(saved,metadata,projection);
    require(saved==legacy&&!compiler_puzzle_completed(saved,reviewed,current)&&
            !compiler_puzzle_completed(saved,metadata,projection)&&
            compiler_puzzle_case_count(saved,reviewed,current)==0&&!has_role_witness(saved),
        "Adopting roles promoted or changed an old stdout-only receipt");
    auto replay_request=old_request;replay_request.local_role_check=check;
    const auto replay=capture_cpp_execution(replay_request,old_result);
    require(compiler_runtime_request_allowed(current,replay_request)&&
            replay.source_revision==replay_request.revision&&
            replay.local_role_request_fingerprint==cpp_local_role_request_fingerprint(replay_request)&&
            !replay.local_role_check_correct&&
            !record_compiler_puzzle_case(saved,reviewed,current,replay_request,replay,
                "Same request ID and revision still lack role observations")&&saved==legacy,
        "A replay of actual historical stdout acquired new role evidence");
    const auto request=request_for(source);
    const auto result=execute(request,visible);
    const auto proof=private_proof(request,result);

    // Counterexamples start from the genuine result, but these deliberately
    // altered phase records are capture tests, not claims of real timeouts.
    for(unsigned scenario=0;scenario<11;++scenario) {
        auto changed=result;
        auto changed_request=request;
        auto& evidence=*changed.local_role_evidence;
        switch(scenario) {
        case 0:evidence.observations[0].instrumented_source_sha256=state::sha256("other source");break;
        case 1:evidence.binding[0]=std::numeric_limits<std::uint32_t>::max();break;
        case 2:evidence.observations[0].role_index=1;break;
        case 3:evidence.observations[0].probe_index=1;break;
        case 4:changed.local_role_request_fingerprint=state::sha256("other request");break;
        case 5:evidence.request_fingerprint=state::sha256("other evidence request");break;
        case 6:evidence.binding_fingerprint=state::sha256("other binding");break;
        case 7:evidence.observations.pop_back();break;
        case 8:evidence.observations[0].compilation.status=CppPhaseStatus::cancelled;break;
        case 9:evidence.observations[0].execution.status=CppPhaseStatus::timed_out;break;
        case 10:changed_request.local_role_check->roles[0].probes[0].replacement+=1;break;
        }
        const auto captured=capture_cpp_execution(changed_request,changed);
        require(!captured.local_role_check_correct&&
                !record_compiler_puzzle_case(saved,reviewed,current,changed_request,captured,
                    "Unbound or incomplete local-role evidence")&&saved==legacy,
            "Altered local-role evidence earned a receipt: "+std::to_string(scenario));
    }
    require(record_compiler_puzzle_case(saved,reviewed,current,request,proof,
                "One public check and three independent local-role observations")&&
            compiler_puzzle_completed(saved,reviewed,current)&&
            compiler_puzzle_case_count(saved,reviewed,current)==1&&has_role_witness(saved)&&
            compiler_puzzle_completed(saved,original,old)&&compiler_puzzle_case_count(saved,original,old)==1,
        "A correct source distinct from the historical program did not earn its own role receipt");
    preserve_preferences(saved,legacy);
    std::cout<<"PASS U2L10 published local roles "<<owner_ids[index]<<": three source-bound observations.\n";

    const auto positive=[&](std::string_view label,const std::string& program,bool newline=false,
                            bool reversed=false,std::string_view stderr_expected=std::string_view{}) {
        const auto candidate=request_for(program);
        const auto observed=execute(candidate,visible+(newline?"\n":""),stderr_expected);
        const auto captured=private_proof(candidate,observed,stderr_expected);
        if(reversed) {
            const auto& binding=observed.local_role_evidence->binding;
            const auto selected=[&](std::string_view id) {
                const auto at=std::ranges::find(check.roles,id,&CppLocalRole::id);
                require(at!=check.roles.end(),"The reversed copy lost an authored role ID");
                return binding.at(static_cast<std::size_t>(at-check.roles.begin()));
            };
            require(binding.size()==3&&selected("initial")==selected("saved")&&
                    selected("current")!=selected("saved"),
                "The reversed copy did not share the initial/saved candidate while keeping current independent");
        }
        auto independent=legacy;
        require(record_compiler_puzzle_case(independent,reviewed,current,candidate,captured,label)&&
                compiler_puzzle_completed(independent,reviewed,current)&&
                compiler_puzzle_case_count(independent,reviewed,current)==1&&has_role_witness(independent),
            "An equivalent local-role source needed an extra public check");
        preserve_preferences(independent,legacy);verify_drafts(independent);
        std::cout<<"PASS U2L10 equivalent local roles "<<owner_ids[index]<<": "<<label<<'\n';
    };
    if(index==0) {
        positive("authenticated archived brace reference",archived_source(original,"block_0009_0003"));
        positive("reordered grouped const auto expressions, fixed2, final LF and innocuous stderr",
            "#include <iostream>\n#include <iomanip>\nint main(){\n"
            "const auto largo(9.0/4.0), corto(3.0/2.0), juntos(corto+largo);\n"
            "const double sin_uso=1.5; (void)sin_uso;\n"
            "std::cout<<std::fixed<<std::setprecision(2)<<\"Puesto 03\\nMesas: 6\\nCable: \"<<juntos<<\" m\\n\";\n"
            "std::cerr<<\"diagnostico de prueba\"; return 0;}\n",true,false,"diagnostico de prueba");
        positive("free names and separate assignments after grouped declarations",
            "#include <iostream>\nint main(){double este{},oeste{},longitud{};\n"
            "este=1.5; oeste=2.25; longitud=este+oeste;\n"
            "std::cout<<\"Puesto 03\\nMesas: 6\\nCable: \"<<longitud<<\" m\";}\n");
    } else {
        positive("authenticated archived braces and ordinary addition",archived_source(original,"block_0010_0003"));
        positive("reversed copy, const auto opening, parentheses and literal return",
            "#include <iostream>\nint main(){const auto apertura{7}; auto disponibles(apertura);\n"
            "disponibles+=3; std::cout<<\"Modulo 02\\nInicio: \"<<apertura<<\"\\nAhora: \"<<disponibles<<'\\n';}\n",
            true,true);
        positive("free names and grouped dependent copy with an equivalent increase",
            "#include <iostream>\nint main(){int existencias{7}, registro{existencias}, entregados{1+2};\n"
            "existencias=existencias+entregados; std::cout<<\"Modulo 02\\nInicio: \"<<registro<<\"\\nAhora: \"<<existencias;}\n");
    }
    const auto negative=[&](std::string_view label,const std::string& program) {
        const auto candidate=request_for(program);
        require(compiler_runtime_request_allowed(current,candidate),
            "A local-role counterexample failed an unrelated request-shape gate");
        const auto observed=execute(candidate,visible);
        const auto captured=capture_cpp_execution(candidate,observed);
        const auto unchanged=saved;
        require(!observed.local_role_evidence&&!captured.local_role_check_correct&&
                captured.local_role_request_fingerprint==cpp_local_role_request_fingerprint(candidate)&&
                !record_compiler_puzzle_case(saved,reviewed,current,candidate,captured,label)&&saved==unchanged,
            "A publicly correct fixed or uncopied U2L10 result earned role credit: "+std::string(label));
        std::cout<<"PASS U2L10 real role counterexample rejected "<<owner_ids[index]<<": "<<label<<'\n';
    };
    negative("entire report fixed despite retained locals",fixed);
    if(index==0) {
        negative("first length ignored",replace_once(source,"tramo_a + tramo_b","1.5 + tramo_b"));
        negative("second length ignored",replace_once(source,"tramo_a + tramo_b","tramo_a + 2.25"));
        negative("stored total ignored at print time",replace_once(source,
            "<< cable_total_m <<","<< (tramo_a + tramo_b) <<"));
    } else {
        negative("opening printed before update without a saved copy",
            "#include <iostream>\nint main(){int libros=7; int devolucion=3;\n"
            "std::cout<<\"Modulo 02\\nInicio: \"<<libros; libros+=devolucion;\n"
            "std::cout<<\"\\nAhora: \"<<libros;}\n");
        negative("saved opening is an unrelated literal",replace_once(source,
            "int al_inicio = libros;","int al_inicio = 7;"));
        negative("late copy repaired only while printing",
            "#include <iostream>\nint main(){int libros=7; int devolucion=3; libros+=devolucion;\n"
            "int copia=libros; std::cout<<\"Modulo 02\\nInicio: \"<<(copia-3)<<\"\\nAhora: \"<<libros;}\n");
        auto wrong_increase=replace_once(source,"libros += devolucion;","libros += devolucion + 2;");
        wrong_increase=replace_once(wrong_increase,"<< libros;","<< (libros - 2);");
        negative("real plus five concealed by minus two at output",wrong_increase);
    }

    auto missing=proof;missing.local_role_request_fingerprint.clear();
    const auto before_missing=saved;
    require(!record_compiler_puzzle_case(saved,reviewed,current,request,missing,
                "Private role success without its request fingerprint")&&saved==before_missing,
        "An unbound role-correctness flag earned a receipt");
    std::stop_source stop;stop.request_stop();
    const auto cancelled_request=request_for(source);
    const auto cancelled=run_cpp(cancelled_request,stop.get_token());
    const auto cancelled_capture=capture_cpp_execution(cancelled_request,cancelled);
    require(!cancelled.compilation.succeeded()&&!cancelled_capture.local_role_check_correct&&
            !record_compiler_puzzle_case(saved,reviewed,current,cancelled_request,cancelled_capture,
                "Pre-cancelled local-role check")&&saved==before_missing,
        "A cancelled local-role request changed an earned result");
    const auto edited=request_for(source+"// New unfinished source.\n");
    auto draft_only=legacy;
    const auto key=draft_key("programming",reviewed,current);
    state::Draft new_draft;new_draft.source=edited.source_utf8;new_draft.input="saved replacement input";
    new_draft.selection_start=3;new_draft.selection_end=21;
    state::put_draft(draft_only,key,new_draft);
    const auto before_invalidation=draft_only;
    invalidate_compiler_puzzle_attempt(draft_only,reviewed,current,edited);
    require(draft_only==before_invalidation&&compiler_puzzle_completed(draft_only,original,old)&&
            !compiler_puzzle_completed(draft_only,reviewed,current),
        "Invalidating an unearned role draft changed its code or a historical receipt");
    invalidate_compiler_puzzle_attempt(saved,reviewed,current,edited);
    require(compiler_puzzle_completed(saved,reviewed,current),
        "An edit revoked an already earned role achievement");
    preserve_preferences(saved,legacy);verify_drafts(saved);
    {
        state::StateStore writer(environment.paths.root);
        require(!writer.empty()&&writer.load()==legacy,
            "Uncommitted role QA changed durable historical state");
        writer.save(saved);
    }
    state::StateStore reader(environment.paths.root);
    require(!reader.empty(),"The new local-role receipt was not saved");
    const auto restored=reader.load();
    state::validate_lesson_state(restored,reviewed);
    validate_compiler_puzzle_state(restored,reviewed,current);
    validate_compiler_puzzle_state(restored,metadata,projection);
    require(restored==saved&&compiler_puzzle_completed(restored,original,old)&&
            compiler_puzzle_case_count(restored,original,old)==1&&
            compiler_puzzle_completed(restored,reviewed,current)&&
            compiler_puzzle_completed(restored,metadata,projection)&&
            compiler_puzzle_case_count(restored,reviewed,current)==1&&has_role_witness(restored),
        "A real StateStore reopen lost historical or independently earned local-role credit");
    preserve_preferences(restored,legacy);verify_drafts(restored);
    auto reset=restored;
    reset_compiler_puzzle(reset,reviewed,current);
    require(!compiler_puzzle_completed(reset,reviewed,current)&&
            compiler_puzzle_case_count(reset,reviewed,current)==0&&!has_role_witness(reset)&&
            compiler_puzzle_completed(reset,original,old)&&compiler_puzzle_case_count(reset,original,old)==1,
        "Reset of the new role objective erased stdout history or kept a stale witness");
    preserve_preferences(reset,legacy);verify_drafts(reset);
    std::cout<<"PASS U2L10 local-role StateStore "<<owner_ids[index]<<": real old fixed report, "
        "different correct source, same-ID replay rejected, three bound observations, 11 capture negatives, "
        "single public case, isolated local-roles/v1 witness, drafts/stdin/selections, disk reopen and scoped reset.\n";
}
} // namespace

int main(int argc,char** argv) {
    try {
        require(argc==2,"Usage: local_role_state_tests <exported-fixture-directory>");
        const auto directory=std::filesystem::path(argv[1]);
        const auto before_bytes=state::read_bytes(directory/"before.json");
        const auto after_bytes=state::read_bytes(directory/"after.json");
        const auto manifest_bytes=state::read_bytes(directory/"manifest.json");
        // The first public loader establishes the WinRT apartment.
        const auto original=load_lesson(directory/"before.json");
        const auto reviewed=load_lesson(directory/"after.json");
        const auto metadata=parse_lesson_progress_metadata(reviewed.raw_json,"es");
        validate_fixture(original,reviewed,metadata,directory);
        validate_schema(original,reviewed);
        for(std::size_t index=0;index<owner_ids.size();++index) {
            std::cout<<"RUN U2L10 local roles "<<owner_ids[index]<<'\n'<<std::flush;
            run_owner(original,reviewed,metadata,index);
        }
        require(state::read_bytes(directory/"before.json")==before_bytes&&
                state::read_bytes(directory/"after.json")==after_bytes&&
                state::read_bytes(directory/"manifest.json")==manifest_bytes,
            "Local-role runtime QA changed its authenticated fixture files");
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL U2L10 local roles: "<<error.what()<<'\n';
        return 1;
    }
}
