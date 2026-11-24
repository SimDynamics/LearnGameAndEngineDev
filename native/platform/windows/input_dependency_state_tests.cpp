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

void validate_same_output_schema(const Lesson& original,const Lesson& reviewed) {
    const auto root=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    const auto declarations=root.GetNamedArray(L"compilerInputChecks");
    const auto first=declarations.GetObjectAt(0);
    first.Insert(L"allowSameOutput",JsonValue::CreateBooleanValue(true));
    const auto opt_in=winrt::to_string(declarations.Stringify());
    const auto opted_raw=contract_document(original,opt_in);
    accepts_raw(opted_raw,"explicit mode with all probes distinguishing");
    for(const auto& parsed:{parse_lesson_json(opted_raw,"es"),
                           parse_lesson_progress_metadata(opted_raw,"es")})
        require(block_at(parsed,identities[0]).input_check->allow_same_output&&
                !block_at(parsed,identities[1]).input_check->allow_same_output,
            "An explicit input mode was lost or applied to another owner");

    for(const std::string_view value:{"false","null","0","1","\"true\"","[]","{}"})
        rejects_raw(contract_document(original,replace_once(opt_in,"\"allowSameOutput\":true",
            "\"allowSameOutput\":"+std::string(value))),"non-true same-output option");
    for(const std::string_view duplicate:{
            "\"allowSameOutput\":false,\"allowSameOutput\":true",
            "\"allowSameOutput\":true,\"allowSameOutput\":true",
            "\"allowSameOut\\u0070ut\":true,\"allowSameOutput\":true"})
        rejects_raw(contract_document(original,replace_once(opt_in,"\"allowSameOutput\":true",duplicate)),
            "raw duplicate same-output option");

    const auto& visible=block_at(reviewed,identities[0]).output_check->cases.front().outputs;
    JsonArray visible_outputs;
    for(const auto& output:visible)visible_outputs.Append(JsonValue::CreateStringValue(winrt::to_hstring(output)));
    first.GetNamedArray(L"probes").GetObjectAt(0).Insert(L"outputs",visible_outputs);
    const auto mixed=winrt::to_string(declarations.Stringify());
    accepts_raw(contract_document(original,mixed),"explicit mixed plan with a whole distinguishing probe");
    const auto legacy_mixed=JsonArray::Parse(winrt::to_hstring(mixed));
    legacy_mixed.GetObjectAt(0).Remove(L"allowSameOutput");
    rejects_raw(contract_document(original,winrt::to_string(legacy_mixed.Stringify())),
        "same mixed plan without the explicit owner option");

    // One different alternative cannot rescue a probe that also accepts the
    // public answer. A separate whole probe must exclude every public answer.
    const auto overlapping=JsonArray::Parse(winrt::to_hstring(mixed));
    JsonArray partial_outputs;
    partial_outputs.Append(JsonValue::CreateStringValue(winrt::to_hstring(visible.front())));
    partial_outputs.Append(JsonValue::CreateStringValue(L"a distinct alternative"));
    overlapping.GetObjectAt(0).GetNamedArray(L"probes").GetObjectAt(0).Insert(L"outputs",partial_outputs);
    accepts_raw(contract_document(original,winrt::to_string(overlapping.Stringify())),
        "overlapping alternatives with another wholly distinguishing probe");
    JsonArray crlf_output;
    std::string crlf;
    for(const char character:visible.back()) {
        if(character=='\n')crlf+='\r';
        crlf+=character;
    }
    crlf_output.Append(JsonValue::CreateStringValue(winrt::to_hstring(crlf)));
    for(const auto& outputs:{visible_outputs,partial_outputs,crlf_output}) {
        const auto invariant=JsonArray::Parse(winrt::to_hstring(mixed));
        for(const auto& probe:invariant.GetObjectAt(0).GetNamedArray(L"probes"))
            probe.GetObject().Insert(L"outputs",outputs);
        rejects_raw(contract_document(original,winrt::to_string(invariant.Stringify())),
            "no whole probe excludes the first public output");
    }
    for(unsigned scenario=0;scenario<7;++scenario) {
        const auto changed=JsonArray::Parse(winrt::to_hstring(mixed));
        const auto entry=changed.GetObjectAt(0);
        const auto probes=entry.GetNamedArray(L"probes");
        switch(scenario) {
        case 0:probes.GetObjectAt(0).Insert(L"allowSameOutput",JsonValue::CreateBooleanValue(true));break;
        case 1:probes.GetObjectAt(0).Insert(L"outputs",JsonArray::Parse(L"[\"x\\n\",\"x\\r\\n\"]"));break;
        case 2:probes.GetObjectAt(1).Insert(L"standardInput",probes.GetObjectAt(0).GetNamedValue(L"standardInput"));break;
        case 3:entry.Remove(L"activity");break;
        case 4:entry.Remove(L"probes");break;
        case 5:
            while(probes.Size()<9) {
                const auto extra=JsonObject::Parse(probes.GetObjectAt(1).Stringify());
                extra.Insert(L"standardInput",JsonValue::CreateStringValue(
                    winrt::to_hstring("private "+std::to_string(probes.Size()))));
                probes.Append(extra);
            }
            break;
        case 6:probes.GetObjectAt(0).Insert(L"outputs",JsonArray::Parse(L"[\"a\",\"b\",\"c\",\"d\",\"e\"]"));break;
        }
        rejects_raw(contract_document(original,winrt::to_string(changed.Stringify())),
            "same-output contract scenario "+std::to_string(scenario));
    }
    std::cout<<"PASS explicit same-output authoring: full/progress agree, true-only owner option, "
        "raw duplicates and all-invariant alternatives rejected, legacy per-probe discrimination retained.\n";
}

