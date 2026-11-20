// Opt-in local-int authoring and real StateStore receipt integration.
// This fixture never invokes the student's compiler or modifies curriculum.
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

void require(bool condition,std::string_view reason) {
    if(!condition)throw std::runtime_error(std::string(reason));
}
const Block& owner(const Lesson& lesson) {
    const auto position=stable_block_position(lesson,"block_0004_0002");
    require(position.has_value(),"Published U2L1 compiler owner was not found");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
void rejects(const JsonObject& root) {
    const auto serialized=winrt::to_string(root.Stringify());
    for(const bool metadata:{false,true}) {
        bool failed=false;
        try {
            if(metadata)(void)parse_lesson_progress_metadata(serialized,"es");
            else (void)parse_lesson_json(serialized,"es");
        }catch(const std::exception&) {failed=true;}
        require(failed,"Invalid local-int authoring passed a native load path");
    }
}
FreshExecution visible_result(std::uint64_t revision,std::string source={}) {
    FreshExecution result;
    result.source_revision=revision;
    result.compilation_succeeded=true;
    result.execution_succeeded=true;
    result.execution_completed=true;
    result.exit_code=0;
    result.standard_output="Llaves: 7";
    result.standard_input="";
    (void)source;
    return result;
}
CppRunRequest request_for(const Lesson& lesson,std::string source) {
    CppRunRequest result;
    result.request_id=17;
    result.revision=31;
    result.project_key="isolated-local-int-u2";
    result.source_utf8=std::move(source);
    result.local_int_check=owner(lesson).local_int_check;
    return result;
}
std::string saved_local_receipt(const state::StateData& data) {
    const auto found=data.preferences.find("native_windows");
    if(found==data.preferences.end())return {};
    for(const auto& [key,value]:found->second)
        if(key.ends_with("/local-int/v1")) {
            const auto text=std::get_if<std::string>(&value);
            return text?*text:std::string{};
        }
    return {};
}
void run() {
    const auto file=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_objetos_valores_y_tipos.json";
    const auto old=load_lesson(file);
    const auto& former=owner(old);
    require(!former.local_int_check&&former.output_check&&
        former.output_check->cases.size()==1,
        "The published U2L1 owner was modified rather than extended at the lesson root");
    const auto raw=JsonObject::Parse(winrt::to_hstring(old.raw_json));
    const std::string declaration=R"([{
       "activity":"block_0004_0002",
       "probes":[
          {"variable":"llaves","replacement":13,
           "outputs":["Llaves: 13","Llaves: 13\n"]},
          {"variable":"llaves","replacement":-4,
           "outputs":["Llaves: -4","Llaves: -4\n"]}
       ]
    }])";
    raw.Insert(L"compilerLocalIntChecks",
        JsonValue::Parse(winrt::to_hstring(declaration)));
    const auto current=parse_lesson_json(winrt::to_string(raw.Stringify()),"es");
    const auto metadata=parse_lesson_progress_metadata(
        winrt::to_string(raw.Stringify()),"es");
    const auto& block=owner(current);
    require(block.local_int_check&&block.local_int_check->probes.size()==2&&
        owner(metadata).local_int_check==block.local_int_check&&
        winrt::to_string(JsonObject::Parse(winrt::to_hstring(block.raw_json)).Stringify())==
        winrt::to_string(JsonObject::Parse(winrt::to_hstring(former.raw_json)).Stringify())&&
        block.initial_code==former.initial_code&&
        block.output_check->cases.size()==former.output_check->cases.size()&&
        same_output_case_requirements(block.output_check->cases.front(),
                                      former.output_check->cases.front())&&
        block.output_check->cases.front().explanation==
            former.output_check->cases.front().explanation&&
        block.address.page==former.address.page&&
        block.address.block==former.address.block&&
        block.address.addition_namespace==former.address.addition_namespace&&
        block.address.progress_page==former.address.progress_page,
        "Root-only authorship changed the existing compiler exercise or was lost in metadata");
    const std::string source="#include <iostream>\nint main(){int llaves{7}; std::cout << \"Llaves: \" << llaves;}\n";
    auto old_request=request_for(old,source);
    auto request=request_for(current,source);
    const auto probe=cpp_local_int_request_fingerprint(request);
    require(!probe.empty()&&compiler_runtime_request_allowed(block,request)&&
        !compiler_runtime_request_allowed(block,old_request),
        "The new owner cannot bind a complete student request");
    auto old_fresh=visible_result(old_request.revision);
    state::StateData learner;
    learner.preferences["learning_progress"]["another_course_untouched"]=std::int64_t{7};
    require(record_compiler_puzzle_case(learner,old,former,old_request,old_fresh,
                "Old stdout success")&&
            compiler_puzzle_completed(learner,old,former),
        "The original output requirement was not accredited in the isolated model");
    const auto before=learner;
    require(!compiler_puzzle_completed(learner,current,block)&&
            compiler_puzzle_case_count(learner,current,block)==1&&
            saved_local_receipt(learner).empty()&&learner==before,
        "An old stdout-only result forged semantic dependency credit");
    auto fresh=visible_result(request.revision);
    fresh.local_int_request_fingerprint=probe;
    require(!record_compiler_puzzle_case(learner,current,block,request,fresh,
               "Unverified source")&&learner==before,
        "Visible stdout silently passed a missing private probe");
    fresh.local_int_check_correct=true;
    fresh.local_int_request_fingerprint="wrong-fingerprint";
    require(!record_compiler_puzzle_case(learner,current,block,request,fresh,
               "Forged source")&&learner==before,
        "A private proof from another request was accepted");
    fresh.local_int_request_fingerprint=probe;
    require(record_compiler_puzzle_case(learner,current,block,request,fresh,
               "Dependency verified")&&
            compiler_puzzle_completed(learner,current,block)&&
            !saved_local_receipt(learner).empty()&&
            learner.preferences.at("learning_progress")==
                before.preferences.at("learning_progress"),
        "Correct independent proof did not preserve unrelated progress");
    // Existing base program and completion seal are not overwritten by
    // adding the independently earned proof to the old stdout receipt.
    for(const auto& [key,value]:before.preferences.at("native_windows"))
        require(learner.preferences.at("native_windows").at(key)==value,
            "The original program, cases, feedback or first-case seal was rewritten");
    const auto decoded=state::decode(state::encode(learner));
    require(compiler_puzzle_completed(decoded,current,block),
        "Real encoded StateStore lost the independent proof on reopen");
    auto corrupted=decoded;
    for(auto& [key,value]:corrupted.preferences["native_windows"])
        if(key.ends_with("/local-int/v1"))value=std::string("forged");
    bool rejected=false;
    try{validate_compiler_puzzle_state(corrupted,current,block);}
    catch(const std::exception&){rejected=true;}
    require(rejected,"Imported semantic proof tampering was silently accepted");

    auto wrong=JsonObject::Parse(winrt::to_hstring(raw.Stringify()));
    auto first=wrong.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0)
        .GetNamedArray(L"probes").GetObjectAt(0);
    JsonArray trivial;
    trivial.Append(JsonValue::CreateStringValue(L"Llaves: 7"));
    first.Insert(L"outputs",trivial);
    rejects(wrong);
    wrong=JsonObject::Parse(winrt::to_hstring(raw.Stringify()));
    first=wrong.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0)
        .GetNamedArray(L"probes").GetObjectAt(0);
    first.Insert(L"variable",JsonValue::CreateStringValue(L"llaves_no_existentes"));
    // A declared identifier must also be present in a submitted program;
    // native authoring alone does not claim a specific student's solution.
    const auto declared=parse_lesson_json(winrt::to_string(wrong.Stringify()),"es");
    auto missing=request_for(declared,source);
    require(cpp_local_int_request_fingerprint(missing).empty()&&
            !compiler_runtime_request_allowed(owner(declared),missing),
        "Missing required local variable passed the private source mutation");
    std::cout<<"PASS Windows local-int contract: two load paths, existing U2L1 owner, "
        "uncredited old stdout, separate same-source receipt, StateStore reopen, "
        "tampering rejection and absent-variable rejection; no student execution claimed.\n";
}
}
int main()try {
    winrt::init_apartment();
    run();
    return 0;
}catch(const winrt::hresult_error& e){
    std::cerr<<winrt::to_string(e.message())<<'\n';
    return 1;
}catch(const std::exception& e){
    std::cerr<<"FAIL local-int state: "<<e.what()<<'\n';
    return 1;
}
