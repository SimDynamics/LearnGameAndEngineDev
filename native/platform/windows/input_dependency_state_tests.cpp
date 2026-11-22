// Hosted Windows QA of the exact published U2L9 editors. Only the exported
// fixture receives compilerInputChecks; curriculum and personal profiles stay
// outside this test. Every runtime assertion below uses a real MSVC result.
#include "core/compiler_flow.hpp"
#include "core/cpp_input_probe.hpp"
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

constexpr std::array<std::string_view,2> identities{
    "p1u2l9_reparar_lectura_de_cajas","block_0009_0003"};
constexpr std::array<std::string_view,2> references{
    "block_0007_0001","block_0010_0001"};
constexpr std::array<std::string_view,2> reference_hashes{
    "14cbc928ef6925ced0d72e4a1e453765575164369ee0bd36bcfa80bf9d930a54",
    "3a8116f693d93843a03cde6ec26126e31cc95cf4e866d6e38f5b9511d2830425"};

void require(bool condition,std::string_view reason) {
    if(!condition)throw std::runtime_error(std::string(reason));
}
const Block& block_at(const Lesson& lesson,std::string_view identity) {
    const auto at=stable_block_position(lesson,identity);
    require(at.has_value(),"An authentic U2L9 stable block is missing");
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
    require(at!=source.npos,"An exact published source token is missing");
    source.replace(at,before.size(),after);
    return source;
}
bool same_public_cases(const OutputCheck& left,const OutputCheck& right) {
    if(left.input_comparison!=right.input_comparison||left.cases.size()!=right.cases.size())return false;
    for(std::size_t index=0;index<left.cases.size();++index)
        if(!same_output_case_requirements(left.cases[index],right.cases[index])||
           left.cases[index].explanation!=right.cases[index].explanation)return false;
    return true;
}
std::string contract_document(const Lesson& original,std::string_view contract) {
    auto raw=canonical(original.raw_json);
    require(!raw.empty()&&raw.back()=='}',"The U2L9 root is not an object");
    raw.pop_back();
    return raw+",\"compilerInputChecks\":"+std::string(contract)+"}";
}
void rejects_raw(std::string_view raw,std::string_view scenario) {
    for(const bool progress:{false,true}) {
        bool rejected=false;
        try {
            if(progress)(void)parse_lesson_progress_metadata(raw,"es");
            else (void)parse_lesson_json(raw,"es");
        }catch(const std::exception&) {rejected=true;}
        require(rejected,"Invalid U2L9 input authoring accepted by "+
            std::string(progress?"progress":"full")+" loader: "+std::string(scenario));
    }
}
void accepts_raw(std::string_view raw,std::string_view scenario) {
    const auto full=parse_lesson_json(raw,"es");
    const auto progress=parse_lesson_progress_metadata(raw,"es");
    for(const auto identity:identities)
        require(block_at(full,identity).input_check&&
                block_at(full,identity).input_check==block_at(progress,identity).input_check,
            "The input loaders disagree on an allowed authored spelling: "+std::string(scenario));
}

