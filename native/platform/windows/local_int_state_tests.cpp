// Opt-in local-int authoring and real StateStore receipt integration.
// This fixture never invokes the student's compiler or modifies curriculum.
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <array>
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
void rejects_raw(std::string_view serialized) {
    for(const bool metadata:{false,true}) {
        bool failed=false;
        try {
            if(metadata)(void)parse_lesson_progress_metadata(serialized,"es");
            else (void)parse_lesson_json(serialized,"es");
        }catch(const std::exception&) {failed=true;}
        require(failed,"Invalid local-int authoring passed a native load path");
    }
}
void rejects(const JsonObject& root) {
    rejects_raw(winrt::to_string(root.Stringify()));
}
std::string overwrite_first(std::string source,std::string_view old_text,
        std::string_view new_text) {
    const auto at=source.find(old_text);
    require(at!=source.npos,"An original duplicate-field test token is missing");
    source.replace(at,old_text.size(),new_text);
    return source;
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
// Test the six real, still-unmodified U2 lesson editors, not synthetic IDs.
// Authoring packets exist only in memory: Work retains control of lesson JSON.
void validate_other_u2_editors() {
    struct LessonCase {
        const char* filename;
        const char* declaration;
        std::array<std::string_view,2> owners;
        std::array<std::string_view,2> sources;
    };
    const std::array<LessonCase,3> cases{{
        {
            "lesson_objetos_valores_y_tipos.json",
            R"JSON([
                {"activity":"block_0004_0002","probes":[
                    {"variable":"llaves","replacement":13,"outputs":["Llaves: 13","Llaves: 13\n"]}]},
                {"activity":"block_0004_0006","probes":[
                    {"variable":"llaves","replacement":19,"outputs":["Llaves: 19","Llaves: 19\n"]}]}
            ])JSON",
            {"block_0004_0002","block_0004_0006"},
            {
                R"CPP(#include <iostream>
int main(){int llaves=7; std::cout<<"Llaves: "<<llaves;})CPP",
                R"CPP(#include <iostream>
int main(){int llaves{11}; std::cout<<"Llaves: "<<llaves;})CPP"
            }
        },{
            "lesson_declarar_e_inicializar.json",
            R"JSON([
                {"activity":"block_0004_0002","probes":[
                    {"variable":"cajas","replacement":13,"outputs":["Cajas: 13\nSobres: 9","Cajas: 13\nSobres: 9\n"]},
                    {"variable":"sobres","replacement":17,"outputs":["Cajas: 4\nSobres: 17","Cajas: 4\nSobres: 17\n"]}]},
                {"activity":"block_0004_0006","probes":[
                    {"variable":"cajas","replacement":6,"outputs":["Cajas: 6\nSobres: 2","Cajas: 6\nSobres: 2\n"]},
                    {"variable":"sobres","replacement":17,"outputs":["Cajas: 0\nSobres: 17","Cajas: 0\nSobres: 17\n"]}]}
            ])JSON",
            {"block_0004_0002","block_0004_0006"},
            {
                R"CPP(#include <iostream>
int main(){
    int cajas=4; int sobres{9};
    std::cout<<"Cajas: "<<cajas<<"\nSobres: "<<sobres;
})CPP",
                R"CPP(#include <iostream>
