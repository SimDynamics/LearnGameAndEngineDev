// Offline, incident-specific state recovery. This executable mode never saves
// a learner profile. The only permissible input maps are the two authenticated
// Git blobs. No product loader or general append-only state guard is relaxed.
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/published_adoption_test_source.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace lgaed::test {
namespace {
using namespace curriculum;
using namespace windows;
using namespace winrt::Windows::Data::Json;
constexpr std::string_view relative=
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json";
constexpr std::string_view rejected_blob="a635ae9c2d3f138702af8a06026cb6b7c5f86217";
constexpr std::string_view restored_blob="33b497bbe50a57090a7ae8ab3d83bc0f208a1357";
constexpr std::string_view rejected_commit="0ed1a59a3e5dee8bb36a9f99b40b567873b9959b";
constexpr std::string_view restored_commit="2c3590c809d13cab02560335f6b2353d5655bbd2";
void require(bool condition,std::string_view why) {
    if(!condition)throw std::runtime_error(std::string(why));
}
const Block& owner(const Lesson& lesson,std::string_view id) {
    const auto at=stable_block_position(lesson,id);
    require(at.has_value(),"Incident owner missing from pinned U3L4");
    return lesson.pages.at(at->page).blocks.at(at->block);
}
struct Incident {
    std::string rejected_bytes,restored_bytes;
    Lesson rejected,restored,compact;
    std::vector<std::string> moved;
};
Incident incident() {
    const auto root=std::filesystem::path(build::source_root);
    Incident result;
    result.rejected_bytes=qa::adoption_detail::git_blob(root,rejected_blob);
    result.restored_bytes=qa::adoption_detail::git_blob(root,restored_blob);
    result.rejected=parse_lesson_json(result.rejected_bytes,"es");
    result.restored=parse_lesson_json(result.restored_bytes,"es");
    result.compact=parse_lesson_progress_metadata(result.restored_bytes,"es");
    auto bad=JsonObject::Parse(winrt::to_hstring(result.rejected.raw_json));
    const auto good=JsonObject::Parse(winrt::to_hstring(result.restored.raw_json));
    bad.GetNamedObject(L"stateLayout").Insert(L"additions",
        good.GetNamedObject(L"stateLayout").GetNamedArray(L"additions"));
    require(bad.Stringify()==good.Stringify(),"Pinned incident differs by more than its storage map");
    const auto& from=result.rejected.state_layout;
    const auto& to=result.restored.state_layout;
    require(result.rejected.id==result.restored.id&&from.lineage==to.lineage&&
        from.signatures==to.signatures&&from.additions.size()==106&&to.additions.size()==106,
        "Unexpected incident identity, original lineage or addition count");
    std::map<std::string,const StateAddition*> originals;
    for(const auto& entry:from.additions)originals.emplace(entry.id,&entry);
    for(const auto& entry:to.additions) {
        const auto found=originals.find(entry.id);
        require(found!=originals.end()&&found->second->sha256==entry.sha256&&
            found->second->progress_page==entry.progress_page,
            "A pinned addition changed ownership or content");
        if(found->second->storage==entry.storage)continue;
        const auto& old=owner(result.rejected,entry.id);
        const auto& now=owner(result.restored,entry.id);
        require(old.raw_json==now.raw_json&&old.type==now.type&&
            !old.address.addition_namespace.empty()&&
            old.address.addition_namespace==now.address.addition_namespace&&
            old.address.progress_page==now.address.progress_page&&
            old.address.page==7&&now.address.page==7,
            "The incident is not an address-only change of one stable owner");
        result.moved.push_back(entry.id);
    }
    require(result.moved.size()==24,"The known incident must move exactly 24 stable owners");
    return result;
}
std::string additions_key(const Lesson& lesson) {
    return "lesson_"+lesson.id+"_state_additions_v2_"+lesson.locale;
}
std::string additions_snapshot(const Lesson& lesson) {
    JsonArray array;
    for(const auto& item:lesson.state_layout.additions) {
        JsonArray row;
        row.Append(JsonValue::CreateStringValue(winrt::to_hstring(item.id)));
        row.Append(JsonValue::CreateStringValue(winrt::to_hstring(item.sha256)));
        row.Append(JsonValue::CreateNumberValue(static_cast<double>(item.storage.page)));
        row.Append(JsonValue::CreateNumberValue(static_cast<double>(item.storage.block)));
        row.Append(JsonValue::CreateStringValue(winrt::to_hstring(item.progress_page)));
        array.Append(row);
    }
    return winrt::to_string(array.Stringify());
}
bool same_snapshot(std::string_view left,std::string_view right) {
    return JsonArray::Parse(winrt::to_hstring(left)).Stringify()==
        JsonArray::Parse(winrt::to_hstring(right)).Stringify();
}
std::string receipt_prefix(const Lesson& lesson,const Block& block) {
    return "compiler-puzzle/v1/"+lesson.locale+"/"+lesson.id+"/"+
        exercise_key(lesson,block)+"/"+state::sha256(reference_objective_frame(lesson,block))+"/";
}
bool response_owner(const Block& block) {
    return block.type==BlockType::multiple_choice||block.type==BlockType::output_prediction;
}
std::string reference_key(const Lesson& lesson,const Block& block) {
    return "reference-proof/v1/"+lesson.locale+"/"+draft_key("programming",lesson,block);
}
std::string reference_seal(const state::StateData& data,const Lesson& lesson,const Block& block) {
    require(block.project_files.empty()&&!block.cmake,"Unexpected project in U3L4 reference recovery");
    const auto draft=state::get_draft(data,draft_key("programming",lesson,block),
        block.initial_code,block.standard_input.value_or(""));
    const auto input=block.standard_input?draft.input:std::string{};
    return state::sha256(reference_objective_frame(lesson,block)+
        "/source/"+std::to_string(draft.source.size())+":"+draft.source+
        "/input/"+std::to_string(input.size())+":"+input);
}
using Key=std::pair<std::string,std::string>;
struct Recovery {
    state::StateData candidate;
    std::size_t copied_responses=0,copied_drafts=0,copied_receipts=0,stale_references=0;
    bool already_restored=false;
};
Recovery stage(const Incident& known,const state::StateData& source,bool report=false) {
    Recovery result;result.candidate=source;
    auto& staged=result.candidate;
    const auto key=additions_key(known.rejected);
    const auto pin=state::string_value(source,"learning_progress",key);
    require(!pin.empty(),"Missing saved U3L4 addition provenance");
    if(same_snapshot(pin,additions_snapshot(known.restored))) {
        state::validate_lesson_state(source,known.restored);
        state::validate_lesson_state(source,known.compact);
        result.already_restored=true;return result;
    }
    require(same_snapshot(pin,additions_snapshot(known.rejected)),
        "Saved U3L4 map is not the exact rejected 106-entry publication; no migration guessed");
    state::validate_lesson_state(source,known.rejected);
    std::set<Key> handled;
    const auto recognize=[&](const std::string& domain,const std::string& name) {
        if(state::find(source,domain,name))handled.emplace(domain,name);
    };
    const auto insert=[&](const std::string& domain,const std::string& name,const state::Value& value) {
        const auto prior=state::find(staged,domain,name);
        if(prior) {
            require(*prior==value,"Distinct values at one stable owner need explicit recovery");
            return false;
        }
        staged.preferences[domain].emplace(name,value);return true;
    };
    const auto group=[&](const std::string& domain,const std::string& prefix) {
        state::Preferences values;
        if(const auto found=source.preferences.find(domain);found!=source.preferences.end())
            for(const auto& [name,value]:found->second)
                if(name.starts_with(prefix)) {
                    handled.emplace(domain,name);
                    values.emplace(name.substr(prefix.size()),value);
                }
        return values;
    };
    for(const auto& id:known.moved) {
        const auto& old=owner(known.rejected,id);
        const auto& now=owner(known.restored,id);
        try {
            if(response_owner(old)) {
                require(!old.choice_revision&&!now.choice_revision,
                    "Unexpected choice representation in the exact U3L4 incident");
                const auto before=state::response_storage_key(known.rejected,old);
                const auto after=state::response_storage_key(known.restored,now);
                recognize("learning_progress",before);recognize("learning_progress",after);
                if(const auto value=state::find(source,"learning_progress",before))
                    if(insert("learning_progress",after,*value))++result.copied_responses;
            }
            if(old.type==BlockType::cpp_compiler) {
                const auto before=draft_key("programming",known.rejected,old);
                const auto after=draft_key("programming",known.restored,now);
                const auto from=group("cpp_lesson_drafts",before+"/");
                const auto to=group("cpp_lesson_drafts",after+"/");
                // Do not mix code from one draft with cursor/input/indentation
                // sidecars from another draft, even if their owner is identical.
                require(from.empty()||to.empty()||from==to,
                    "Distinct drafts for the same stable owner need explicit recovery");
                if(!from.empty()&&to.empty()) {
                    for(const auto& [suffix,value]:from)insert("cpp_lesson_drafts",after+"/"+suffix,value);
                    ++result.copied_drafts;
                }
                const auto old_receipt=receipt_prefix(known.rejected,old);
                const auto new_receipt=receipt_prefix(known.restored,now);
                const auto old_fields=group("native_windows",old_receipt);
                const auto new_fields=group("native_windows",new_receipt);
                stage_u3l4_storage_receipts(staged,known.rejected,old,known.restored,now);
                if(!old_fields.empty()&&new_fields.empty())++result.copied_receipts;

                const auto old_ref=reference_key(known.rejected,old);
                const auto new_ref=reference_key(known.restored,now);
                recognize("native_windows",old_ref);recognize("native_windows",new_ref);
                if(state::find(source,"native_windows",old_ref)) {
                    const auto stored=state::string_value(source,"native_windows",old_ref);
                    if(stored==reference_seal(source,known.rejected,old))
                        insert("native_windows",new_ref,reference_seal(staged,known.restored,now));
                    else ++result.stale_references; // Kept exactly; stale evidence is never promoted.
                }
                if(report)std::cout<<"owner "<<id<<": draft_fields "<<from.size()<<"/"<<to.size()
                    <<", receipt_fields "<<old_fields.size()<<"/"<<new_fields.size()<<"\n";
            }
        }catch(const std::exception& error) {
            // The diagnostics contain only an authored stable ID and our own
            // message, never personal source, answers, values or file paths.
            throw std::runtime_error("U3L4 owner "+id+": "+error.what());
        }
    }
    // An earlier unpublished map may have assigned an additional coordinate
    // to the very same namespace. Do not silently ignore or reinterpret it.
    for(const auto& [domain,entries]:source.preferences)
        for(const auto& [name,value]:entries) {
            (void)value;
            if(handled.contains({domain,name}))continue;
            for(const auto& id:known.moved) {
                const auto& block=owner(known.restored,id);
                if(name.find(block.address.addition_namespace)!=name.npos)
                    throw std::runtime_error("Unclassified historical state for "+id+
                        "; original profile preserved");
            }
        }
    staged.preferences["learning_progress"][key]=additions_snapshot(known.restored);
    state::validate_lesson_state(staged,known.restored);
    state::validate_lesson_state(staged,known.compact);
    // All prior fields except the map pin remain exactly where they were.
    // A canonical copy never deletes an historical response, draft or seal.
    for(const auto& [domain,entries]:source.preferences)
        for(const auto& [name,value]:entries) {
            if(domain=="learning_progress"&&name==key)continue;
            const auto previous=state::find(staged,domain,name);
            require(previous&&*previous==value,"Recovery would erase or overwrite historical state");
        }
    require(state::decode(state::encode(staged))==staged,"Recovered candidate is not a stable state serialization");
    return result;
}
void expect_rejection(const Incident& known,const state::StateData& data) {
    const auto original=state::encode(data);
    bool rejected=false;
    try{(void)stage(known,data);}catch(const std::exception&){rejected=true;}
    require(rejected&&state::encode(data)==original,
        "A conflicting or unauthenticated recovery changed the input profile");
}
}
int u3l4_storage_recovery_windows() {
    const auto known=incident();
    const auto& old=owner(known.rejected,"u3l4_lotes_editor_incluye_seis");
    const auto& now=owner(known.restored,old.stable_id);
    const auto& other=owner(known.restored,"u3l4_lotes_editor_supera_seis");
    // These two editors share coordinate 85 across the two maps, but their
    // different stable namespaces make them different stored owners.
    require(old.address.addition_namespace!=other.address.addition_namespace,
        "The stable addition namespace did not isolate the owners");
    state::StateData data;state::prepare_lesson_state(data,known.rejected);
    data.preferences["qa"]["opaque"]=std::string("unrelated state");
    state::save_current_page(data,known.rejected,2);
    const auto prior=data;
    const auto empty=stage(known,data);
    require(empty.copied_responses==0&&empty.copied_drafts==0&&empty.copied_receipts==0&&
        data==prior&&stage(known,empty.candidate).already_restored,
        "Map-only recovery was not read-only and idempotent");

    const auto& choice=owner(known.rejected,"u3l4_buscar_tres_bloques_practica");
    state::Response response;response.selected=choice.correct_index;
    response.correct=true;
    state::put_response(data,known.rejected,choice,response);
    state::Draft draft;draft.source=old.initial_code+"\n// QA saved draft\n";
    draft.input="QA draft input";draft.selection_start=1;draft.selection_end=3;draft.has_saved=true;
    state::put_draft(data,draft_key("programming",known.rejected,old),draft);

    // Use the native receipt writer in isolated model QA. This is not a
    // claim of running learner code or a way of creating a personal receipt.
    CppRunRequest request;request.source_utf8=old.initial_code;request.revision=31;
    const auto& first=old.output_check->cases.front();request.stdin_utf8=first.standard_input;
    FreshExecution execution;execution.source_revision=request.revision;
    execution.compilation_succeeded=true;execution.execution_completed=true;
    execution.execution_succeeded=first.exit_code==0;execution.exit_code=first.exit_code;
    execution.standard_input=request.stdin_utf8;execution.standard_output=first.outputs.front();
    execution.executable=first.executable;
    require(record_compiler_puzzle_case(data,known.rejected,old,request,execution,"QA original receipt"),
        "Could not seed an isolated native case receipt");
    auto failed=execution;failed.standard_output="QA deliberately incorrect report";
    require(record_compiler_puzzle_review(data,known.rejected,old,request,failed,"QA original rejection"),
        "Could not seed an isolated native rejected-attempt receipt");
    const auto before=data;
    const auto recovered=stage(known,data);
    require(data==before&&recovered.copied_responses==1&&recovered.copied_drafts==1&&
        recovered.copied_receipts==1,"Recovery lost its independently owned state");
    const auto restored_choice=state::get_response(recovered.candidate,known.restored,
        owner(known.restored,choice.stable_id));
    const auto restored_draft=state::get_draft(recovered.candidate,
        draft_key("programming",known.restored,now),now.initial_code);
    require(restored_choice&&restored_choice->correct&&restored_choice->selected==response.selected&&
        restored_draft.source==draft.source&&restored_draft.input==draft.input&&
        compiler_puzzle_completed(recovered.candidate,known.restored,now)&&
        compiler_puzzle_case_count(recovered.candidate,known.restored,now)==
            compiler_puzzle_case_count(data,known.rejected,old)&&
        compiler_puzzle_review(recovered.candidate,known.restored,now,request)=="QA original rejection"&&
        !compiler_puzzle_completed(recovered.candidate,known.restored,other),
        "Recovery changed original credit, lost feedback or promoted another owner");
    auto conflict=data;auto distinct=draft;distinct.source+="different";
    state::put_draft(conflict,draft_key("programming",known.restored,now),distinct);
    expect_rejection(known,conflict);
    auto forged=data;
    auto& cases=std::get<std::vector<std::string>>(
        forged.preferences["native_windows"].at(receipt_prefix(known.rejected,old)+"cases"));
    cases.front()="forged";
    expect_rejection(known,forged);
    auto intermediate=data;
    auto map=JsonArray::Parse(winrt::to_hstring(additions_snapshot(known.rejected)));
    while(map.Size()>98)map.RemoveAtEnd();
    intermediate.preferences["learning_progress"][additions_key(known.rejected)]=
        winrt::to_string(map.Stringify());
    expect_rejection(known,intermediate);
    auto unclassified=data;
    unclassified.preferences["cpp_lesson_drafts"]["programming/"+known.rejected.id+
        "/state-layout-v2/"+old.address.addition_namespace+"/7/999/source"]=std::string("unknown prior draft");
    expect_rejection(known,unclassified);
    auto reset=recovered.candidate;reset_compiler_puzzle(reset,known.restored,now);
    require(stage(known,reset).candidate==reset&&!compiler_puzzle_completed(reset,known.restored,now),
        "Repeated recovery revived an explicitly reset canonical achievement");
    std::cout<<"PASS exact U3L4 incident: 106 entries, 24 owners, address-independent namespaces, "
        "response/draft/case/review retention, conflicts, forged and unknown provenance, no new credit or live profile writes\n";
    return 0;
}
int inspect_u3l4_storage_recovery_windows(const std::filesystem::path& profile) {
    const auto known=incident();
    const auto root=std::filesystem::path(build::source_root);
    const auto live=state::read_bytes(root/std::filesystem::path(relative));
    require(std::string(live.begin(),live.end())==known.restored_bytes,
        "The local U3L4 curriculum is not the exact restored publication");
    require(profile.is_absolute(),"Provide the absolute profile directory; no default profile is inspected");
    state::StateStore store(profile);
    const auto file=store.root()/"active"/"state.bin";
    const auto bytes=state::read_bytes(file);
    const auto data=store.load(); // deliberately never call save()
    try {
        const auto candidate=stage(known,data,true);
        require(state::read_bytes(file)==bytes,"Profile changed during inspection; discard the candidate");
        std::cout<<"READY in memory; original profile untouched. Source publication "<<rejected_commit
            <<", target "<<restored_commit<<"; responses "<<candidate.copied_responses
            <<", draft groups "<<candidate.copied_drafts<<", receipt groups "<<candidate.copied_receipts
            <<", stale references retained "<<candidate.stale_references
            <<", already restored "<<candidate.already_restored<<"\n";
        return 0;
    }catch(const std::exception& error) {
        require(state::read_bytes(file)==bytes,"Profile changed during inspection; discard the candidate");
        std::cerr<<"BLOCKED; original profile untouched: "<<error.what()<<"\n";
        return 3;
    }
}
} // namespace lgaed::test
