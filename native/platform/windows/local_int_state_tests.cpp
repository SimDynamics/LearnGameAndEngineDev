// Opt-in local-scalar authoring and real StateStore receipt integration.
// The U2L8 mode runs hosted MSVC in an isolated QA instance. Neither mode
// modifies curriculum or opens a personal Work/Codex profile.
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
#include <chrono>
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

void validate_u2l8_scalars(const std::filesystem::path& directory) {
    // before.json is copied byte-for-byte from the published 19d83972 lesson.
    // after.json may only append the root-authored private dependency checks.
    const auto original=load_lesson(directory/"before.json");
    require(state::sha256(original.raw_json)==
        "d44ea3aca4844b169373abd9cc5bf31d59fa03ec53b3ea9b4cd686f6ddca4b6c",
        "The U2L8 baseline is not the authentic published 19d83972 lesson");
    const auto reviewed=load_lesson(directory/"after.json");
    const auto old_metadata=parse_lesson_progress_metadata(original.raw_json,"es");
    const auto metadata=parse_lesson_progress_metadata(reviewed.raw_json,"es");
    const auto block_at=[](const Lesson& lesson,std::string_view identity)->const Block& {
        const auto at=stable_block_position(lesson,identity);
        require(at.has_value(),"An authentic U2L8 stable owner is missing");
        return lesson.pages.at(at->page).blocks.at(at->block);
    };
    const auto canonical=[](std::string_view raw) {
        return winrt::to_string(JsonObject::Parse(winrt::to_hstring(raw)).Stringify());
    };
    constexpr std::string_view identity="block_0006_0002";
    const auto& former=block_at(original,identity);
    const auto& current=block_at(reviewed,identity);
    const auto& projection=block_at(metadata,identity);
    const auto root=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    require(root.HasKey(L"compilerLocalIntChecks")&&
            !JsonObject::Parse(winrt::to_hstring(original.raw_json)).HasKey(L"compilerLocalIntChecks"),
        "The U2L8 fixture does not introduce its opt-in root declaration");
    const auto declarations=root.GetNamedArray(L"compilerLocalIntChecks");
    require(declarations.Size()==1&&
            declarations.GetObjectAt(0).GetNamedString(L"activity")==L"block_0006_0002",
        "The U2L8 declaration changed or added a compiler owner");
    const auto without_checks=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
    without_checks.Remove(L"compilerLocalIntChecks");
    require(winrt::to_string(without_checks.Stringify())==canonical(original.raw_json)&&
            reviewed.pages.size()==original.pages.size()&&
            original.compiler_puzzles_required&&reviewed.compiler_puzzles_required&&
            old_metadata.compiler_puzzles_required&&metadata.compiler_puzzles_required,
        "The scalar fixture changed curriculum, history or the original reader contract");
    for(std::size_t p=0;p<original.pages.size();++p) {
        require(reviewed.pages[p].blocks.size()==original.pages[p].blocks.size(),
            "The scalar fixture changed an original U2L8 page");
        for(std::size_t b=0;b<original.pages[p].blocks.size();++b) {
            const auto& before=original.pages[p].blocks[b];
            const auto& after=reviewed.pages[p].blocks[b];
            require(canonical(before.raw_json)==canonical(after.raw_json)&&
                    before.address.page==after.address.page&&
                    before.address.block==after.address.block,
                "The scalar fixture changed an original U2L8 block or stable address");
            require(!before.local_int_check&&
                    (after.local_int_check.has_value()==(&after==&current)),
                "A scalar dependency leaked onto another U2L8 block");
        }
    }
    const std::vector<std::string> visible_outputs{
        "Pasillo: C\nAbierta: 1","Pasillo: C\nAbierta: 1\n"};
    require(!former.local_int_check&&!block_at(old_metadata,identity).local_int_check&&
            current.local_int_check&&current.local_int_check==projection.local_int_check&&
            former.initial_code==current.initial_code&&
            current.initial_code==projection.initial_code&&
            former.output_check&&current.output_check&&projection.output_check&&
            former.output_check->cases.size()==1&&current.output_check->cases.size()==1&&
            projection.output_check->cases.size()==1&&
            former.output_check->cases.front().outputs==visible_outputs&&
            current.output_check->cases.front().outputs==visible_outputs&&
            projection.output_check->cases.front().outputs==visible_outputs,
        "Full and progress loaders disagree on the one unchanged U2L8 visible case");
    const auto& probes=current.local_int_check->probes;
    require(probes.size()==2&&
            probes[0].variable=="pasillo"&&probes[0].replacement=='D'&&
            probes[0].value_type==CppLocalValueType::character&&
            !probes[0].after_update&&!probes[0].expected_increase&&
            probes[0].expected_stdout==std::vector<std::string>{
                "Pasillo: D\nAbierta: 1","Pasillo: D\nAbierta: 1\n"}&&
            probes[1].variable=="abierta"&&probes[1].replacement==0&&
            probes[1].value_type==CppLocalValueType::boolean&&
            !probes[1].after_update&&!probes[1].expected_increase&&
            probes[1].expected_stdout==std::vector<std::string>{
                "Pasillo: C\nAbierta: 0","Pasillo: C\nAbierta: 0\n"},
        "Native U2L8 typed specs differ from their two independent authored goals");

    const auto equals=block_at(original,"block_0007_0001").code;
    const auto braces=block_at(original,"block_0008_0003").code;
    auto parentheses=overwrite_first(equals,"char pasillo = 'C';","char pasillo('C');");
    parentheses=overwrite_first(parentheses,"bool abierta = true;","bool abierta(true);");
    auto assigned=overwrite_first(equals,"char pasillo = 'C';","char pasillo;\n    pasillo = 'C';");
    assigned=overwrite_first(assigned,"bool abierta = true;","bool abierta;\n    abierta = true;");
    const auto newline=overwrite_first(equals,
        "std::cout << \"Abierta: \" << abierta;",
        "std::cout << \"Abierta: \" << abierta << '\\n';");

    const auto options=LaunchOptions{ProfileId::Codex,
        L"local-scalars-u2l8-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    require(environment.options.qa_instance.has_value()&&
            !environment.options.qa_instance->empty(),
        "U2L8 runtime or persistence would target a personal profile");
    initialize_profile_directories(environment.paths);
    const auto projects=environment.paths.projects/L"independent U2L8 scalar tests";
    std::uint64_t next_request=100;
    const auto make_request=[&](std::string source,bool checked=true) {
        CppRunRequest request;
        request.request_id=++next_request;request.revision=next_request+100;
        request.project_key="isolated-u2l8-scalars";
        request.source_utf8=std::move(source);
        request.stdin_utf8=current.standard_input.value_or("");
        request.projects_directory=projects;
        request.compile_timeout=std::chrono::seconds(60);
        request.execution_timeout=std::chrono::seconds(8);
        if(checked)request.local_int_check=current.local_int_check;
        return request;
    };
    const auto execute=[&](const CppRunRequest& request,std::string_view expected) {
        const auto result=run_cpp(request);
        require(result.error_utf8.empty()&&result.compilation.succeeded()&&
                result.execution.succeeded()&&result.execution.stderr_bytes.empty()&&
                cpp_local_int_canonical_stdout(result.execution.stdout_bytes)==expected,
            "Real MSVC failed the public U2L8 observation: "+
            result.error_utf8+result.compilation.stderr_bytes+result.execution.stderr_bytes);
        return result;
    };
    const auto private_proof=[&](const CppRunRequest& request,const CppRunResult& result) {
        const auto proof=capture_cpp_execution(request,result);
        require(compiler_runtime_request_allowed(current,request)&&
                !cpp_local_int_request_fingerprint(request).empty()&&
                proof.local_int_check_correct&&
                proof.local_int_request_fingerprint==cpp_local_int_request_fingerprint(request)&&
                result.local_int_evidence.size()==2,
            "A valid U2L8 source did not earn two authentic private observations");
        for(std::size_t i=0;i<probes.size();++i) {
            const auto variant=cpp_local_int_override(request.source_utf8,probes[i]);
            const auto& evidence=result.local_int_evidence[i];
            require(variant&&evidence.instrumented_source_sha256==state::sha256(*variant)&&
                    evidence.compilation.succeeded()&&evidence.execution.succeeded()&&
                    evidence.execution.stderr_bytes.empty()&&
                    cpp_local_int_expected_output(probes[i],evidence.execution.stdout_bytes),
                "U2L8 private evidence is not bound to its exact typed source variant");
        }
        return proof;
    };
    const auto original_request=make_request(equals,false);
    const auto checked_request=make_request(equals);
    require(compiler_runtime_request_allowed(former,original_request)&&
            compiler_runtime_request_allowed(current,checked_request)&&
            !compiler_runtime_request_allowed(current,original_request),
        "The historical and checked U2L8 requests lost their distinct contracts");

    state::StateData saved;
    saved.preferences["learning_progress"]["other_course_unchanged"]=std::int64_t{42};
    saved.preferences["native_windows"]["unrelated_reader_setting"]=std::string("keep");
    state::prepare_lesson_state(saved,original);
    const auto key=draft_key("programming",original,former);
    require(key==draft_key("programming",reviewed,current),
        "The opt-in scalar check moved the original U2L8 draft");
    state::Draft draft;
    draft.source=parentheses+"// QA unfinished edit; keep this exact draft.\n";
    draft.input="private U2L8 draft input";
    draft.selection_start=9;draft.selection_end=27;
    state::put_draft(saved,key,draft);
    const auto verify_draft=[&](const state::StateData& data) {
        const auto actual=state::get_draft(data,key,current.initial_code);
        require(actual.has_saved&&actual.source==draft.source&&actual.input==draft.input&&
                actual.selection_start==draft.selection_start&&
                actual.selection_end==draft.selection_end&&
                data.preferences.at("learning_progress").at("other_course_unchanged")==
                    state::Value{std::int64_t{42}}&&
                data.preferences.at("native_windows").at("unrelated_reader_setting")==
                    state::Value{std::string("keep")},
            "U2L8 execution or reopening changed a draft, selection or unrelated preference");
    };
    const auto old_result=execute(original_request,visible_outputs.front());
    const auto old_proof=capture_cpp_execution(original_request,old_result);
    require(record_compiler_puzzle_case(saved,original,former,original_request,old_proof,
            "Real historical U2L8 visible output")&&
            compiler_puzzle_completed(saved,original,former)&&
            compiler_puzzle_case_count(saved,original,former)==1&&saved_local_receipt(saved).empty(),
        "The real historical U2L8 stdout receipt was not retained");
    const auto legacy=saved;
    {
        state::StateStore writer(environment.paths.root);
        require(writer.empty(),"The supposedly fresh U2L8 QA profile contains progress");
        (void)writer.load();writer.save(saved);
    }
    {
        state::StateStore historical_reader(environment.paths.root);
        require(!historical_reader.empty(),"The U2L8 historical QA save is missing");
        saved=historical_reader.load();
    }
    state::validate_lesson_state(saved,reviewed);
    verify_draft(saved);
    auto replay_request=original_request;
    replay_request.local_int_check=current.local_int_check;
    const auto replay_proof=capture_cpp_execution(replay_request,old_result);
    require(compiler_runtime_request_allowed(current,replay_request)&&
            replay_proof.source_revision==replay_request.revision&&
            replay_proof.local_int_request_fingerprint==cpp_local_int_request_fingerprint(replay_request)&&
            !replay_proof.local_int_check_correct,
        "Replaying real stdout must lack private evidence without relying on a stale revision");
    require(saved==legacy&&!compiler_puzzle_completed(saved,reviewed,current)&&
            !compiler_puzzle_completed(saved,metadata,projection)&&
            compiler_puzzle_case_count(saved,reviewed,current)==1&&saved_local_receipt(saved).empty()&&
            !record_compiler_puzzle_case(saved,reviewed,current,original_request,old_proof,
                "Historical request cannot acquire a new typed proof")&&
            !record_compiler_puzzle_case(saved,reviewed,current,replay_request,replay_proof,
                "Matching visible output cannot acquire private evidence")&&saved==legacy,
        "Reopening or replaying old U2L8 stdout promoted it to a char/bool dependency");

    const auto checked_result=execute(checked_request,visible_outputs.front());
    const auto checked_proof=private_proof(checked_request,checked_result);
    auto tampered=checked_result;
    tampered.local_int_evidence[0].instrumented_source_sha256=state::sha256(equals);
    const auto unbound=capture_cpp_execution(checked_request,tampered);
    require(!unbound.local_int_check_correct&&
            !record_compiler_puzzle_case(saved,reviewed,current,checked_request,unbound,
                "Private char evidence from the unchanged source")&&saved==legacy,
        "A private observation from the wrong U2L8 source earned credit");
    require(record_compiler_puzzle_case(saved,reviewed,current,checked_request,checked_proof,
            "Real independent U2L8 char/bool observations")&&
            compiler_puzzle_completed(saved,reviewed,current)&&
            compiler_puzzle_case_count(saved,reviewed,current)==1&&!saved_local_receipt(saved).empty(),
        "One visible U2L8 case plus its two private observations did not complete the owner");
    for(const auto& [name,value]:legacy.preferences.at("native_windows"))
        require(saved.preferences.at("native_windows").at(name)==value,
            "Adding U2L8 private evidence rewrote an existing historical receipt");

    const auto equivalent=[&](std::string_view description,const std::string& source,
                              std::string_view expected) {
        const auto request=make_request(source);
        require(compiler_runtime_request_allowed(current,request),
            "An equivalent U2L8 spelling failed the typed source gate");
        const auto result=execute(request,expected);
        const auto proof=private_proof(request,result);
        state::StateData fresh;
        state::prepare_lesson_state(fresh,reviewed);
        require(!compiler_puzzle_completed(fresh,reviewed,current)&&
                compiler_puzzle_case_count(fresh,reviewed,current)==0&&
                record_compiler_puzzle_case(fresh,reviewed,current,request,proof,description)&&
                compiler_puzzle_completed(fresh,reviewed,current)&&
                compiler_puzzle_case_count(fresh,reviewed,current)==1&&!saved_local_receipt(fresh).empty(),
            "A single real visible case did not finish an equivalent U2L8 source");
        std::cout<<"PASS real U2L8 scalar source: "<<description<<'\n';
    };
    equivalent("published brace initialization",braces,visible_outputs.front());
    equivalent("parenthesized initialization",parentheses,visible_outputs.front());
    equivalent("declaration followed by assignment",assigned,visible_outputs.front());
    equivalent("optional final newline",newline,visible_outputs.back());

    const auto fixed_char=overwrite_first(equals,
        "std::cout << \"Pasillo: \" << pasillo << '\\n';",
        "std::cout << \"Pasillo: C\\n\";");
    const auto fixed_bool=overwrite_first(equals,
        "std::cout << \"Abierta: \" << abierta;",
        "std::cout << \"Abierta: 1\";");
    const auto fixed_both=overwrite_first(fixed_char,
        "std::cout << \"Abierta: \" << abierta;",
        "std::cout << \"Abierta: 1\";");
    const auto reject_fixed=[&](std::string_view description,const std::string& source,
                               std::array<bool,2> expected_private) {
        const auto request=make_request(source);
        require(compiler_runtime_request_allowed(current,request),
            "The fixed-output counterexample did not reach the runtime checker");
        const auto result=execute(request,visible_outputs.front());
        require(result.local_int_evidence.size()==2,
            "A fixed-output counterexample lacks its two real private observations");
        for(std::size_t i=0;i<probes.size();++i) {
            const auto& evidence=result.local_int_evidence[i];
            require(evidence.compilation.succeeded()&&evidence.execution.succeeded()&&
                    evidence.execution.stderr_bytes.empty()&&
                    cpp_local_int_expected_output(probes[i],evidence.execution.stdout_bytes)==expected_private[i],
                "The independent char/bool probe did not isolate the fixed output line");
        }
        const auto rejected=capture_cpp_execution(request,result);
        const auto unchanged=saved;
        require(!rejected.local_int_check_correct&&
                !record_compiler_puzzle_case(saved,reviewed,current,request,rejected,description)&&
                saved==unchanged,
            "Correct public U2L8 output hid a missing char or bool dependency");
    };
    reject_fixed("Both output lines fixed",fixed_both,{false,false});
    reject_fixed("Only the char output line fixed",fixed_char,{false,true});
    reject_fixed("Only the bool output line fixed",fixed_bool,{true,false});

    auto wrong_char=overwrite_first(equals,"char pasillo = 'C';","int pasillo = 67;");
    wrong_char=overwrite_first(wrong_char,"<< pasillo <<","<< static_cast<char>(pasillo) <<");
    const auto wrong_bool=overwrite_first(equals,"bool abierta = true;","int abierta = 1;");
    for(const auto& source:{wrong_char,wrong_bool}) {
        // Compile the genuine visible-output counterexample without private
        // authoring, then prove it cannot be admitted or credited as typed.
        const auto plain=make_request(source,false);
        const auto result=execute(plain,visible_outputs.front());
        auto typed=plain;typed.local_int_check=current.local_int_check;
        const auto rejected=capture_cpp_execution(typed,result);
        const auto unchanged=saved;
        require(!compiler_runtime_request_allowed(current,typed)&&
                cpp_local_int_request_fingerprint(typed).empty()&&!rejected.local_int_check_correct&&
                !record_compiler_puzzle_case(saved,reviewed,current,typed,rejected,
                    "An int declaration cannot stand in for char or bool")&&saved==unchanged,
            "A correct visible result concealed an int in place of the authored scalar type");
    }

    verify_draft(saved);
    {
        state::StateStore writer(environment.paths.root);
        require(!writer.empty()&&writer.load()==legacy,
            "An uncommitted U2L8 test changed the durable historical state");
        writer.save(saved);
    }
    state::StateStore reopened(environment.paths.root);
    require(!reopened.empty(),"The U2L8 private QA receipt was not saved");
    const auto restored=reopened.load();
    state::validate_lesson_state(restored,reviewed);
    validate_compiler_puzzle_state(restored,reviewed,current);
    validate_compiler_puzzle_state(restored,metadata,projection);
    verify_draft(restored);
    require(restored==saved&&compiler_puzzle_completed(restored,original,former)&&
            compiler_puzzle_completed(restored,reviewed,current)&&
            compiler_puzzle_completed(restored,metadata,projection)&&
            compiler_puzzle_case_count(restored,reviewed,current)==1&&
            saved_local_receipt(restored)==saved_local_receipt(saved),
        "StateStore reopening lost the U2L8 receipt or changed its single visible obligation");

    // Both native loaders must preserve JSON types, rather than interpreting
    // ASCII numbers or a string spelling of false as a typed declaration.
    for(const unsigned scenario:{0u,1u,2u,3u,4u,5u,6u}) {
        const auto invalid=JsonObject::Parse(winrt::to_hstring(reviewed.raw_json));
        const auto authored=invalid.GetNamedArray(L"compilerLocalIntChecks").GetObjectAt(0)
            .GetNamedArray(L"probes");
        if(scenario==0)authored.GetObjectAt(0).Insert(L"replacement",JsonValue::CreateNumberValue(68));
        if(scenario==1)authored.GetObjectAt(1).Insert(L"replacement",JsonValue::CreateNumberValue(0));
        if(scenario==2)authored.GetObjectAt(1).Insert(L"replacement",JsonValue::CreateStringValue(L"false"));
        if(scenario==3)authored.GetObjectAt(0).Remove(L"valueType");
        if(scenario==4)authored.GetObjectAt(1).Insert(L"afterUpdate",JsonValue::CreateBooleanValue(true));
        if(scenario==5)authored.GetObjectAt(0).Insert(L"valueType",JsonValue::CreateStringValue(L"integer"));
        if(scenario==6) {
            const auto mixed=authored.GetObjectAt(1);
            mixed.Insert(L"variable",JsonValue::CreateStringValue(L"pasillo"));
            mixed.Insert(L"valueType",JsonValue::CreateStringValue(L"int"));
            mixed.Insert(L"replacement",JsonValue::CreateNumberValue(68));
        }
        rejects(invalid);
    }
    // WinRT merges duplicate members; the original source parser must still
    // reject a hidden first valueType even when the last value looks valid.
    rejects_raw(overwrite_first(canonical(reviewed.raw_json),
        R"("valueType":"char")",R"("valueType":"int","valueType":"char")"));
    std::cout<<"PASS real Windows U2L8 char/bool: authenticated original blocks, "
        "full/progress typed specs, five valid source forms, three fixed-output and "
        "two wrong-type counterexamples, one visible case, immutable historical stdout, "
        "private evidence and saved draft/selection/preferences after StateStore reopen.\n";
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
    if(argc==3&&std::string_view(argv[1])=="--u2l8-scalars"){
        validate_u2l8_scalars(std::filesystem::path(argv[2]));return 0;
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