int main(){
    int cajas{0}; int sobres=2;
    std::cout<<"Cajas: "<<cajas<<"\nSobres: "<<sobres;
})CPP"
            }
        },{
            "lesson_estado_asignacion_y_copia.json",
            R"JSON([
                {"activity":"block_0006_0002","probes":[
                    {"variable":"aforo","replacement":10,"outputs":["Antes: 10\nAhora: 13","Antes: 10\nAhora: 13\n"]},
                    {"variable":"anterior","replacement":11,"outputs":["Antes: 11\nAhora: 8","Antes: 11\nAhora: 8\n"]},
                    {"variable":"aforo","replacement":4,"afterUpdate":true,
                     "outputs":["Antes: 5\nAhora: 12","Antes: 5\nAhora: 12\n"]}]},
                {"activity":"block_0006_0006","probes":[
                    {"variable":"aforo","replacement":10,"outputs":["Antes: 13\nAhora: 13","Antes: 13\nAhora: 13\n"]},
                    {"variable":"anterior","replacement":11,"outputs":["Antes: 11\nAhora: 8","Antes: 11\nAhora: 8\n"]},
                    {"variable":"aforo","replacement":4,"afterUpdate":true,
                     "outputs":["Antes: 12\nAhora: 12","Antes: 12\nAhora: 12\n"]}]}
            ])JSON",
            {"block_0006_0002","block_0006_0006"},
            {
                R"CPP(#include <iostream>
int main(){
    int aforo=5; int anterior=aforo; aforo+=3;
    std::cout<<"Antes: "<<anterior<<"\nAhora: "<<aforo;
})CPP",
                R"CPP(#include <iostream>
int main(){
    int aforo{5}; aforo+=3; int anterior=aforo;
    std::cout<<"Antes: "<<anterior<<"\nAhora: "<<aforo;
})CPP"
            }
        }
    }};
    const auto root=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado";
    for(const auto& item:cases) {
        const auto published=load_lesson(root/item.filename);
        auto active=JsonObject::Parse(winrt::to_hstring(published.raw_json));
        active.Insert(L"compilerLocalIntChecks",
            JsonValue::Parse(winrt::to_hstring(std::string(item.declaration))));
        const auto original=winrt::to_string(active.Stringify());
        const auto loaded=parse_lesson_json(original,"es");
        const auto metadata=parse_lesson_progress_metadata(original,"es");
        for(std::size_t index=0;index<item.owners.size();++index) {
            const auto lookup=[&](const Lesson& lesson)->const Block& {
                const auto position=stable_block_position(lesson,item.owners[index]);
                require(position.has_value(),"An original U2 editor was displaced");
                return lesson.pages.at(position->page).blocks.at(position->block);
            };
            const auto& before=lookup(published);
            const auto& after=lookup(loaded);
            const auto& progress=lookup(metadata);
            require(before.output_check&&after.output_check&&
                before.output_check->cases.size()==1&&
                after.output_check->cases.size()==1&&
                same_output_case_requirements(before.output_check->cases.front(),
                    after.output_check->cases.front())&&
                after.local_int_check&&
                after.local_int_check==progress.local_int_check&&
                !before.local_int_check&&
                after.address.page==before.address.page&&
                after.address.block==before.address.block&&
                after.initial_code==before.initial_code,
                "New U2 root checks replaced, moved or silently credited a published editor");
            CppRunRequest request;
            request.request_id=17;request.revision=9;
            request.project_key="isolated-six-u2-owners";
            request.source_utf8=std::string(item.sources[index]);
            request.local_int_check=after.local_int_check;
            require(compiler_runtime_request_allowed(after,request)&&
                !cpp_local_int_request_fingerprint(request).empty(),
                "A valid U2 answer could not prepare its separate private variations");
            // A printout without the requested int variable cannot prepare
            // a complete private proof, however its stdout may look right.
            request.source_utf8=
                "#include <iostream>\nint main(){std::cout<<\"Llaves: 7\";}";
            require(cpp_local_int_request_fingerprint(request).empty()&&
                !compiler_runtime_request_allowed(after,request),
                "An uncomputed constant was eligible for local-int credit");
        }
    }
    std::cout<<"PASS six real U2 compiler owners: authentic root declarations "
        "for llaves/cajas/sobres/aforo/anterior, matching progress loader, "
        "safe source mutations and unchanged historical exercises.\n";
}

