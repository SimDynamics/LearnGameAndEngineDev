// Opt-in local-int authoring and real StateStore receipt integration.
// This fixture never invokes the student's compiler or modifies curriculum.
#include "core/compiler_flow.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
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

// A real on-disk round trip, always under a unique QA instance of Codex.
// Do not open, migrate or rewrite either personal Work/Codex profile.
state::StateData reopen_isolated_int_qa_state(
        const state::StateData& saved,std::wstring_view scenario) {
    require(!scenario.empty(),"A local-int test needs a named QA instance");
    const auto label=L"local-int-"+std::wstring(scenario)+
        L"-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64());
    const auto options=LaunchOptions{ProfileId::Codex,label,false};
    const auto environment=resolve_windows_environment(options);
    require(environment.options.qa_instance.has_value()&&
            !environment.options.qa_instance->empty(),
        "Local-int persistence would target a personal profile");
    initialize_profile_directories(environment.paths);
    {
        state::StateStore writer(environment.paths.root);
        require(writer.empty(),
            "A supposedly fresh local-int QA profile already has progress");
        (void)writer.load();
        writer.save(saved);
    }
    // Close the writer before constructing a second StateStore. A reload
    // through the same object also reads bytes but would not verify that
    // a new owner discovers a durable, committed state from disk.
    state::StateStore reopened(environment.paths.root);
    require(!reopened.empty(),
        "A completed local-int QA save did not publish an active state file");
    return reopened.load();
}