void validate_schema(const Lesson& original,const Lesson& reviewed) {
    const auto root=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    const auto declarations=root.GetNamedArray(L"compilerInputChecks");
    require(declarations.Size()==2,"U2L9 input QA must declare exactly two owners");
    const auto contract=winrt::to_string(declarations.Stringify());
    const std::array<std::pair<std::string_view,std::string_view>,3> duplicates{{
        {"\"activity\":\"p1u2l9_reparar_lectura_de_cajas\"",
         "\"activity\":\"wrong\",\"activity\":\"p1u2l9_reparar_lectura_de_cajas\""},
        {"\"standardInput\":\"Leo 2\"",
         "\"standardInput\":\"wrong\",\"standardInput\":\"Leo 2\""},
        {"\"outputs\":[\"Leo: 5\",\"Leo: 5\\n\"]",
         "\"outputs\":[\"wrong\"],\"outputs\":[\"Leo: 5\",\"Leo: 5\\n\"]"},
    }};
    for(const auto& [before,after]:duplicates)
        rejects_raw(contract_document(original,replace_once(contract,before,after)),
            "raw duplicate contract member");
    rejects_raw(replace_once(contract_document(original,contract),
        "\"compilerInputChecks\":","\"compilerInputChecks\":[],\"compilerInputChecks\":"),
        "raw duplicate root");

    // Every variant changes only the new contract. A historical fingerprint
    // mismatch in the old lesson must not hide a permissive input parser.
    for(unsigned scenario=0;scenario<18;++scenario) {
        const auto changed=JsonArray::Parse(winrt::to_hstring(contract));
        const auto entry=changed.GetObjectAt(0);
        const auto probes=entry.GetNamedArray(L"probes");
        const auto probe=probes.GetObjectAt(0);
        switch(scenario) {
        case 0:probe.Insert(L"standardInput",JsonValue::CreateNumberValue(2));break;
        case 1:probe.Insert(L"outputs",JsonValue::CreateStringValue(L"Leo: 5"));break;
        case 2:probe.Insert(L"standardInput",JsonValue::CreateStringValue(winrt::hstring(std::wstring(1,L'\0'))));break;
        case 3:probe.Insert(L"standardInput",JsonValue::CreateStringValue(winrt::hstring(std::wstring(2049,L'\u00e9'))));break;
        case 4:probe.Insert(L"outputs",JsonArray::Parse(L"[\"Rita: 7\\r\\n\"]"));break;
        case 5:probe.Insert(L"outputs",JsonArray::Parse(L"[\"x\\n\",\"x\\r\\n\"]"));break;
        case 6:probe.Insert(L"standardInput",JsonValue::CreateStringValue(L" \tRita\r\n4\v\f"));break;
        case 7:probes.GetObjectAt(1).Insert(L"standardInput",JsonValue::CreateStringValue(L"Leo\t2\r\n"));break;
        case 8:probe.Insert(L"extra",JsonValue::CreateBooleanValue(true));break;
        case 9:entry.Insert(L"probes",JsonArray{});break;
        case 10:{
            JsonArray too_many;
            for(unsigned index=0;index<9;++index) {
                const auto extra=JsonObject::Parse(L"{\"standardInput\":\"x\",\"outputs\":[\"different\"]}");
                extra.Insert(L"standardInput",JsonValue::CreateStringValue(
                    winrt::to_hstring("input "+std::to_string(index))));
                too_many.Append(extra);
            }
            entry.Insert(L"probes",too_many);break;
        }
        case 11:changed.Append(JsonObject::Parse(entry.Stringify()));break;
        case 12:entry.Insert(L"activity",JsonValue::CreateStringValue(L"missing_editor"));break;
        case 13:probe.Insert(L"outputs",JsonArray::Parse(L"[\"a\",\"b\",\"c\",\"d\",\"e\"]"));break;
        case 14:probe.Insert(L"outputs",JsonArray::Parse(L"[false]"));break;
        case 15:entry.Insert(L"extra",JsonValue::CreateBooleanValue(true));break;
        case 16:probe.Remove(L"standardInput");break;
        case 17:probe.Remove(L"outputs");break;
        }
        rejects_raw(contract_document(original,winrt::to_string(changed.Stringify())),
            "contract scenario "+std::to_string(scenario));
    }
    for(const auto& value:{std::wstring{},std::wstring(2048,L'\u00e9'),std::wstring(L"Rita\u00a04")}) {
        const auto changed=JsonArray::Parse(winrt::to_hstring(contract));
        const auto probe=changed.GetObjectAt(0).GetNamedArray(L"probes").GetObjectAt(0);
        probe.Insert(L"standardInput",JsonValue::CreateStringValue(winrt::hstring(value)));
        probe.Insert(L"outputs",JsonArray::Parse(L"[\"\"]"));
        accepts_raw(contract_document(original,winrt::to_string(changed.Stringify())),
            "empty strings, UTF-8 boundary and Unicode non-separator");
    }
    std::cout<<"PASS U2L9 authoring: both loaders, 22 invalid contracts rejected; "
        "empty strings, 4096-byte UTF-8 and ASCII-only token comparison preserved.\n";
}