void validate_legacy_input_fingerprint() {
    CppRunRequest request;
    request.request_id=17;request.revision=29;
    request.source_utf8="int main(){}\n";request.stdin_utf8="first\n";
    request.project_key="input-mode-compat";
    request.projects_directory=L"C:\\input-mode-compat";
    request.compile_timeout=std::chrono::milliseconds(60000);
    request.execution_timeout=std::chrono::milliseconds(5000);
    request.output_limit_bytes=1048576;
    request.input_check=CppInputCheck{{{"other\n",{"result","result\n"}}}};
    // Frozen from the pre-option capture-v1 bytes; no compilation is needed.
    const auto legacy=cpp_input_request_fingerprint(request);
    require(legacy=="49763b068fd57e857fb23687ad749f79617cdf0341f68123a195becf5bdc080c",
        "The absent-option input capture no longer has its exact historical preimage");
    request.input_check->allow_same_output=true;
    require(!cpp_input_request_fingerprint(request).empty()&&cpp_input_request_fingerprint(request)!=legacy,
        "The explicit input mode shares the historical capture domain");
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

void validate_same_output_binding(const Lesson& original,const Lesson& reviewed,std::size_t index,
        const CppRunRequest& request,const CppRunResult& result,const FreshExecution& proof,
        const state::StateData& earned) {
    // An isolated negative fixture, not an allowed transition of a published owner.
    const auto root=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    const auto declarations=root.GetNamedArray(L"compilerInputChecks");
    declarations.GetObjectAt(static_cast<std::uint32_t>(index)).Insert(
        L"allowSameOutput",JsonValue::CreateBooleanValue(true));
    const auto raw=contract_document(original,winrt::to_string(declarations.Stringify()));
    const auto opted=parse_lesson_json(raw,"es");
    const auto metadata=parse_lesson_progress_metadata(raw,"es");
    const auto& current=block_at(reviewed,identities[index]);
    const auto& changed=block_at(opted,identities[index]);
    auto mode_request=request;
    mode_request.input_check=changed.input_check;
    const auto legacy_fingerprint=cpp_input_request_fingerprint(request);
    const auto mode_fingerprint=cpp_input_request_fingerprint(mode_request);
    require(proof.input_check_correct&&proof.input_request_fingerprint==legacy_fingerprint&&
            !mode_fingerprint.empty()&&mode_fingerprint!=legacy_fingerprint&&
            compiler_runtime_request_allowed(changed,mode_request)&&
            !compiler_runtime_request_allowed(current,mode_request)&&
            !compiler_runtime_request_allowed(changed,request)&&
            reference_objective_frame(reviewed,current)==reference_objective_frame(opted,changed)&&
            draft_key("programming",reviewed,current)==draft_key("programming",opted,changed),
        "An explicit input mode changed a draft/objective or shared the old runtime request");

    // Reuse the actual successful native result; never relabel its evidence as
    // an observation of a different mode. Capture and completion must reject it.
    const auto recaptured=capture_cpp_execution(mode_request,result);
    require(!recaptured.input_check_correct&&recaptured.input_request_fingerprint==mode_fingerprint,
        "A real legacy execution was recaptured as explicit same-output evidence");
    for(const Lesson* lesson:{&opted,&metadata}) {
        const auto& owner=block_at(*lesson,identities[index]);
        auto attempt=earned;
        require(!compiler_puzzle_completed(earned,*lesson,owner)&&
                !record_compiler_puzzle_case(attempt,*lesson,owner,mode_request,proof,
                    "A legacy capture cannot serve another input mode")&&
                !record_compiler_puzzle_case(attempt,*lesson,owner,mode_request,recaptured,
                    "A relabelled request cannot invent private observations")&&attempt==earned,
            "A legacy input capture or completion seal crossed into the explicit mode");
        bool rejected=false;
        try {validate_compiler_puzzle_state(earned,*lesson,owner);}
        catch(const std::exception&) {rejected=true;}
        require(rejected,"An earned legacy input witness was accepted under a different mode");
    }
    require(compiler_puzzle_completed(earned,reviewed,current),
        "Rejecting a different mode invalidated the actual historical achievement");

    // All inputs and alternatives remain fingerprinted, including probes that
    // a future explicit plan may legitimately give the same public output.
    for(std::size_t probe=0;probe<mode_request.input_check->probes.size();++probe) {
        auto altered=mode_request;
        altered.input_check->probes[probe].standard_input+='\n';
        require(!cpp_input_request_fingerprint(altered).empty()&&
                cpp_input_request_fingerprint(altered)!=mode_fingerprint,
            "An explicit-mode private input was omitted from the capture");
        for(std::size_t output=0;output<mode_request.input_check->probes[probe].outputs.size();++output) {
            altered=mode_request;
            altered.input_check->probes[probe].outputs[output]+=' ';
            require(!cpp_input_request_fingerprint(altered).empty()&&
                    cpp_input_request_fingerprint(altered)!=mode_fingerprint,
                "An explicit-mode output alternative was omitted from the capture");
        }
    }
    auto reordered=mode_request;
    std::reverse(reordered.input_check->probes.begin(),reordered.input_check->probes.end());
    require(cpp_input_request_fingerprint(reordered)!=mode_fingerprint,
        "An explicit-mode private execution order was omitted from the capture");
    std::cout<<"PASS U2L9 input-mode isolation "<<identities[index]<<": real legacy capture and seal "
        "rejected in full/progress, complete ordered plan bound, old objective and draft unchanged.\n";
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

    validate_same_output_binding(original,reviewed,index,checked_request,checked_result,checked_capture,saved);

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

struct ExtendedInputPlan {
    std::string_view name,path,owner,reference,source_sha256,reference_sha256;
    std::vector<std::pair<std::string,std::string>> public_cases,private_cases,free_names;
    std::string read_statement;
    std::vector<std::pair<std::string,std::string>> fixed_operands;
};
const std::array<ExtendedInputPlan,3>& extended_input_plans() {
    static const std::array<ExtendedInputPlan,3> plans{{
        {"u2l10",
         "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_representar_procesar_y_recuperar.json",
         "p1u2l10_reparar_registro","p1u2l10_ejemplo_copia_reparada",
         "e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0",
         "ed1c83bff583431507d0260a596f080294f65561d5b155d8497a91f1aec6e6ed",
         {{"4 5 2","Inicio: 4\nAhora: 7"},{"0 2 1","Inicio: 0\nAhora: 1"},{"6 0 2","Inicio: 6\nAhora: 4"}},
         {{"6 5 2","Inicio: 6\nAhora: 9"},{"4 2 2","Inicio: 4\nAhora: 4"},{"4 5 1","Inicio: 4\nAhora: 8"}},
         {{"libros","saldo_actual"},{"libros_devueltos","entradas"},{"libros_prestados","salidas"},{"libros_iniciales","saldo_al_abrir"}},
         "    std::cin >> libros >> libros_devueltos >> libros_prestados;\n",
         {{"libros","4"},{"libros_devueltos","5"},{"libros_prestados","2"}}},
        {"u3l1",
         "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_valores_expresiones_y_prioridad.json",
         "p1u3l1_construir_puntuacion","p1u3l1_ejemplo_puntuacion",
         "0c4f3884ae261d3f27015de5e91d776efb4bf1dd521b725f3fc3203134fef716",
         "8373f6490814b65622005006be6cafbae12b6cb1fc27b64db3812a5878fc100d",
         {{"10 3 4","Puntos: 18"},{"7 0 2","Puntos: 16"},{"5 5 3","Puntos: 3"}},
         {{"7 0 2","Puntos: 16"},{"5 5 3","Puntos: 3"}},
         {{"puntos_iniciales","inicio"},{"puntos_gastados","coste"},{"bono","extra"},{"puntos_finales","resultado"}},
         "    std::cin >> puntos_iniciales >> puntos_gastados >> bono;\n",
         {{"puntos_iniciales","10"},{"puntos_gastados","3"},{"bono","4"}}},
        {"u3l2",
         "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_division_y_resto.json",
         "p1u3l2_construir_informe_bandejas","p1u3l2_ejemplo_bandejas",
         "0f0fccd697fc9bd24ac1087bb735cdb89ecf362b9b8b35964790b94a5c48b596",
         "36637787dc00da0ce5874a7974398768cf0d820d979a7aa2fc58fe59751eb588",
         {{"29 6","Bandejas completas: 4\nSobrantes: 5"},{"24 6","Bandejas completas: 4\nSobrantes: 0"},
          {"3 6","Bandejas completas: 0\nSobrantes: 3"},{"0 6","Bandejas completas: 0\nSobrantes: 0"},
          {"29 4","Bandejas completas: 7\nSobrantes: 1"}},
         {{"24 6","Bandejas completas: 4\nSobrantes: 0"},{"3 6","Bandejas completas: 0\nSobrantes: 3"},
          {"0 6","Bandejas completas: 0\nSobrantes: 0"},{"29 4","Bandejas completas: 7\nSobrantes: 1"}},
         {{"fichas","cantidad"},{"capacidad","por_grupo"},{"bandejas_completas","grupos"},{"sobrantes","resto"}},
         "    std::cin >> fichas >> capacidad;\n",
         {{"fichas","29"},{"capacidad","6"}}},
    }};
    return plans;
}
CppInputCheck extended_input_check(const ExtendedInputPlan& plan) {
    CppInputCheck check;
    for(const auto& [input,output]:plan.private_cases)
        check.probes.push_back({input,{output,output+'\n'}});
    return check;
}
std::string extended_free_names(std::string source,const ExtendedInputPlan& plan) {
    const auto identifier=[](char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
    };
    // These exact authenticated sources contain the selected names only as
    // C++ identifiers. Boundaries avoid changing the prefix of another name.
    for(const auto& [before,after]:plan.free_names) {
        std::size_t count=0;
        for(std::size_t at=0;(at=source.find(before,at))!=source.npos;) {
            const auto end=at+before.size();
            if((at==0||!identifier(source[at-1]))&&(end==source.size()||!identifier(source[end]))) {
                source.replace(at,before.size(),after);at+=after.size();++count;
            }else at=end;
        }
        require(count>=2,"A free-name input solution did not rename its declaration and use");
    }
    return source;
}
bool extended_reference_available(const state::StateData& data,const Lesson& lesson,
        const ExtendedInputPlan& plan) {
    const auto location=stable_block_position(lesson,plan.reference);
    require(location.has_value(),"The extended input reference has no stable position");
    const auto& reference=lesson.pages[location->page].blocks[location->block];
    const auto proofs=compiler_completion_snapshot(lesson,
        [&](const Block& block){return compiler_puzzle_completed(data,lesson,block);});
    return compiler_reference_available(lesson,location->page,reference,proofs,
        [&](const Block& block){return compiler_puzzle_completed(data,lesson,block);});
}
state::Preferences extended_input_seals(const state::StateData& data,const Lesson& lesson,
        const Block& block) {
    state::Preferences seals;
    const auto prefix="compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+exercise_key(lesson,block)+'/';
    if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
        for(const auto& [key,value]:domain->second)
            if(key.starts_with(prefix)&&key.find("/input/v1/")!=key.npos&&
               (key.ends_with("/witness")||key.ends_with("/completed")))seals.emplace(key,value);
    return seals;
}
const Block& prepare_extended_prerequisite(state::StateData& data,const Lesson& lesson,
        const ExtendedInputPlan& plan,const std::filesystem::path& projects) {
    const auto& owner=block_at(lesson,plan.owner);
    const auto dependencies=activity_prerequisites(lesson,owner);
    require(dependencies.size()==1,"An extended input prerequisite needs an explicit QA audit");
    const auto& prerequisite=lesson.pages[dependencies[0].page].blocks[dependencies[0].block];
    std::string source;
    if(plan.name=="u2l10") {
        require(prerequisite.stable_id=="block_0007_0002",
            "The U2L10 input prerequisite is no longer its library report");
        source=block_at(lesson,"block_0008_0001").code;
    }else if(plan.name=="u3l1") {
        require(prerequisite.stable_id=="p1u3l1_reparar_fichas"&&prerequisite.local_int_check,
            "The U3L1 input prerequisite lost its independent local-variable goal");
        source=replace_once(prerequisite.initial_code,
            "(sobres + fichas_sueltas) * fichas_por_sobre",
            "sobres * fichas_por_sobre + fichas_sueltas");
    }else {
        require(plan.name=="u3l2"&&prerequisite.stable_id=="p1u3l2_reparar_conversion_minutos"&&
                prerequisite.local_int_check,
            "The U3L2 input prerequisite lost its independent local-variable goal");
        source=replace_once(prerequisite.initial_code,
            "const int minutos = total_minutos / minutos_por_hora;",
            "const int minutos = total_minutos % minutos_por_hora;");
    }
    require(prerequisite.output_check&&!prerequisite.output_check->cases.empty()&&!source.empty()&&
            compiler_puzzle_case_count(data,lesson,prerequisite)==0,
        "The extended prerequisite is missing or already has invented credit");
    CppRunRequest request;
    request.request_id=60000;request.revision=60001;
    request.project_key=draft_key("programming",lesson,prerequisite);
    request.source_utf8=std::move(source);
    request.stdin_utf8=prerequisite.output_check->cases.front().standard_input;
    request.projects_directory=projects;
    request.compile_timeout=std::chrono::seconds(60);request.execution_timeout=std::chrono::seconds(8);
    request.local_int_check=prerequisite.local_int_check;
    require(compiler_runtime_request_allowed(prerequisite,request),
        "The real extended prerequisite has an incompatible execution request");
    const auto result=run_cpp(request);
    const auto captured=capture_cpp_execution(request,result);
    require(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
            result.execution.stderr_bytes.empty()&&
            (!prerequisite.local_int_check||captured.local_int_check_correct)&&
            evaluate_cpp_output_check(*prerequisite.output_check,captured,request.revision).verdict==OutputVerdict::correct&&
            record_compiler_puzzle_case(data,lesson,prerequisite,request,captured,
                "Actual declared prerequisite, including its independent local checks")&&
            compiler_puzzle_completed(data,lesson,prerequisite)&&
            compiler_puzzle_case_count(data,lesson,prerequisite)==1,
        "A real extended input prerequisite failed its own independent contract");
    return prerequisite;
}
void validate_extended_fixture(const ExtendedInputPlan& plan,const Lesson& original,
        const Lesson& reviewed,const Lesson& metadata,const std::filesystem::path& directory) {
    require(state::sha256(original.raw_json)==plan.source_sha256,
        "Extended input QA did not load the exact main 2ca32b23 publication");
    const auto without=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    require(without.HasKey(L"compilerInputChecks"),"The extended input declaration is missing");
    without.Remove(L"compilerInputChecks");
    require(winrt::to_string(without.Stringify())==canonical(original.raw_json)&&
            original.compiler_puzzles_required&&reviewed.compiler_puzzles_required&&
            metadata.compiler_puzzles_required&&original.pages.size()==reviewed.pages.size(),
        "Extended input QA changed a historical field or visible completion policy");
    for(std::size_t page=0;page<original.pages.size();++page) {
        require(original.pages[page].blocks.size()==reviewed.pages[page].blocks.size(),
            "Extended input QA moved an authored block");
        for(std::size_t index=0;index<original.pages[page].blocks.size();++index) {
            const auto& before=original.pages[page].blocks[index];
            const auto& after=reviewed.pages[page].blocks[index];
            require(canonical(before.raw_json)==canonical(after.raw_json)&&
                    before.stable_id==after.stable_id&&before.address.page==after.address.page&&
                    before.address.block==after.address.block&&!before.input_check&&
                    after.input_check.has_value()==(after.stable_id==plan.owner)&&
                    before.local_int_check==after.local_int_check,
                "Extended input QA changed an original block, another local goal or its address");
        }
    }
    const auto& before=block_at(original,plan.owner);
    const auto& after=block_at(reviewed,plan.owner);
    const auto& projection=block_at(metadata,plan.owner);
    const auto expected=extended_input_check(plan);
    require(after.input_check&&*after.input_check==expected&&after.input_check==projection.input_check&&
            before.initial_code==after.initial_code&&after.initial_code==projection.initial_code&&
            before.standard_input==after.standard_input&&after.standard_input==projection.standard_input&&
            before.output_check&&after.output_check&&projection.output_check&&
            same_public_cases(*before.output_check,*after.output_check)&&
            same_public_cases(*after.output_check,*projection.output_check)&&
            before.output_check->input_comparison==TextComparison::tokens&&
            before.output_check->cases.size()==plan.public_cases.size()&&
            draft_key("programming",original,before)==draft_key("programming",reviewed,after),
        "Extended full/progress loading changed a public case, draft or private input owner");
    for(std::size_t index=0;index<plan.public_cases.size();++index) {
        const auto& [input,output]=plan.public_cases[index];
        const auto& actual=before.output_check->cases[index];
        require(actual.standard_input==input&&actual.outputs==std::vector<std::string>{output,output+'\n'},
            "An extended input public case lost its original labels, data or optional final LF");
    }
    require(before.standard_input==plan.public_cases.front().first&&
            state::sha256(block_at(original,plan.reference).code)==plan.reference_sha256,
        "The extended input source or first public stdin is not authentic");
    const auto dependency=reference_activity(reviewed,block_at(reviewed,plan.reference));
    require(dependency&&reviewed.pages[dependency->page].blocks[dependency->block].stable_id==plan.owner,
        "The extended reference no longer belongs to this exact editor");
    const auto manifest=JsonObject::Parse(winrt::to_hstring(file_bytes_as_string(directory/"manifest.json")));
    const auto files=manifest.GetNamedObject(L"files");
    const auto owners=manifest.GetNamedArray(L"activities");
    require(winrt::to_string(manifest.GetNamedString(L"publishedCommit"))==
                "2ca32b2337178d1effa3a2c81759e2493cd746d0"&&
            winrt::to_string(manifest.GetNamedString(L"publishedPath"))==plan.path&&
            owners.Size()==1&&winrt::to_string(owners.GetStringAt(0))==plan.owner&&
            winrt::to_string(manifest.GetNamedString(L"reference"))==plan.reference&&
            winrt::to_string(manifest.GetNamedString(L"referenceSha256"))==plan.reference_sha256&&
            winrt::to_string(files.GetNamedString(L"before.json"))==plan.source_sha256&&
            winrt::to_string(files.GetNamedString(L"after.json"))==state::sha256(reviewed.raw_json),
        "The extended input export manifest does not authenticate these exact files");
}
void run_extended_owner(const ExtendedInputPlan& plan,const Lesson& original,
        const Lesson& reviewed,const Lesson& metadata) {
    const auto& former=block_at(original,plan.owner);
    const auto& current=block_at(reviewed,plan.owner);
    const auto& projected=block_at(metadata,plan.owner);
    const auto source=block_at(original,plan.reference).code;
    const auto& first=plan.public_cases.front();
    const auto fixed="#include <iostream>\nint main() {\n    std::cout << "+
        winrt::to_string(JsonValue::CreateStringValue(winrt::to_hstring(first.second)).Stringify())+
        ";\n    return 0;\n}\n";
    const auto renamed=extended_free_names(source,plan);
    const auto final_lf=replace_once(renamed,"    return 0;\n",
        "    std::cout << '\\n';\n    return 0;\n");
    require(source!=fixed&&renamed!=source&&final_lf!=renamed,
        "Extended input positive and historical programs must be distinct sources");
    const auto options=LaunchOptions{ProfileId::Codex,
        L"input-dependency-"+std::wstring(winrt::to_hstring(plan.name).c_str())+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),
        "Extended input QA requires a fresh isolated profile");
    initialize_profile_directories(environment.paths);
    std::uint64_t sequence=40000;
    const auto make_request=[&](std::string program,bool checked=true,
                               std::optional<std::string> input=std::nullopt) {
        CppRunRequest request;
        request.request_id=++sequence;request.revision=sequence+1000;
        request.project_key=draft_key("programming",reviewed,current);
        request.source_utf8=std::move(program);
        request.stdin_utf8=input.value_or(first.first);
        request.projects_directory=environment.paths.projects;
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
            "Real extended public execution failed: "+std::string(plan.owner)+" / "+
                result.error_utf8+result.compilation.stderr_bytes+result.execution.stderr_bytes);
        require(file_bytes_as_string(result.run_directory/L"main.cpp")==request.source_utf8&&
                file_bytes_as_string(result.run_directory/L"stdin.bin")==request.stdin_utf8&&
                file_bytes_as_string(result.run_directory/L"execution-stdout.bin")==result.execution.stdout_bytes&&
                file_bytes_as_string(result.run_directory/L"execution-stderr.bin")==result.execution.stderr_bytes,
            "Extended private checks changed the original public source, stdin or console files");
        if(request.input_check)require(result.input_executable_sha256.size()==64&&
            state::sha256(state::read_bytes(result.run_directory/L"program.exe"))==result.input_executable_sha256,
            "Extended input checks did not retain the same public executable");
        return result;
    };
    const auto private_proof=[&](const CppRunRequest& request,const CppRunResult& result) {
        const auto captured=capture_cpp_execution(request,result);
        require(compiler_runtime_request_allowed(current,request)&&captured.input_check_correct&&
                !cpp_input_request_fingerprint(request).empty()&&
                captured.input_request_fingerprint==cpp_input_request_fingerprint(request)&&
                result.input_request_fingerprint==cpp_input_request_fingerprint(request)&&
                result.input_evidence.size()==current.input_check->probes.size()&&
                evaluate_cpp_output_check(*current.output_check,captured,request.revision).verdict==OutputVerdict::correct,
            "An extended correct solution lacks its independently witnessed input observations");
        for(std::size_t index=0;index<current.input_check->probes.size();++index) {
            const auto& probe=current.input_check->probes[index];
            const auto& evidence=result.input_evidence[index];
            require(evidence.source_sha256==state::sha256(request.source_utf8)&&
                    evidence.standard_input==probe.standard_input&&
                    evidence.executable_sha256==result.input_executable_sha256&&
                    evidence.execution.succeeded()&&evidence.execution.stderr_bytes.empty()&&
                    cpp_input_expected_output(evidence.execution.stdout_bytes,probe),
                "Extended private evidence belongs to another source, input, binary or result");
        }
        return captured;
    };
    state::StateData saved;
    state::prepare_lesson_state(saved,original);
    // References use the production dependency snapshot. Only the declared
    // predecessor is prepared, through its real runner and private checks;
    // this state-level regression does not claim to navigate a lesson view.
    const auto& prerequisite=prepare_extended_prerequisite(saved,original,plan,environment.paths.projects);
    saved.preferences["learning_progress"]["other_course_unchanged"]=std::int64_t{42};
    saved.preferences["native_windows"]["unrelated_reader_setting"]=std::string("keep");
    std::vector<std::pair<std::string,state::Draft>> drafts;
    for(const auto& page:original.pages)for(const auto& editor:page.blocks)
        if(editor.type==BlockType::cpp_compiler) {
            state::Draft draft;
            draft.source=(editor.stable_id==plan.owner?renamed:editor.initial_code)+"// Saved independent draft.\n";
            draft.input=" \t"+editor.standard_input.value_or("")+"\n";
            draft.selection_start=9;draft.selection_end=27;
            const auto key=draft_key("programming",original,editor);
            state::put_draft(saved,key,draft);drafts.emplace_back(key,std::move(draft));
        }
    const auto verify_drafts=[&](const state::StateData& data) {
        for(const auto& [key,expected]:drafts) {
            const auto actual=state::get_draft(data,key,"");
            require(actual.has_saved&&actual.source==expected.source&&actual.input==expected.input&&
                    actual.selection_start==expected.selection_start&&actual.selection_end==expected.selection_end,
                "Extended checking, disk reopen or Reset changed a saved draft, stdin or selection");
        }
    };

    const auto old_request=make_request(fixed,false);
    require(compiler_runtime_request_allowed(former,old_request)&&
            !compiler_runtime_request_allowed(current,old_request),
        "Extended original and revised requests have become indistinguishable");
    const auto old_result=execute(old_request,first.second);
    const auto old_capture=capture_cpp_execution(old_request,old_result);
    require(record_compiler_puzzle_case(saved,original,former,old_request,old_capture,
                "Real historical stdout from a distinct fixed-report source")&&
            compiler_puzzle_completed(saved,original,former)&&
            compiler_puzzle_case_count(saved,original,former)==1&&!has_input_witness(saved),
        "The real extended historical stdout receipt is missing");
    const auto legacy=saved;
    {
        state::StateStore writer(environment.paths.root);
        require(writer.empty(),"The extended QA profile already contains progress");
        (void)writer.load();writer.save(legacy);
    }
    {
        state::StateStore reader(environment.paths.root);
        require(!reader.empty(),"The extended historical disk receipt is missing");
        saved=reader.load();
    }
    state::validate_lesson_state(saved,reviewed);
    validate_compiler_puzzle_state(saved,metadata,projected);
    require(saved==legacy&&!compiler_puzzle_completed(saved,reviewed,current)&&
            !compiler_puzzle_completed(saved,metadata,projected)&&
            compiler_puzzle_case_count(saved,reviewed,current)==0&&
            !extended_reference_available(saved,reviewed,plan),
        "Loading the extended input goal promoted stdout history or unlocked its reference");
    auto replay_request=old_request;replay_request.input_check=current.input_check;
    const auto replay=capture_cpp_execution(replay_request,old_result);
    require(compiler_runtime_request_allowed(current,replay_request)&&
            replay.source_revision==replay_request.revision&&
            replay.input_request_fingerprint==cpp_input_request_fingerprint(replay_request)&&
            !replay.input_check_correct&&
            !record_compiler_puzzle_case(saved,reviewed,current,replay_request,replay,
                "Same request ID/revision still has no private input evidence")&&saved==legacy,
        "Extended old stdout was promoted without fresh private evidence");

    std::vector<std::pair<std::string,std::string>> negatives{{"fixed report",fixed}};
    for(const auto& [operand,value]:plan.fixed_operands)
        negatives.emplace_back("fixed "+operand,replace_once(source,plan.read_statement,
            plan.read_statement+"    "+operand+" = "+value+";\n"));
    for(const auto& [name,program]:negatives) {
        const auto request=make_request(program);
        require(compiler_runtime_request_allowed(current,request),
            "An extended negative was rejected for an unrelated request shape");
        const auto result=execute(request,first.second);
        const auto captured=capture_cpp_execution(request,result);
        require(captured.source_revision==request.revision&&
                captured.input_request_fingerprint==cpp_input_request_fingerprint(request)&&
                result.input_evidence.size()==current.input_check->probes.size()&&
                std::ranges::all_of(result.input_evidence,[](const auto& evidence) {
                    return evidence.execution.succeeded();
                })&&
                evaluate_cpp_output_check(*current.output_check,captured,request.revision).verdict==OutputVerdict::correct&&
                !captured.input_check_correct&&
                !record_compiler_puzzle_case(saved,reviewed,current,request,captured,
                    "Publicly correct but privately fixed operand")&&saved==legacy,
            "A publicly correct extended fixed-input solution earned credit: "+std::string(plan.owner)+" / "+name);
    }

    // A voluntary case is a real saved observation, but its return value is
    // false until the first public case has earned objective completion.
    const auto optional_request=make_request(source,true,plan.public_cases[1].first);
    const auto optional_result=execute(optional_request,plan.public_cases[1].second);
    const auto optional_capture=private_proof(optional_request,optional_result);
    auto partial=legacy;
    require(!record_compiler_puzzle_case(partial,reviewed,current,optional_request,optional_capture,
                "Real optional input before the first public case")&&
            !compiler_puzzle_completed(partial,reviewed,current)&&
            compiler_puzzle_case_count(partial,reviewed,current)==1&&!has_input_witness(partial)&&
            !extended_reference_available(partial,reviewed,plan),
        "An optional-only extended observation completed or unlocked the mandatory first case");
    const auto partial_environment=resolve_windows_environment({ProfileId::Codex,
        *environment.options.qa_instance+L"-optional",false});
    require(!std::filesystem::exists(partial_environment.paths.root),
        "The optional extended QA profile is not fresh");
    initialize_profile_directories(partial_environment.paths);
    {
        state::StateStore writer(partial_environment.paths.root);
        require(writer.empty(),"The optional extended QA profile contains progress");
        (void)writer.load();writer.save(partial);
    }
    {
        state::StateStore reader(partial_environment.paths.root);
        require(!reader.empty(),"The optional extended observation was not saved");
        const auto restored=reader.load();
        state::validate_lesson_state(restored,reviewed);
        validate_compiler_puzzle_state(restored,metadata,projected);
        require(restored==partial&&!compiler_puzzle_completed(restored,metadata,projected)&&
                compiler_puzzle_case_count(restored,metadata,projected)==1&&!has_input_witness(restored),
            "Optional-only extended disk state was promoted, corrupted or lost");
        preserves_old_preferences(restored,legacy);verify_drafts(restored);partial=restored;
    }
    const auto request=make_request(source);
    const auto result=execute(request,first.second);
    const auto captured=private_proof(request,result);
    require(record_compiler_puzzle_case(saved,reviewed,current,request,captured,
                "One public check and the authored private inputs")&&
            compiler_puzzle_completed(saved,reviewed,current)&&
            compiler_puzzle_completed(saved,metadata,projected)&&
            compiler_puzzle_case_count(saved,reviewed,current)==1&&has_input_witness(saved)&&
            extended_reference_available(saved,reviewed,plan),
        "One real extended first case failed to complete and open its reference");
    const auto seals=extended_input_seals(saved,reviewed,current);
    require(seals.size()==2,"The extended input goal lacks its separate completion and witness seals");
    require(record_compiler_puzzle_case(partial,reviewed,current,request,captured,
                "The first public case after an optional disk observation")&&
            compiler_puzzle_completed(partial,reviewed,current)&&
            compiler_puzzle_case_count(partial,reviewed,current)==2&&has_input_witness(partial),
        "The extended first case lost an earlier optional observation");

    for(const bool with_lf:{false,true}) {
        const auto renamed_request=make_request(with_lf?final_lf:renamed);
        const auto renamed_result=execute(renamed_request,first.second+(with_lf?"\n":""));
        const auto renamed_capture=private_proof(renamed_request,renamed_result);
        for(std::size_t index=0;index<current.input_check->probes.size();++index)
            require(cpp_input_normalize_newlines(renamed_result.input_evidence[index].execution.stdout_bytes)==
                        current.input_check->probes[index].outputs[with_lf?1u:0u],
                "The real renamed program did not exercise both optional-final-LF forms");
        auto independent=legacy;
        require(record_compiler_puzzle_case(independent,reviewed,current,renamed_request,renamed_capture,
                    "Real free-name solution with optional final LF")&&
                compiler_puzzle_completed(independent,reviewed,current)&&
                compiler_puzzle_case_count(independent,reviewed,current)==1&&
                extended_reference_available(independent,reviewed,plan),
            "The extended input contract imposed authored variable names or a final-LF spelling");
        preserves_old_preferences(independent,legacy);verify_drafts(independent);
    }
    for(std::size_t index=1;index<plan.public_cases.size();++index) {
        const auto optional=make_request(source,true,plan.public_cases[index].first);
        const auto observed=execute(optional,plan.public_cases[index].second);
        const auto proof=private_proof(optional,observed);
        require(record_compiler_puzzle_case(saved,reviewed,current,optional,proof,
                    "A voluntary original public input after completion")&&
                compiler_puzzle_case_count(saved,reviewed,current)==index+1&&
                extended_input_seals(saved,reviewed,current)==seals,
            "An optional extended case changed the earned seals or the public case count");
    }
    const auto edited=make_request(renamed+"// A new unfinished attempt.\n");
    invalidate_compiler_puzzle_attempt(saved,reviewed,current,edited);
    require(compiler_puzzle_completed(saved,reviewed,current)&&
            extended_input_seals(saved,reviewed,current)==seals,
        "Editing an extended draft erased its independently earned input achievement");
    preserves_old_preferences(saved,legacy);verify_drafts(saved);
    {
        state::StateStore writer(environment.paths.root);
        require(!writer.empty()&&writer.load()==legacy,
            "Extended in-memory experiments changed durable historical state");
        writer.save(saved);
    }
    state::StateStore reader(environment.paths.root);
    require(!reader.empty(),"The extended input result was not saved");
    const auto restored=reader.load();
    state::validate_lesson_state(restored,reviewed);
    validate_compiler_puzzle_state(restored,metadata,projected);
    require(restored==saved&&compiler_puzzle_completed(restored,original,former)&&
            compiler_puzzle_case_count(restored,original,former)==1&&
            compiler_puzzle_completed(restored,reviewed,current)&&
            compiler_puzzle_completed(restored,metadata,projected)&&
            compiler_puzzle_case_count(restored,reviewed,current)==plan.public_cases.size()&&
            extended_input_seals(restored,reviewed,current)==seals&&
            extended_reference_available(restored,reviewed,plan),
        "Extended disk reopen lost history, optional cases, input seals or reference access");
    preserves_old_preferences(restored,legacy);verify_drafts(restored);
    auto prerequisite_missing=restored;
    reset_compiler_puzzle(prerequisite_missing,reviewed,block_at(reviewed,prerequisite.stable_id));
    require(compiler_puzzle_completed(prerequisite_missing,reviewed,current)&&
            !extended_reference_available(prerequisite_missing,reviewed,plan),
        "An extended reference bypassed its actual activityAfter prerequisite");
    auto reset=restored;
    reset_compiler_puzzle(reset,reviewed,current);
    require(!compiler_puzzle_completed(reset,reviewed,current)&&
            compiler_puzzle_case_count(reset,reviewed,current)==0&&
            extended_input_seals(reset,reviewed,current).empty()&&
            compiler_puzzle_completed(reset,original,former)&&
            compiler_puzzle_case_count(reset,original,former)==1&&
            !extended_reference_available(reset,reviewed,plan),
        "Extended Reset erased stdout history or retained the new input achievement");
    preserves_old_preferences(reset,legacy);verify_drafts(reset);
    std::cout<<"PASS "<<plan.owner<<": exact main source; "<<plan.private_cases.size()
        <<" private inputs of the same executable; fixed report and "<<plan.fixed_operands.size()
        <<" fixed operands rejected; free names and both final-LF forms; one first case passes its reference gate; "
        <<"all "<<plan.public_cases.size()<<" public cases, optional-only false return, distinct historical source, "
        <<"actual declared prerequisite, separate seals, drafts/stdin/selections, disk reopen and scoped Reset preserved.\n";
}
void run_extended_inputs(const std::filesystem::path& root) {
    for(const auto& plan:extended_input_plans()) {
        const auto directory=root/std::filesystem::path(plan.name);
        const auto before_bytes=state::read_bytes(directory/"before.json");
        const auto after_bytes=state::read_bytes(directory/"after.json");
        const auto manifest_bytes=state::read_bytes(directory/"manifest.json");
        // The first loader initializes WinRT before any JSON helper is used.
        const auto original=load_lesson(directory/"before.json");
        const auto reviewed=load_lesson(directory/"after.json");
        const auto metadata=parse_lesson_progress_metadata(reviewed.raw_json,"es");
        validate_extended_fixture(plan,original,reviewed,metadata,directory);
        std::cout<<"RUN extended input "<<plan.owner<<"\n"<<std::flush;
        run_extended_owner(plan,original,reviewed,metadata);
        require(state::read_bytes(directory/"before.json")==before_bytes&&
                state::read_bytes(directory/"after.json")==after_bytes&&
                state::read_bytes(directory/"manifest.json")==manifest_bytes,
            "Extended native QA changed its authentic source, proposal or manifest");
    }
}

} // namespace

int main(int argc,char** argv) {
    const bool extended=argc==3&&std::string_view(argv[1])=="--extended-input";
    try {
        if(extended){run_extended_inputs(std::filesystem::path(argv[2]));return 0;}
        require(argc==2&&std::string_view(argv[1])!="--extended-input",
            "Usage: input_dependency_state_tests <u2l9-fixture-directory> | --extended-input <root>");
        const auto directory=std::filesystem::path(argv[1]);
        // load_lesson initializes WinRT before any JsonObject is constructed.
        const auto original=load_lesson(directory/"before.json");
        const auto reviewed=load_lesson(directory/"after.json");
        const auto metadata=parse_lesson_progress_metadata(reviewed.raw_json,"es");
        validate_fixture(original,reviewed,metadata);
        validate_schema(original,reviewed);
        validate_same_output_schema(original,reviewed);
        validate_legacy_input_fingerprint();
        for(std::size_t index=0;index<identities.size();++index)
            run_owner(original,reviewed,metadata,index);
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<(extended?"FAIL extended input dependency: ":"FAIL U2L9 input dependency: ")<<error.what()<<'\n';
        return 1;
    }
}