const Block& owner(const Lesson& lesson) {
    const auto position=stable_block_position(lesson,"block_0004_0002");
    require(position.has_value(),"Published U2L1 compiler owner was not found");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
void rejects_raw(std::string_view serialized) {
    // Every malformed-document variant must fail on BOTH native load paths.
    // Number the invocations so a Windows CI failure identifies precisely
    // which adversarial input and loader path still accept a duplicate key.
    static unsigned scenario=0;
    const auto case_number=++scenario;
    for(const bool metadata:{false,true}) {
        bool failed=false;
        try {
            if(metadata)(void)parse_lesson_progress_metadata(serialized,"es");
            else (void)parse_lesson_json(serialized,"es");
        }catch(const std::exception&) {failed=true;}
        if(!failed)
            throw std::runtime_error("Invalid local-int authoring scenario "+
                std::to_string(case_number)+" passed "+
                (metadata?"progress metadata":"full lesson")+" loader");
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
void validate_other_u2_editors(bool export_plan=false) {
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
    if(export_plan){
        // The same in-memory opt-in proposal used by the WinRT state tests
        // can be exercised with REAL hosted MSVC. Print no student data and
        // never change a published lesson or its historical identities.
        JsonArray examples;
        for(const auto& item:cases){
            JsonObject entry;
            entry.Insert(L"filename",JsonValue::CreateStringValue(winrt::to_hstring(item.filename)));
            entry.Insert(L"checks",JsonValue::Parse(winrt::to_hstring(std::string(item.declaration))));
            JsonArray answers;
            for(std::size_t i=0;i<item.owners.size();++i){
                JsonObject owner;
                owner.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(item.owners[i])));
                owner.Insert(L"source",JsonValue::CreateStringValue(winrt::to_hstring(item.sources[i])));
                answers.Append(owner);
            }
            entry.Insert(L"answers",answers);
            examples.Append(entry);
        }
        std::cout<<winrt::to_string(examples.Stringify())<<'\n';
        return;
    }
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
        if(std::string_view(item.filename)=="lesson_estado_asignacion_y_copia.json") {
            // This is a stronger, optional authoring edge on the real two
            // U2L3 editors; the shared export plan and original assets are
            // deliberately unchanged. Loading both representations must
            // retain its exact increase and derive a DIFFERENT private
            // fingerprint, not borrow an earlier afterUpdate receipt.
            auto exact=JsonObject::Parse(active.Stringify());
            const auto owned=exact.GetNamedArray(L"compilerLocalIntChecks");
            require(owned.Size()==2,"Current U2L3 lost one original editor");
            for(unsigned index=0;index<2;++index) {
                const auto proposals=owned.GetObjectAt(index).GetNamedArray(L"probes");
                require(proposals.Size()==3,
                    "U2L3 needs its original two probes and one increase witness");
                proposals.GetObjectAt(2).Insert(
                    L"expectedIncrease",JsonValue::CreateNumberValue(3));
            }
            const auto publication=winrt::to_string(exact.Stringify());
            const auto full=parse_lesson_json(publication,"es");
            const auto compact=parse_lesson_progress_metadata(publication,"es");
            for(std::size_t index=0;index<item.owners.size();++index) {
                const auto identity=item.owners[index];
                const auto position=stable_block_position(full,identity);
                const auto before_at=stable_block_position(loaded,identity);
                const auto compact_at=stable_block_position(compact,identity);
                require(position&&before_at&&compact_at,
                    "The exact U2L3 authoring lost its original stable owner");
                const auto& updated=full.pages.at(position->page).blocks.at(position->block);
                const auto& previous=loaded.pages.at(before_at->page).blocks.at(before_at->block);
                const auto& progress=compact.pages.at(compact_at->page).blocks.at(compact_at->block);
                require(updated.local_int_check&&previous.local_int_check&&
                    updated.local_int_check==progress.local_int_check&&
                    updated.local_int_check->probes.size()==3&&
                    updated.local_int_check->probes.back().after_update&&
                    updated.local_int_check->probes.back().expected_increase==3&&
                    !previous.local_int_check->probes.back().expected_increase,
                    "A stronger U2L3 increase did not retain exact full/compact authoring");
                CppRunRequest request;
                request.request_id=61;
                request.revision=9;
                request.project_key="u2l3-exact-increase-source-only";
                request.source_utf8=std::string(item.sources[index]);
                request.local_int_check=updated.local_int_check;
                const auto proof=cpp_local_int_request_fingerprint(request);
                require(!proof.empty()&&compiler_runtime_request_allowed(updated,request),
                    "A correct, unchanged U2L3 reference failed exact +3 authoring");
                request.local_int_check=previous.local_int_check;
                const auto legacy=cpp_local_int_request_fingerprint(request);
                require(!legacy.empty()&&legacy!=proof,
                    "A prior +positive receipt could be replayed for exact +3");
            }
            // JSON authoring is stricter than the C++ temporary fixture:
            // reject a signed-zero, negative, floating, bool or out-of-int
            // expected delta, and do not allow it without afterUpdate.
            for(const auto invalid:{0.0,-3.0,3.5,2147483648.0}) {
                auto changed=JsonObject::Parse(exact.Stringify());
                changed.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0)
                    .GetNamedArray(L"probes").GetObjectAt(2).Insert(
                        L"expectedIncrease",JsonValue::CreateNumberValue(invalid));
                rejects(changed);
            }
            auto bool_value=JsonObject::Parse(exact.Stringify());
            bool_value.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0)
                .GetNamedArray(L"probes").GetObjectAt(2).Insert(
                    L"expectedIncrease",JsonValue::CreateBooleanValue(true));
            rejects(bool_value);
            auto without_moment=JsonObject::Parse(exact.Stringify());
            without_moment.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0)
                .GetNamedArray(L"probes").GetObjectAt(2).Remove(L"afterUpdate");
            rejects(without_moment);
            const auto authored_at=publication.find("\"compilerLocalIntChecks\"");
            require(authored_at!=publication.npos,"Cannot locate U2L3 exact root");
            rejects_raw(publication.substr(0,authored_at)+overwrite_first(
                publication.substr(authored_at),
                "\"expectedIncrease\":3",
                "\"expectedIncrease\":3,\"expectedIncrease\":3"));
        }
    }
    std::cout<<"PASS six real U2 compiler owners: authentic root declarations "
        "for llaves/cajas/sobres/aforo/anterior, matching progress loader, "
        "safe source mutations and unchanged historical exercises.\n";
}