void validate_fixture(const Lesson& original,const Lesson& reviewed,const Lesson& metadata) {
    require(state::sha256(original.raw_json)==
        "37b473d7e9b66ab8232812e4e2c9a25e0aaa207bcfc92059fcd6fc675401d94f",
        "U2L9 before.json is not the exact published 51fc2709 source");
    const auto without=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    require(without.HasKey(L"compilerInputChecks"),"U2L9 QA lacks its private input declarations");
    without.Remove(L"compilerInputChecks");
    require(winrt::to_string(without.Stringify())==canonical(original.raw_json)&&
            original.compiler_puzzles_required&&reviewed.compiler_puzzles_required&&
            metadata.compiler_puzzles_required&&original.pages.size()==reviewed.pages.size(),
        "U2L9 input QA changed old curriculum, history or the visible completion policy");
    const std::array<CppInputCheck,2> expected{{
        {{{"Leo 2",{"Leo: 5","Leo: 5\n"}},
          {"Rita 2",{"Rita: 5","Rita: 5\n"}},
          {"Leo 4",{"Leo: 7","Leo: 7\n"}}}},
        {{{"Leo 4 2",{"Leo: 22","Leo: 22\n"}},
          {"Iris 3 2",{"Iris: 17","Iris: 17\n"}},
          {"Iris 4 1",{"Iris: 21","Iris: 21\n"}}}},
    }};
    for(std::size_t page=0;page<original.pages.size();++page) {
        require(original.pages[page].blocks.size()==reviewed.pages[page].blocks.size(),
            "U2L9 input QA moved a block");
        for(std::size_t index=0;index<original.pages[page].blocks.size();++index) {
            const auto& former=original.pages[page].blocks[index];
            const auto& current=reviewed.pages[page].blocks[index];
            require(canonical(former.raw_json)==canonical(current.raw_json)&&
                    former.address.page==current.address.page&&former.address.block==current.address.block,
                "U2L9 input QA changed an original block or persisted address");
            const bool checked=&current==&block_at(reviewed,identities[0])||
                &current==&block_at(reviewed,identities[1]);
            require(!former.input_check&&current.input_check.has_value()==checked,
                "A private input contract escaped its two original owners");
        }
    }
    for(std::size_t index=0;index<identities.size();++index) {
        const auto& before=block_at(original,identities[index]);
        const auto& after=block_at(reviewed,identities[index]);
        const auto& projection=block_at(metadata,identities[index]);
        require(after.input_check&&*after.input_check==expected[index]&&
                after.input_check==projection.input_check&&
                before.initial_code==after.initial_code&&after.initial_code==projection.initial_code&&
                before.standard_input==after.standard_input&&after.standard_input==projection.standard_input&&
                before.output_check&&after.output_check&&projection.output_check&&
                before.output_check->cases.size()==(index==0?2u:4u)&&
                same_public_cases(*after.output_check,*before.output_check)&&
                same_public_cases(*after.output_check,*projection.output_check)&&
                draft_key("programming",original,before)==draft_key("programming",reviewed,after),
            "U2L9 full/progress readers changed the original 2/4 visible cases or drafts");
        require(state::sha256(block_at(original,references[index]).code)==reference_hashes[index],
            "An authentic U2L9 reference source changed");
    }
    require(draft_key("programming",reviewed,block_at(reviewed,identities[0]))!=
            draft_key("programming",reviewed,block_at(reviewed,identities[1])),
        "The two original U2L9 editors share a draft address");
    require(state::sha256(block_at(original,"block_0008_0003").code)==
        "6a7c37d267495154ff0878395d91c643720be8ece29bef8cb9da6cdf94e19053",
        "The authentic fixed-input U2L9 counterexample changed");
    std::cout<<"PASS exact published U2L9 fixture: unchanged pages, history, IDs, drafts, "
        "2/4 public cases and two references; only compilerInputChecks appended.\n";
}

bool has_input_witness(const state::StateData& data) {
    const auto domain=data.preferences.find("native_windows");
    if(domain==data.preferences.end())return false;
    return std::ranges::any_of(domain->second,[](const auto& entry) {
        const auto text=std::get_if<std::string>(&entry.second);
        return entry.first.find("/input/v1/")!=std::string::npos&&entry.first.ends_with("/witness")&&
            text&&!text->empty();
    });
}
void preserves_old_preferences(const state::StateData& after,const state::StateData& before) {
    for(const auto& [domain,values]:before.preferences)
        for(const auto& [key,value]:values)
            require(after.preferences.at(domain).at(key)==value,
                "A new input proof changed an old preference or historical receipt");
}