void run() {
    validate_other_u2_editors();
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
    // Probe collection and grading must be bound to the actual request,
    // source variant and each independent Win32 process observation. These
    // synthetic phase records exercise the real capture function; they are
    // NOT presented as executions of an installed student compiler.
    CppRunResult observed;
    observed.request_id=request.request_id;
    observed.revision=request.revision;
    observed.local_int_request_fingerprint=probe;
    observed.compilation.status=CppPhaseStatus::completed;
    observed.compilation.exit_code=0;
    observed.execution.status=CppPhaseStatus::completed;
    observed.execution.exit_code=0;
    observed.execution.stdout_bytes="Llaves: 7";
    for(const auto& variation:request.local_int_check->probes) {
        const auto source_variant=cpp_local_int_override(request.source_utf8,variation);
        require(source_variant.has_value(),
            "The Windows capture cannot derive a declared independent source");
        CppLocalIntEvidence evidence;
        evidence.instrumented_source_sha256=state::sha256(*source_variant);
        evidence.compilation=observed.compilation;
        evidence.execution=observed.execution;
        evidence.execution.stdout_bytes=variation.expected_stdout.front();
        observed.local_int_evidence.push_back(std::move(evidence));
    }
    const auto first_capture=capture_cpp_execution(request,observed);
    require(first_capture.local_int_check_correct&&
        first_capture.local_int_request_fingerprint==probe,
        "Two bound private phases failed to authorize their original source");
    auto tampered=observed;
    tampered.local_int_evidence.back().instrumented_source_sha256="foreign";
    require(!capture_cpp_execution(request,tampered).local_int_check_correct,
        "An unrelated instrumented source was accepted as a native probe");
    tampered=observed;
    tampered.local_int_evidence.front().execution.stdout_bytes="Llaves: 7";
    require(!capture_cpp_execution(request,tampered).local_int_check_correct,
        "Hard-coded visible output passed a changed-int phase");
    tampered=observed;
    tampered.local_int_evidence.back().execution.exit_code=1;
    require(!capture_cpp_execution(request,tampered).local_int_check_correct,
        "A failed independent process earned a successful variable receipt");
    tampered=observed;
    tampered.local_int_evidence.pop_back();
    require(!capture_cpp_execution(request,tampered).local_int_check_correct,
        "One successful phase hid a missing second mutation");
    tampered=observed;
    tampered.local_int_request_fingerprint="other-program";
    require(!capture_cpp_execution(request,tampered).local_int_check_correct,
        "Probes from another request acquired the learner's identity");
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

    // Windows.Data.Json normally merges duplicate object keys. Exercise
    // both the full loader and the progress-side loader with raw, valid JSON
    // instead of pre-parsing the malicious source and losing the duplicates.
    const auto canonical=winrt::to_string(raw.Stringify());
    const auto duplicated_root=std::string("{\"compilerLocalIntChecks\":")+
        winrt::to_string(raw.GetNamedArray(L"compilerLocalIntChecks").Stringify())+
        ","+canonical.substr(1);
    rejects_raw(duplicated_root);
    rejects_raw(overwrite_first(canonical,
        "\"activity\":\"block_0004_0002\"",
        "\"activity\":\"block_0004_0002\",\"activity\":\"block_0004_0002\""));
    rejects_raw(overwrite_first(canonical,
        "\"replacement\":13",
        "\"replacement\":13,\"replacement\":17"));
    rejects_raw(overwrite_first(canonical,
        "\"variable\":\"llaves\"",
        "\"variable\":\"llaves\",\"variable\":\"llaves\""));
    rejects_raw(overwrite_first(canonical,
        "\"variable\":\"llaves\"",
        "\"variable\":\"llaves\",\"afterUpdate\":true,\"afterUpdate\":true"));
    const auto local_int_start=canonical.find("\"compilerLocalIntChecks\"");
    require(local_int_start!=canonical.npos,
        "The canonical JSON lost the local-int declaration");
    rejects_raw(canonical.substr(0,local_int_start)+
        overwrite_first(canonical.substr(local_int_start),
            "\"outputs\":[\"",
            "\"outputs\":[],\"outputs\":[\""));
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
    JsonArray disguised_newline;
    disguised_newline.Append(JsonValue::CreateStringValue(L"Llaves: 7\r\n"));
    first.Insert(L"outputs",disguised_newline);
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