// Cross-editor WinRT state regression for the CURRENT U2L5 publication.
// This uses real content/progress loaders and the existing state codec, but
// models the compiler's verified outcome: it does NOT execute a student
// compiler, create listening evidence or claim Win32 UI acceptance.
void validate_current_u2l5_editor_state() {
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_enteros_y_aproximaciones.json";
    const auto lesson=load_lesson(path);
    const auto compact=parse_lesson_progress_metadata(lesson.raw_json,"es");
    const auto first_at=stable_block_position(lesson,"block_0006_0002");
    const auto second_at=stable_block_position(lesson,"p1u2l5_reparar_agrupacion");
    const auto first_ref=stable_block_position(lesson,"block_0007_0001");
    const auto second_ref=stable_block_position(lesson,"p1u2l5_referencia_agrupacion");
    require(first_at&&second_at&&first_ref&&second_ref&&
        first_at->page==6&&first_at->block==2&&
        second_at->page==7&&second_at->block==9&&
        first_ref->page==7&&second_ref->page==8,
        "The current U2L5 editor or its reference changed stable position");
    const auto& first=lesson.pages.at(first_at->page).blocks.at(first_at->block);
    const auto& second=lesson.pages.at(second_at->page).blocks.at(second_at->block);
    const auto& reference_one=lesson.pages.at(first_ref->page).blocks.at(first_ref->block);
    const auto& reference_two=lesson.pages.at(second_ref->page).blocks.at(second_ref->block);
    const auto compact_owner=[&](std::string_view identity)->const Block& {
        const auto at=stable_block_position(compact,identity);
        require(at.has_value(),"Compact progress loader lost an active U2L5 editor");
        return compact.pages.at(at->page).blocks.at(at->block);
    };
    require(lesson.compiler_puzzles_required&&first.output_check&&second.output_check&&
        first.output_check->cases.size()==1&&second.output_check->cases.size()==1&&
        first.output_check->cases.front().outputs.front()=="Parte: 4.5 m"&&
        second.output_check->cases.front().outputs.front()=="Parte: 2.75 m"&&
        first.local_int_check&&second.local_int_check&&
        first.local_int_check==compact_owner(first.stable_id).local_int_check&&
        second.local_int_check==compact_owner(second.stable_id).local_int_check&&
        reference_one.type==BlockType::code&&reference_two.type==BlockType::code&&
        !reference_one.code.empty()&&!reference_two.code.empty(),
        "Full and compact U2L5 loaders disagree on the two authored objectives");
    const auto& archives=lesson.state_layout.retirement_history;
    require(archives.size()==1&&archives.front().current.size()>7&&
        archives.front().current[6].blocks.size()>6&&
        archives.front().current[7].blocks.size()>9&&
        archives.front().current[6].blocks[2]=="block_0006_0002"&&
        archives.front().current[6].blocks[6]=="block_0006_0006"&&
        archives.front().current[7].blocks[9]=="block_0007_0009"&&
        !stable_block_position(lesson,"block_0006_0006")&&
        !stable_block_position(lesson,"block_0007_0009"),
        "The archived three-editor chain became an active U2L5 obligation");
    const auto prerequisites=activity_prerequisites(lesson,second);
    require(prerequisites.size()==1&&prerequisites.front()==*first_at,
        "The current second editor no longer depends on the first");

    state::StateData learner;
    learner.preferences["learning_progress"]["other_course_unchanged"]=std::int64_t{42};
    learner.preferences["native_windows"]["unrelated_reader_setting"]=std::string("keep");
    state::prepare_lesson_state(learner,lesson);
    const auto first_key=draft_key("programming",lesson,first);
    const auto second_key=draft_key("programming",lesson,second);
    require(first_key!=second_key,"Two real editors share one draft address");
    state::Draft draft_one;
    draft_one.source=reference_one.code+"// QA draft one\n";
    draft_one.input="private first draft";
    draft_one.selection_start=3;draft_one.selection_end=9;
    state::Draft draft_two;
    draft_two.source=reference_two.code+"// QA draft two\n";
    draft_two.input="private second draft";
    draft_two.selection_start=4;draft_two.selection_end=10;
    state::put_draft(learner,first_key,draft_one);
    state::put_draft(learner,second_key,draft_two);
    const auto verify_drafts=[&](const state::StateData& stored) {
        const auto a=state::get_draft(stored,first_key,first.initial_code);
        const auto b=state::get_draft(stored,second_key,second.initial_code);
        require(a.has_saved&&b.has_saved&&a.source==draft_one.source&&
            b.source==draft_two.source&&a.input==draft_one.input&&
            b.input==draft_two.input&&
            a.selection_start==draft_one.selection_start&&
            b.selection_start==draft_two.selection_start&&
            a.selection_end==draft_one.selection_end&&
            b.selection_end==draft_two.selection_end,
            "U2L5 compiler receipts or reopening merged independent source drafts");
    };
    const auto snapshot=[&](const state::StateData& data) {
        return compiler_completion_snapshot(lesson,[&](const Block& block) {
            return compiler_puzzle_completed(data,lesson,block);
        });
    };
    const auto visible=[&](const state::StateData& data,bool first_reference) {
        const auto& position=first_reference?*first_ref:*second_ref;
        const auto& block=first_reference?reference_one:reference_two;
        return compiler_reference_available(lesson,position.page,block,snapshot(data),
            [&](const Block& owner){return compiler_puzzle_completed(data,lesson,owner);});
    };
    require(!compiler_puzzle_completed(learner,lesson,first)&&
        !compiler_puzzle_completed(learner,lesson,second)&&
        !compiler_activity_available(lesson,second,snapshot(learner))&&
        !visible(learner,true)&&!visible(learner,false),
        "A fresh U2L5 profile inherited compiler proof or unlocked its references");

    const auto request_for_owner=[&](const Block& block,std::string source) {
        CppRunRequest request;request.request_id=71;request.revision=41;
        request.project_key="u2l5-private-state-only";
        request.source_utf8=std::move(source);
        request.local_int_check=block.local_int_check;
        return request;
    };
    const auto first_request=request_for_owner(first,reference_one.code);
    const auto second_request=request_for_owner(second,reference_two.code);
    const auto first_probe=cpp_local_int_request_fingerprint(first_request);
    const auto second_probe=cpp_local_int_request_fingerprint(second_request);
    require(!first_probe.empty()&&!second_probe.empty()&&first_probe!=second_probe&&
        compiler_runtime_request_allowed(first,first_request)&&
        compiler_runtime_request_allowed(second,second_request),
        "The native Windows worker cannot prepare the two private U2L5 source variants");
    const auto unverified=learner;
    auto unproved=visible_result(first_request.revision);
    unproved.standard_output="Parte: 4.5 m";
    unproved.local_int_request_fingerprint=first_probe;
    require(!record_compiler_puzzle_case(learner,lesson,first,first_request,unproved,
        "Synthetic stdout without semantic proof")&&learner==unverified,
        "Current U2L5 accepted visible stdout without its private proof");
    unproved.local_int_check_correct=true;
    unproved.local_int_request_fingerprint=second_probe;
    require(!record_compiler_puzzle_case(learner,lesson,first,first_request,unproved,
        "Synthetic proof from the other editor")&&learner==unverified,
        "Current U2L5 accepted another editor's private fingerprint");
    const auto accepted=[&](const CppRunRequest& request,const Block& block,
                            std::string_view fingerprint) {
        auto evidence=visible_result(request.revision);
        evidence.standard_output=block.output_check->cases.front().outputs.front();
        evidence.local_int_check_correct=true;
        evidence.local_int_request_fingerprint=std::string(fingerprint);
        return evidence;
    };
    require(record_compiler_puzzle_case(learner,lesson,first,first_request,
        accepted(first_request,first,first_probe),"Synthetic first-editor WinRT receipt")&&
        compiler_puzzle_completed(learner,lesson,first)&&
        !compiler_puzzle_completed(learner,lesson,second)&&
        compiler_activity_available(lesson,second,snapshot(learner))&&
        visible(learner,true)&&!visible(learner,false),
        "First U2L5 receipt failed to unlock only its own reference and next editor");
    const auto after_first=learner;
    auto initial_second=request_for_owner(second,second.initial_code);
    const auto initial_probe=cpp_local_int_request_fingerprint(initial_second);
    if(!initial_probe.empty()) {
        auto wrong=accepted(initial_second,second,initial_probe);
        wrong.standard_output="Parte: 4.5 m"; // Its actual integer-division starter result.
        require(!record_compiler_puzzle_case(learner,lesson,second,initial_second,wrong,
            "Synthetic uncorrected second starter")&&learner==after_first,
            "The incorrect current grouping passed its fixed first-case objective");
    }
    require(record_compiler_puzzle_case(learner,lesson,second,second_request,
        accepted(second_request,second,second_probe),"Synthetic second-editor WinRT receipt")&&
        compiler_puzzle_completed(learner,lesson,first)&&
        compiler_puzzle_completed(learner,lesson,second)&&
        visible(learner,true)&&visible(learner,false),
        "Second U2L5 receipt did not unlock its own reference independently");
    verify_drafts(learner);
    require(learner.preferences.at("learning_progress").at("other_course_unchanged")==
                state::Value{std::int64_t{42}}&&
        learner.preferences.at("native_windows").at("unrelated_reader_setting")==
                state::Value{std::string("keep")},
        "U2L5 receipts damaged a different course or unrelated reader preferences");
    const auto restored=state::decode(state::encode(learner));
    state::validate_lesson_state(restored,lesson);
    verify_drafts(restored);
    require(compiler_puzzle_completed(restored,lesson,first)&&
        compiler_puzzle_completed(restored,lesson,second)&&
        visible(restored,true)&&visible(restored,false),
        "Reopening authentic encoded U2L5 receipts lost progress or reader gates");

    const auto disk_reopened=reopen_isolated_int_qa_state(restored,L"u2l5");
    state::validate_lesson_state(disk_reopened,lesson);
    verify_drafts(disk_reopened);
    require(disk_reopened==restored&&
        compiler_puzzle_completed(disk_reopened,lesson,first)&&
        compiler_puzzle_completed(disk_reopened,lesson,second)&&
        visible(disk_reopened,true)&&visible(disk_reopened,false)&&
        disk_reopened.preferences.at("learning_progress").at("other_course_unchanged")==
            state::Value{std::int64_t{42}}&&
        disk_reopened.preferences.at("native_windows").at("unrelated_reader_setting")==
            state::Value{std::string("keep")},
        "A real disk reopen merged U2L5 drafts, discarded an editor receipt "
        "or modified another course or reader setting");

    auto reset=restored;
    reset_compiler_puzzle(reset,lesson,second);
    require(compiler_puzzle_completed(reset,lesson,first)&&
        !compiler_puzzle_completed(reset,lesson,second)&&
        visible(reset,true)&&!visible(reset,false),
        "Resetting the second editor erased the first proof or leaked its reference");
    reset_compiler_puzzle(reset,lesson,first);
    require(!compiler_puzzle_completed(reset,lesson,first)&&
        !compiler_puzzle_completed(reset,lesson,second)&&
        !visible(reset,true)&&!visible(reset,false),
        "Resetting both editors left a visible protected reference");
    std::cout<<"PASS published U2L5 WinRT StateStore: two independent real "
        "objectives, preserved three-editor archive, source-pinned fingerprints, "
        "separate saved drafts, synthetic receipts, real QA-disk reopen, reference gates "
        "and isolated resets; no student execution or audio asserted.\n";
}