void run_owner(const Lesson& original,const Lesson& reviewed,const Lesson& metadata,std::size_t index) {
    const auto& former=block_at(original,identities[index]);
    const auto& current=block_at(reviewed,identities[index]);
    const auto& projected=block_at(metadata,identities[index]);
    const auto source=block_at(original,references[index]).code;
    const auto& probes=current.input_check->probes;
    const auto fixed=index==0?block_at(original,"block_0008_0003").code:
        replace_once(source,"std::cout << nombre << \": \" << total;","std::cout << \"Iris: 22\";");
    require(fixed!=source,"The historical stdout source must differ from the corrected input solution");
    const auto options=LaunchOptions{ProfileId::Codex,
        L"input-dependency-u2l9-"+std::to_wstring(index)+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty(),
        "U2L9 execution or persistence would target a personal profile");
    initialize_profile_directories(environment.paths);
    const auto projects=environment.paths.projects/L"U2L9 private input tests";
    std::uint64_t sequence=1000+static_cast<std::uint64_t>(index)*100;
    const auto make_request=[&](std::string program,bool checked=true,
                               std::optional<std::string> input=std::nullopt) {
        CppRunRequest request;
        request.request_id=++sequence;request.revision=sequence+1000;
        request.project_key="isolated-u2l9-input-"+std::to_string(index);
        request.source_utf8=std::move(program);
        request.stdin_utf8=input.value_or(current.standard_input.value_or(""));
        request.projects_directory=projects;
        request.compile_timeout=std::chrono::seconds(60);
        request.execution_timeout=std::chrono::seconds(8);
        if(checked)request.input_check=current.input_check;
        return request;
    };
    const auto execute=[&](const CppRunRequest& request,std::string_view expected) {
        const auto result=run_cpp(request);
        require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                result.execution.succeeded()&&result.execution.stderr_bytes.empty()&&
                cpp_input_normalize_newlines(result.execution.stdout_bytes)==expected,
            "Real MSVC failed a public U2L9 observation: "+result.error_utf8+
            result.compilation.stderr_bytes+result.execution.stderr_bytes);
        return result;
    };
    const auto private_proof=[&](const CppRunRequest& request,const CppRunResult& result,
                                 std::string_view expected_stderr=std::string_view{}) {
        const auto captured=capture_cpp_execution(request,result);
        require(compiler_runtime_request_allowed(current,request)&&
                !cpp_input_request_fingerprint(request).empty()&&captured.input_check_correct&&
                captured.input_request_fingerprint==cpp_input_request_fingerprint(request)&&
                result.input_request_fingerprint==cpp_input_request_fingerprint(request)&&
                result.input_evidence.size()==probes.size()&&
                result.input_executable_sha256.size()==64,
            "A correct U2L9 source lacks its three authenticated private executions");
        require(state::sha256(state::read_bytes(result.run_directory/L"program.exe"))==
                    result.input_executable_sha256&&
                file_bytes_as_string(result.run_directory/L"main.cpp")==request.source_utf8&&
                file_bytes_as_string(result.run_directory/L"stdin.bin")==request.stdin_utf8&&
                file_bytes_as_string(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
                file_bytes_as_string(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "Private input runs replaced the original executable, source, input or public console artifacts");
        for(std::size_t probe=0;probe<probes.size();++probe) {
            const auto& evidence=result.input_evidence[probe];
            require(evidence.source_sha256==state::sha256(request.source_utf8)&&
                    evidence.standard_input==probes[probe].standard_input&&
                    evidence.executable_sha256==result.input_executable_sha256&&
                    evidence.execution.succeeded()&&evidence.execution.stderr_bytes==expected_stderr&&
                    cpp_input_expected_output(evidence.execution.stdout_bytes,probes[probe]),
                "Private U2L9 evidence does not belong to the original source and same executable");
        }
        return captured;
    };
    state::StateData saved;
    saved.preferences["learning_progress"]["other_course_unchanged"]=std::int64_t{42};
    saved.preferences["native_windows"]["unrelated_reader_setting"]=std::string("keep");
    state::prepare_lesson_state(saved,original);
    std::array<state::Draft,2> drafts;
    for(std::size_t draft_index=0;draft_index<identities.size();++draft_index) {
        auto& draft=drafts[draft_index];
        draft.source=block_at(original,references[draft_index]).code+"// Unfinished U2L9 draft.\n";
        draft.input=draft_index==0?"  Rita\t4\n":"Iris\n4\t2\n";
        draft.selection_start=9;draft.selection_end=27;
        state::put_draft(saved,draft_key("programming",original,block_at(original,identities[draft_index])),draft);
    }
    const auto verify_drafts=[&](const state::StateData& data) {
        for(std::size_t draft_index=0;draft_index<identities.size();++draft_index) {
            const auto& editor=block_at(reviewed,identities[draft_index]);
            const auto actual=state::get_draft(data,draft_key("programming",reviewed,editor),editor.initial_code);
            const auto& expected=drafts[draft_index];
            require(actual.has_saved&&actual.source==expected.source&&actual.input==expected.input&&
                    actual.selection_start==expected.selection_start&&actual.selection_end==expected.selection_end,
                "U2L9 checking or reopening changed a draft, stdin or selection");
        }
        require(data.preferences.at("learning_progress").at("other_course_unchanged")==state::Value{std::int64_t{42}}&&
                data.preferences.at("native_windows").at("unrelated_reader_setting")==state::Value{std::string("keep")},
            "U2L9 checking changed another course or reader setting");
    };

    // An actual fixed-output program earns an old stdout receipt, then the
    // same stable editor adopts the new requirement and a different source.
    const auto old_request=make_request(fixed,false);
    require(compiler_runtime_request_allowed(former,old_request)&&
            !compiler_runtime_request_allowed(current,old_request),
        "Original and revised U2L9 request contracts are not distinct");
    const auto old_result=execute(old_request,current.output_check->cases.front().outputs.front());
    const auto old_capture=capture_cpp_execution(old_request,old_result);
    require(record_compiler_puzzle_case(saved,original,former,old_request,old_capture,
                "Actual historical U2L9 stdout from a fixed-output source")&&
            compiler_puzzle_completed(saved,original,former)&&
            compiler_puzzle_case_count(saved,original,former)==1&&!has_input_witness(saved),
        "A real historical U2L9 stdout receipt was not retained");
    const auto legacy=saved;
    {
        state::StateStore writer(environment.paths.root);
        require(writer.empty(),"The supposedly fresh U2L9 QA instance contains progress");
        (void)writer.load();writer.save(saved);
    }
    {
        state::StateStore reader(environment.paths.root);
        require(!reader.empty(),"The historical U2L9 disk receipt is missing");
        saved=reader.load();
    }
    state::validate_lesson_state(saved,reviewed);
    validate_compiler_puzzle_state(saved,reviewed,current);
    verify_drafts(saved);
    require(saved==legacy&&!compiler_puzzle_completed(saved,reviewed,current)&&
            !compiler_puzzle_completed(saved,metadata,projected)&&
            compiler_puzzle_case_count(saved,reviewed,current)==0&&
            compiler_puzzle_case_count(saved,metadata,projected)==0&&
            compiler_puzzle_completed(saved,original,former),
        "Loading the new input goal promoted or changed a historical stdout receipt");
    auto replay_request=old_request;
    replay_request.input_check=current.input_check;
    const auto replay=capture_cpp_execution(replay_request,old_result);
    require(compiler_runtime_request_allowed(current,replay_request)&&
            replay.source_revision==replay_request.revision&&
            replay.input_request_fingerprint==cpp_input_request_fingerprint(replay_request)&&
            !replay.input_check_correct&&
            !record_compiler_puzzle_case(saved,reviewed,current,replay_request,replay,
                "Identical ID and revision still lack private input observations")&&
            !record_compiler_puzzle_case(saved,reviewed,current,old_request,old_capture,
                "Old request lacks the authored private input requirement")&&saved==legacy,
        "Actual old stdout acquired new input credit without private executions");

    const auto checked_request=make_request(source);
    const auto checked_result=execute(checked_request,current.output_check->cases.front().outputs.front());
    const auto checked_capture=private_proof(checked_request,checked_result);
    require(record_compiler_puzzle_case(saved,reviewed,current,checked_request,checked_capture,
                "One public case with three real private stdin observations")&&
            compiler_puzzle_completed(saved,reviewed,current)&&
            compiler_puzzle_case_count(saved,reviewed,current)==1&&has_input_witness(saved)&&
            compiler_puzzle_completed(saved,original,former)&&
            compiler_puzzle_case_count(saved,original,former)==1,
        "One genuine public check did not earn the separate input receipt");
    preserves_old_preferences(saved,legacy);

    // Optional inputs remain voluntary. Their private reuse does not earn a
    // visible case, and an optional public run cannot stand in for case zero.
    const auto optional_request=make_request(source,true,current.output_check->cases[1].standard_input);
    const auto optional_result=execute(optional_request,current.output_check->cases[1].outputs.front());
    const auto optional_capture=private_proof(optional_request,optional_result);
    auto partial=legacy;
    // The return value reports objective completion, not whether a public
    // observation was saved. An optional-only observation is retained below.
    const bool optional_completed=record_compiler_puzzle_case(
        partial,reviewed,current,optional_request,optional_capture,
        "A voluntary public case before the mandatory first case");
    require(!optional_completed,
        "An optional-only observation reported objective completion");
    require(!compiler_puzzle_completed(partial,reviewed,current),
        "An optional public input or its private runs completed the first case");
    require(compiler_puzzle_case_count(partial,reviewed,current)==1,
        "An optional-only run did not retain exactly one public observation");
    require(!has_input_witness(partial),
        "An optional-only observation wrote a completion witness before the first case");
    preserves_old_preferences(partial,legacy);
    {
        const auto partial_options=LaunchOptions{ProfileId::Codex,
            *environment.options.qa_instance+L"-partial",false};
        const auto partial_environment=resolve_windows_environment(partial_options);
        require(partial_environment.options.qa_instance&&!partial_environment.options.qa_instance->empty(),
            "A partial U2L9 save would target a personal profile");
        initialize_profile_directories(partial_environment.paths);
        {
            state::StateStore writer(partial_environment.paths.root);
            require(writer.empty(),"The partial U2L9 QA instance unexpectedly contains progress");
            (void)writer.load();writer.save(partial);
        }
        state::StateStore reader(partial_environment.paths.root);
        require(!reader.empty(),"An optional U2L9 observation was not saved");
        const auto reopened_partial=reader.load();
        state::validate_lesson_state(reopened_partial,reviewed);
        validate_compiler_puzzle_state(reopened_partial,reviewed,current);
        validate_compiler_puzzle_state(reopened_partial,metadata,projected);
        require(reopened_partial==partial&&!compiler_puzzle_completed(reopened_partial,reviewed,current)&&
                compiler_puzzle_case_count(reopened_partial,reviewed,current)==1&&
                !compiler_puzzle_completed(reopened_partial,metadata,projected)&&
                compiler_puzzle_case_count(reopened_partial,metadata,projected)==1&&
                !has_input_witness(reopened_partial),
            "An optional-only disk receipt became complete, corrupt or lost on reopening");
        verify_drafts(reopened_partial);
        preserves_old_preferences(reopened_partial,legacy);
        partial=reopened_partial;
    }
    auto invalidated=partial;
    const auto edited_request=make_request(source+"// New unfinished draft.\n");
    auto new_draft=drafts[index];
    new_draft.source=edited_request.source_utf8;
    new_draft.input="  "+current.standard_input.value_or("")+"\n";
    const auto edited_key=draft_key("programming",reviewed,current);
    state::put_draft(invalidated,edited_key,new_draft);
    auto edited_legacy=legacy;
    state::put_draft(edited_legacy,edited_key,new_draft);
    invalidate_compiler_puzzle_attempt(invalidated,reviewed,current,edited_request);
    require(compiler_puzzle_case_count(invalidated,reviewed,current)==0&&
            !compiler_puzzle_completed(invalidated,reviewed,current)&&
            compiler_puzzle_completed(invalidated,original,former)&&
            compiler_puzzle_case_count(invalidated,original,former)==1,
        "Editing an incomplete new input attempt erased old stdout receipts or kept stale partial credit");
    preserves_old_preferences(invalidated,edited_legacy);
    const auto retained_draft=state::get_draft(invalidated,edited_key,current.initial_code);
    require(retained_draft.has_saved&&retained_draft.source==new_draft.source&&
            retained_draft.input==new_draft.input&&retained_draft.selection_start==new_draft.selection_start&&
            retained_draft.selection_end==new_draft.selection_end,
        "Invalidating the incomplete input attempt changed the learner's new draft");
    require(record_compiler_puzzle_case(partial,reviewed,current,checked_request,checked_capture,
                "The mandatory first public input after a voluntary case")&&
            compiler_puzzle_completed(partial,reviewed,current)&&
            compiler_puzzle_case_count(partial,reviewed,current)==2,
        "A later first public case lost the separate voluntary observation");

    // These are capture-adversarial cases, not claims that Windows itself
    // timed out or produced a foreign binary. All start from one real result.
    for(unsigned scenario=0;scenario<10;++scenario) {
        auto tampered=checked_result;
        auto request=checked_request;
        switch(scenario) {
        case 0:tampered.input_evidence[0].source_sha256=state::sha256("other source");break;
        case 1:tampered.input_evidence[0].standard_input="other input";break;
        case 2:tampered.input_evidence[0].executable_sha256=state::sha256("other binary");break;
        case 3:tampered.input_request_fingerprint=state::sha256("other specification");break;
        case 4:tampered.input_executable_sha256.clear();break;
        case 5:tampered.input_evidence.pop_back();break;
        case 6:std::swap(tampered.input_evidence[0],tampered.input_evidence[1]);break;
        case 7:tampered.input_evidence[0].execution.status=CppPhaseStatus::timed_out;break;
        case 8:tampered.input_evidence[0].execution.status=CppPhaseStatus::cancelled;break;
        case 9:request.input_check->probes[0].outputs={"a different authored result"};break;
        }
        const auto rejected=capture_cpp_execution(request,tampered);
        const auto unchanged=saved;
        require(!rejected.input_check_correct&&
                !record_compiler_puzzle_case(saved,reviewed,current,request,rejected,
                    "Mismatched private input evidence")&&saved==unchanged,
            "Tampered U2L9 source/input/binary/specification or incomplete phase earned credit: "+
            std::to_string(scenario));
    }
    auto unsealed=checked_capture;
    unsealed.input_request_fingerprint.clear();
    const auto before_unsealed=saved;
    require(!record_compiler_puzzle_case(saved,reviewed,current,checked_request,unsealed,
            "Private correctness without its request fingerprint")&&saved==before_unsealed,
        "An unbound private-correctness flag earned a receipt");

    const auto equivalent=[&](std::string_view label,std::string equivalent_source,std::string input,
                              std::string_view expected_stderr=std::string_view{}) {
        const auto request=make_request(std::move(equivalent_source),true,std::move(input));
        const auto& expected=current.output_check->cases.front().outputs.front();
        const auto result=run_cpp(request);
        require(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
                result.execution.stderr_bytes==expected_stderr&&
                (cpp_input_normalize_newlines(result.execution.stdout_bytes)==expected||
                 cpp_input_normalize_newlines(result.execution.stdout_bytes)==expected+"\n"),
            "An equivalent U2L9 source failed its authentic public case");
        const auto proof=private_proof(request,result,expected_stderr);
        state::StateData fresh;
        state::prepare_lesson_state(fresh,reviewed);
        require(record_compiler_puzzle_case(fresh,reviewed,current,request,proof,label)&&
                compiler_puzzle_completed(fresh,reviewed,current)&&
                compiler_puzzle_case_count(fresh,reviewed,current)==1&&has_input_witness(fresh),
            "An equivalent source needed an extra visible check");
        std::cout<<"PASS U2L9 equivalent source "<<identities[index]<<": "<<label<<'\n';
    };
    const std::string renamed=index==0?
        "#include <iostream>\n#include <string>\nint main(){std::string etiqueta; int cantidad{}; "
        "std::cin>>etiqueta; std::cin>>cantidad; std::cout<<etiqueta<<\": \"<<(cantidad+3);}\n":
        "#include <iostream>\n#include <string>\nint main(){std::string etiqueta; int grupos{}; int piezas{}; "
        "std::cin>>etiqueta; std::cin>>grupos; std::cin>>piezas; "
        "std::cout<<etiqueta<<\": \"<<(grupos*5+piezas);}\n";
    equivalent("free names and split extractions",renamed,current.standard_input.value_or(""));
    const auto newline=replace_once(source,index==0?
        "std::cout << nombre << \": \" << cajas;":"std::cout << nombre << \": \" << total;",
        index==0?"std::cout << nombre << \": \" << cajas << '\\n';":
            "std::cout << nombre << \": \" << total << '\\n';");
    equivalent("optional newline and public ASCII whitespace",newline,index==0?"  Rita\t4\n":"Iris\n4\t2\n");
    const auto diagnostic=replace_once(source,"return 0;",
        "std::cerr << \"diagnostico de prueba\";\n    return 0;");
    equivalent("innocuous stderr keeps the original ignored-stderr policy",diagnostic,
        current.standard_input.value_or(""),"diagnostico de prueba");

    const auto reject_fixed=[&](std::string_view label,const std::string& program,std::array<bool,3> expected) {
        const auto request=make_request(program);
        require(compiler_runtime_request_allowed(current,request),
            "A fixed-output counterexample was rejected before the actual runtime check");
        const auto result=execute(request,current.output_check->cases.front().outputs.front());
        require(result.input_evidence.size()==3,"A counterexample lacks its actual private executions");
        for(std::size_t probe=0;probe<probes.size();++probe) {
            const auto& evidence=result.input_evidence[probe];
            require(evidence.source_sha256==state::sha256(program)&&
                    evidence.standard_input==probes[probe].standard_input&&
                    evidence.executable_sha256==result.input_executable_sha256&&
                    evidence.execution.succeeded()&&evidence.execution.stderr_bytes.empty()&&
                    cpp_input_expected_output(evidence.execution.stdout_bytes,probes[probe])==expected[probe],
                "An independent U2L9 probe did not isolate the fixed datum: "+std::string(label));
        }
        const auto proof=capture_cpp_execution(request,result);
        const auto unchanged=saved;
        require(!proof.input_check_correct&&
                !record_compiler_puzzle_case(saved,reviewed,current,request,proof,label)&&saved==unchanged,
            "A correct public result concealed a fixed input datum");
        std::cout<<"PASS U2L9 real counterexample rejected "<<identities[index]<<": "<<label<<'\n';
    };
    reject_fixed("all input data fixed",fixed,{false,false,false});
    if(index==0) {
        reject_fixed("name fixed",replace_once(source,
            "std::cout << nombre << \": \" << cajas;","std::cout << \"Rita\" << \": \" << cajas;"),
            {false,true,false});
        reject_fixed("box count fixed",replace_once(source,
            "std::cout << nombre << \": \" << cajas;","std::cout << nombre << \": 7\";"),
            {false,false,true});
    } else {
        reject_fixed("label fixed",replace_once(source,
            "std::cout << nombre << \": \" << total;","std::cout << \"Iris\" << \": \" << total;"),
            {false,true,true});
        reject_fixed("package count fixed",replace_once(source,
            "(5 * paquetes) + sueltas","(5 * 4) + sueltas"),{true,false,true});
        reject_fixed("loose-piece count fixed",replace_once(source,
            "(5 * paquetes) + sueltas","(5 * paquetes) + 2"),{true,true,false});
    }
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled_request=make_request(source);
    const auto cancelled=run_cpp(cancelled_request,stop.get_token());
    const auto cancellation=capture_cpp_execution(cancelled_request,cancelled);
    const auto unchanged=saved;
    require(!cancelled.compilation.succeeded()&&!cancellation.input_check_correct&&
            !record_compiler_puzzle_case(saved,reviewed,current,cancelled_request,cancellation,
                "Cancelled before native compilation")&&saved==unchanged,
        "A pre-cancelled native request earned private input credit");

    // Preserve both old and new earned achievements when the learner edits a
    // fresh draft, then prove separate ownership survives a real disk reopen.
    invalidate_compiler_puzzle_attempt(saved,reviewed,current,edited_request);
    require(compiler_puzzle_completed(saved,reviewed,current),
        "Editing a new draft erased an already earned input achievement");
    verify_drafts(saved);
    preserves_old_preferences(saved,legacy);
    {
        state::StateStore writer(environment.paths.root);
        require(!writer.empty()&&writer.load()==legacy,
            "An uncommitted U2L9 test modified durable historical progress");
        writer.save(saved);
    }
    state::StateStore reopened(environment.paths.root);
    require(!reopened.empty(),"The new U2L9 input receipt was not saved");
    const auto restored=reopened.load();
    state::validate_lesson_state(restored,reviewed);
    validate_compiler_puzzle_state(restored,reviewed,current);
    validate_compiler_puzzle_state(restored,metadata,projected);
    verify_drafts(restored);
    preserves_old_preferences(restored,legacy);
    require(restored==saved&&compiler_puzzle_completed(restored,original,former)&&
            compiler_puzzle_completed(restored,reviewed,current)&&
            compiler_puzzle_completed(restored,metadata,projected)&&
            compiler_puzzle_case_count(restored,reviewed,current)==1&&has_input_witness(restored),
        "A real StateStore reopen lost the original or new U2L9 receipts");
    auto reset=restored;
    reset_compiler_puzzle(reset,reviewed,current);
    require(!compiler_puzzle_completed(reset,reviewed,current)&&
            compiler_puzzle_case_count(reset,reviewed,current)==0&&
            compiler_puzzle_completed(reset,original,former)&&
            compiler_puzzle_case_count(reset,original,former)==1,
        "Resetting the revised input objective erased the historical stdout receipt");
    preserves_old_preferences(reset,legacy);
    verify_drafts(reset);
    std::cout<<"PASS U2L9 real MSVC and StateStore "<<identities[index]<<": one public check, "
        "three private runs of the same executable, historical fixed-output receipt, different corrected source, "
        "optional-case disk isolation, innocuous stderr, 10 capture negatives, cancellation, separate input/v1 seal, "
        "drafts/selections/stdin/preferences and old receipts preserved on disk reopen and reset.\n";
}
} // namespace

int main(int argc,char** argv) {
    try {
        require(argc==2,"Usage: input_dependency_state_tests <exported-fixture-directory>");
        const auto directory=std::filesystem::path(argv[1]);
        // load_lesson initializes WinRT before any JsonObject is constructed.
        const auto original=load_lesson(directory/"before.json");
        const auto reviewed=load_lesson(directory/"after.json");
        const auto metadata=parse_lesson_progress_metadata(reviewed.raw_json,"es");
        validate_fixture(original,reviewed,metadata);
        validate_schema(original,reviewed);
        for(std::size_t index=0;index<identities.size();++index)
            run_owner(original,reviewed,metadata,index);
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL U2L9 input dependency: "<<error.what()<<'\n';
        return 1;
    }
}