void run() {
    validate_other_u2_editors();
    validate_current_u2l5_editor_state();
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
        "An encoded state round trip lost the independent local-int proof");

    // An in-memory codec round trip is not a disk reopen.
    const auto disk_reopened=reopen_isolated_int_qa_state(decoded,L"u2l1");
    require(disk_reopened==decoded&&
        compiler_puzzle_completed(disk_reopened,current,block)&&
        saved_local_receipt(disk_reopened)==saved_local_receipt(decoded)&&
        disk_reopened.preferences.at("learning_progress")==
            before.preferences.at("learning_progress"),
        "The isolated on-disk StateStore lost the local-int proof, "
        "historical receipt or unrelated course progress");
    auto corrupted=disk_reopened;
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
    const auto local_int_start=canonical.find("\"compilerLocalIntChecks\"");
    require(local_int_start!=canonical.npos,
        "The canonical JSON lost the local-int declaration");
    // Existing lessons also contain "activity" properties in relationship
    // objects. Malformed local-int tests must target the opt-in contract,
    // never whichever similarly named property appears first in the lesson.
    const auto duplicate_local_int=[&](std::string_view before,
                                       std::string_view after) {
        return canonical.substr(0,local_int_start)+
            overwrite_first(canonical.substr(local_int_start),before,after);
    };
    const auto duplicated_root=std::string("{\"compilerLocalIntChecks\":")+
        winrt::to_string(raw.GetNamedArray(L"compilerLocalIntChecks").Stringify())+
        ","+canonical.substr(1);
    rejects_raw(duplicated_root);
    rejects_raw(duplicate_local_int(
        "\"activity\":\"block_0004_0002\"",
        "\"activity\":\"block_0004_0002\",\"activity\":\"block_0004_0002\""));
    rejects_raw(duplicate_local_int(
        "\"replacement\":13",
        "\"replacement\":13,\"replacement\":17"));
    rejects_raw(duplicate_local_int(
        "\"variable\":\"llaves\"",
        "\"variable\":\"llaves\",\"variable\":\"llaves\""));
    rejects_raw(duplicate_local_int(
        "\"variable\":\"llaves\"",
        "\"variable\":\"llaves\",\"afterUpdate\":true,\"afterUpdate\":true"));
    rejects_raw(duplicate_local_int(
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
        "uncredited old stdout, separate same-source receipt, real isolated StateStore reopen, "
        "tampering rejection and absent-variable rejection; no student execution claimed.\n";
}
}
int main(int argc,char** argv)try {
    winrt::init_apartment();
    if(argc==2&&std::string_view(argv[1])=="--export-six-u2-qa"){
        validate_other_u2_editors(true);return 0;
    }
    require(argc==1,"Unknown local-int QA command");
    run();
    return 0;
}catch(const winrt::hresult_error& e){
    std::cerr<<winrt::to_string(e.message())<<'\n';
    return 1;
}catch(const std::exception& e){
    std::cerr<<"FAIL local-int state: "<<e.what()<<'\n';
    return 1;
}
