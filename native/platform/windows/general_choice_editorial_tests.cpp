#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "core/narration_state.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

void require(bool valid,std::string_view message) {
    if(!valid)throw std::runtime_error(std::string(message));
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    require(stream.good(),"The generated general-choice fixture was not found");
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void rejected_change(std::string_view original,
        const std::function<void(JsonObject)>& change,std::string_view reason) {
    auto root=JsonObject::Parse(winrt::to_hstring(original));
    change(root);
    const auto bytes=winrt::to_string(root.Stringify());
    bool rejected_full=false,rejected_progress=false;
    try { (void)parse_lesson_json(bytes,"es"); }
    catch(const std::exception&) {rejected_full=true;}
    try { (void)parse_lesson_progress_metadata(bytes,"es"); }
    catch(const std::exception&) {rejected_progress=true;}
    require(rejected_full&&rejected_progress,reason);
}

std::vector<JsonObject> historical_choice_blocks(const JsonObject& root,
        std::string_view identity) {
    std::vector<JsonObject> result;
    for(const auto& item:root.GetNamedObject(L"stateLayout")
            .GetNamedArray(L"retirementHistory")) {
        const auto snapshot=item.GetObject();
        const auto ownership=snapshot.GetNamedArray(L"current");
        const auto pages=snapshot.GetNamedArray(L"pages");
        for(unsigned p=0;p<ownership.Size();++p) {
            const auto names=ownership.GetObjectAt(p).GetNamedArray(L"blocks");
            const auto blocks=pages.GetObjectAt(p).GetNamedArray(L"blocks");
            for(unsigned b=0;b<names.Size();++b)
                if(winrt::to_string(names.GetStringAt(b))==identity)
                    result.push_back(blocks.GetObjectAt(b));
        }
    }
    return result;
}

void verify_retirement_choice_pair(const std::filesystem::path& directory,
        std::string_view fixture) {
    const auto before_bytes=read_file(directory/(std::string(fixture)+"-before.json"));
    const auto after_bytes=read_file(directory/(std::string(fixture)+"-after.json"));
    const auto original=parse_lesson_json(before_bytes,"es");
    const auto revised=parse_lesson_json(after_bytes,"es");
    const auto progress=parse_lesson_progress_metadata(after_bytes,"es");
    const auto before_json=JsonObject::Parse(winrt::to_hstring(before_bytes));
    const auto after_json=JsonObject::Parse(winrt::to_hstring(after_bytes));
    const auto before_layout=before_json.GetNamedObject(L"stateLayout");
    const auto after_layout=after_json.GetNamedObject(L"stateLayout");
    std::set<std::string> previous_revisions;
    if(before_layout.HasKey(L"choiceRevisions"))
        for(const auto& value:before_layout.GetNamedArray(L"choiceRevisions"))
            previous_revisions.insert(winrt::to_string(value.GetObject().GetNamedString(L"id")));
    std::optional<JsonObject> added;
    for(const auto& value:after_layout.GetNamedArray(L"choiceRevisions")) {
        const auto record=value.GetObject();
        if(previous_revisions.contains(winrt::to_string(record.GetNamedString(L"id"))))continue;
        require(!added,"The retirement fixture must introduce one reviewed choice");
        added=record;
    }
    require(added.has_value(),"The retirement fixture has no new reviewed choice");
    const auto identity=winrt::to_string(added->GetNamedString(L"id"));
    const auto option=static_cast<std::size_t>(added->GetNamedNumber(L"optionIndex"));
    require(before_layout.GetNamedArray(L"retirementHistory").Stringify()==
            after_layout.GetNamedArray(L"retirementHistory").Stringify(),
        "Authoring rewrote an already published retirement snapshot");

    const auto old_position=stable_block_position(original,identity);
    const auto position=stable_block_position(revised,identity);
    require(old_position&&position&&*old_position==*position,
        "Historical prose support moved the reviewed activity");
    const auto& before=original.pages[position->page].blocks[position->block];
    const auto& after=revised.pages[position->page].blocks[position->block];
    const auto& metadata=progress.pages[position->page].blocks[position->block];
    require(after.choice_revision&&metadata.choice_revision&&
            after.choice_revision->general_option_index==option&&
            metadata.choice_revision->general_option_index==option&&
            after.options==metadata.options&&after.correct_index==before.correct_index&&
            after.question==before.question&&after.explanation==before.explanation&&
            after.review_feedback_options==before.review_feedback_options&&
            after.options.size()==before.options.size()&&
            after.options.at(option)!=before.options.at(option),
        "The full/progress loaders lost the current authenticated choice contract");
    for(std::size_t index=0;index<before.options.size();++index)
        if(index!=option)require(after.options[index]==before.options[index],
            "The reviewed choice changed an unrelated answer");

    require(!original.state_layout.retirement_history.empty()&&
            original.state_layout.retirement_history.size()==revised.state_layout.retirement_history.size(),
        "The real earlier reading snapshots are missing");
    bool older_prose=false;
    for(std::size_t h=0;h<original.state_layout.retirement_history.size();++h) {
        const auto& old=original.state_layout.retirement_history[h];
        const auto& current=revised.state_layout.retirement_history[h];
        require(old.sha256==current.sha256&&old.text_semantics==current.text_semantics,
            "Loading a reviewed option rewrote its archived reading representation");
        for(std::size_t p=0;p<old.current.size();++p)
            for(std::size_t b=0;b<old.current[p].blocks.size();++b) {
                if(old.current[p].blocks[b]!=identity)continue;
                const auto& old_block=old.pages[p].blocks[b];
                const auto& new_block=current.pages[p].blocks[b];
                older_prose=older_prose||old_block.question!=before.question||
                    old_block.explanation!=before.explanation;
                require(old_block.question==new_block.question&&
                        old_block.explanation==new_block.explanation&&
                        old_block.review_feedback_options==new_block.review_feedback_options&&
                        old_block.text_semantics==new_block.text_semantics&&
                        old_block.options==new_block.options&&
                        old_block.correct_index==new_block.correct_index,
                    "A snapshot borrowed later question, explanation or option text");
            }
    }
    require(older_prose,
        "The regression no longer includes prose older than the choice archive");

    state::StateData saved;
    state::prepare_lesson_state(saved,original);
    state::Response response;
    require(before.correct_index.has_value(),"The original choice has no correct answer");
    response.selected=static_cast<std::int64_t>(*before.correct_index);response.correct=true;
    response.raw_json=R"({"incorrectAttempts":3,"opaque":{"caret":11}})";
    const auto address=exercise_key(original,before);
    state::put_response(saved,address,response);
    const auto old_receipt=state::string_value(saved,"learning_progress",address);
    auto reopened=saved;
    state::prepare_lesson_state(reopened,revised);
    const auto restored=state::get_response(reopened,revised,after);
    require(state::response_storage_key(revised,after)==address&&restored&&
            restored->correct&&restored->selected==static_cast<std::int64_t>(*before.correct_index)&&
            !restored->historical_choice&&
            state::string_value(reopened,"learning_progress",address)==old_receipt,
        "Historical prose support lost an existing answer, retry count or storage owner");

    rejected_change(after_bytes,[&](JsonObject root) {
        auto blocks=historical_choice_blocks(root,identity);
        require(!blocks.empty(),"There is no archived choice to corrupt");
        blocks.front().Insert(L"question",JsonValue::CreateStringValue(L"Invented historical wording"));
    },"Unpublished prose was accepted as an earlier reading snapshot");
    rejected_change(after_bytes,[&](JsonObject root) {
        auto blocks=historical_choice_blocks(root,identity);
        require(!blocks.empty(),"There is no archived answer to corrupt");
        blocks.front().Insert(L"correctIndex",JsonValue::CreateNumberValue(
            static_cast<double>((*before.correct_index+1)%before.options.size())));
    },"A changed historical correct answer was accepted");
    rejected_change(after_bytes,[&](JsonObject root) {
        root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page))
            .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block))
            .Insert(L"question",JsonValue::CreateStringValue(L"Unreviewed active question"));
    },"Historical snapshot permission leaked into the active choice projection");
    std::cout<<"PASS retirement choice "<<fixture
        <<": original archived prose, full/progress loaders, unchanged answers and response state\n";
}

constexpr std::string_view prose_owner="block_0006_0002";
const Block& prose_block(const Lesson& lesson) {
    const auto at=stable_block_position(lesson,prose_owner);
    require(at&&at->page==6&&at->block==2,
        "The U2L10 prose fixture changed the original choice address");
    return lesson.pages.at(at->page).blocks.at(at->block);
}

narration::SemanticPlanProof prose_reading_proof(const Block& block) {
    // This is stored reading provenance, not a simulated audio execution.
    // Its exact old sources must remain old after the lesson is reopened.
    narration::SemanticPlanProof result;
    for(const auto path:{"/question","/explanation"}) {
        const auto found=block.text_semantics.find(path);
        require(found!=block.text_semantics.end(),"The choice prose lost its authored reading field");
        result.regions.push_back({std::string(prose_owner)+path,
            found->second.source,"auto","text",found->second});
    }
    return result;
}

void verify_prose_saved_state(const std::vector<Lesson>& lessons,
        const std::vector<Lesson>& metadata,std::int64_t selected) {
    const auto& original=lessons.front();
    const auto& before=prose_block(original);
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"choice-prose-"+std::to_wstring(selected)+L"-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
            !std::filesystem::exists(environment.paths.root),
        "Choice prose QA would open an existing or personal profile");
    initialize_profile_directories(environment.paths);
    state::StateData saved;
    {
        state::StateStore initial(environment.paths.root);
        saved=initial.load();
        require(saved.preferences.empty(),"The isolated choice profile is not empty");
    }
    state::prepare_lesson_state(saved,original);
    state::Response response;
    response.selected=selected;response.correct=selected==0;
    response.raw_json=R"({"incorrectAttempts":5,"opaque":{"caret":11,"draft":"keep"}})";
    if(!response.correct)state::mark_response_reviewed(response,before);
    state::put_response(saved,original,before,response);
    const auto receipt_key=state::response_storage_key(original,before);
    const auto receipt=state::string_value(saved,"learning_progress",receipt_key);
    saved.preferences["native_windows"]["qa_unrelated_reader_preference"]=std::string("keep");
    saved.preferences["learning_progress"]["qa_other_course_progress"]=std::int64_t{37};

    const auto compiler_at=stable_block_position(original,"block_0004_0002");
    require(compiler_at.has_value(),"The unchanged U2L10 compiler draft owner is missing");
    const auto& compiler=original.pages.at(compiler_at->page).blocks.at(compiler_at->block);
    const auto compiler_key=draft_key("programming",original,compiler);
    state::Draft draft;
    draft.source=compiler.initial_code+"\n// Borrador QA conservado\n";
    draft.input="entrada guardada";draft.selection_start=2;draft.selection_end=7;draft.has_saved=true;
    state::put_draft(saved,compiler_key,draft);

    narration::Position position;
    position.segment=0;position.milliseconds=250;position.heard_through=0;
    auto history=narration::begin({},"choice-prose-authentic-before","qa-choice-reader",position,
        original.id+"/es/page_0006",original.id+"/es/page_0006");
    history.entries.front().semantic_plan=prose_reading_proof(before);
    narration::write_history(saved,history);
    const auto original_history=narration::read_history(saved);
    const auto encoded=state::encode(saved);
    {
        state::StateStore writer(environment.paths.root);
        (void)writer.load();writer.save(saved);
    }

    for(std::size_t stage=1;stage<lessons.size();++stage) {
        state::StateStore reopened(environment.paths.root);
        auto current=reopened.load();
        require(state::encode(current)==encoded,
            "Physical reopening changed the published choice receipt or unrelated state");
        for(const auto* lesson:{&lessons[stage],&metadata[stage]}) {
            const auto& after=prose_block(*lesson);
            state::validate_lesson_state(current,*lesson);
            state::prepare_lesson_state(current,*lesson);
            const auto retained=state::get_response(current,*lesson,after);
            require(retained&&retained->selected==selected&&retained->correct==(selected==0)&&
                    !retained->historical_choice&&
                    state::response_storage_key(*lesson,after)==receipt_key&&
                    state::string_value(current,"learning_progress",receipt_key)==receipt&&
                    state::selected_choice_text(*retained,after)==before.options.at(static_cast<std::size_t>(selected)),
                "A prose revision changed a saved selection, attempts, correctness or response address");
            if(retained->correct)
                require(state::choice_correct_feedback(*retained,after)==after.explanation,
                    "A retained correct answer displays an explanation from the wrong publication");
            else require(state::response_reviewed(*retained,after)&&
                    state::choice_incorrect_feedback(*retained,after)==incorrect_feedback(before,static_cast<std::size_t>(selected)),
                "A prose revision discarded the old incorrect attempt or reviewed guide");
            const auto retained_draft=state::get_draft(current,compiler_key,compiler.initial_code);
            require(retained_draft.source==draft.source&&retained_draft.input==draft.input&&
                    retained_draft.selection_start==draft.selection_start&&
                    retained_draft.selection_end==draft.selection_end,
                "A choice prose revision rewrote an unrelated compiler draft or selection");
            require(narration::read_history(current)==original_history&&
                    original_history.entries.front().semantic_plan!=prose_reading_proof(after),
                "Current prose inherited an old reading plan or rewrote its saved bookmark");
            require(state::encode(current)==encoded,
                "Preparing a later prose edition changed historical or unrelated preference bytes");
        }
        reopened.save(current);
    }
    // Import uses the exact same historical bytes, not a rebuilt response.
    auto imported=state::decode(encoded);
    state::prepare_lesson_state(imported,lessons.back());
    state::validate_lesson_state(imported,metadata.back());
    require(state::encode(imported)==encoded,
        "Importing the earlier publication into the final prose changed its historical data");
    std::cout<<"PASS U2L10 choice prose state selection "<<selected
        <<": real StateStore reopen, exact attempts/draft/preferences, original reading plan and import\n";
}

void verify_prose_history(const std::filesystem::path& directory) {
    winrt::init_apartment();
    constexpr std::string_view published_sha256=
        "e0d2ec13f6a72e0f1548017e9f4a175a04a0cbad50021a42eafc0f18eef18aa0";
    const auto manifest=JsonObject::Parse(winrt::to_hstring(read_file(directory/"manifest.json")));
    require(manifest.GetNamedString(L"identity")==winrt::to_hstring(prose_owner),
        "The prose fixture belongs to another activity");
    for(const auto& entry:manifest.GetNamedObject(L"sha256"))
        require(state::sha256(read_file(directory/winrt::to_string(entry.Key())))==
                winrt::to_string(entry.Value().GetString()),
            "A generated prose fixture differs from its exported bytes");
    const auto published_bytes=read_file(directory/"published.json");
    require(state::sha256(published_bytes)==published_sha256,
        "U2L10 prose QA does not start from the exact authentic main publication");
    std::vector<Lesson> lessons,metadata;
    std::vector<JsonObject> documents;
    for(const auto name:{"published.json","question.json","explanation.json"}) {
        const auto bytes=read_file(directory/name);
        lessons.push_back(parse_lesson_json(bytes,"es"));
        metadata.push_back(parse_lesson_progress_metadata(bytes,"es"));
        documents.push_back(JsonObject::Parse(winrt::to_hstring(bytes)));
    }
    const auto& before=prose_block(lessons.front());
    const auto original_layout=documents.front().GetNamedObject(L"stateLayout");
    require(before.choice_revision&&before.choice_revision->general_option_index==0&&
            before.correct_index==0&&before.options.size()==3,
        "The authentic publication lost its existing generic option correction");
    for(std::size_t stage=0;stage<lessons.size();++stage) {
        const auto& current=prose_block(lessons[stage]);
        const auto& meta=prose_block(metadata[stage]);
        const auto layout=documents[stage].GetNamedObject(L"stateLayout");
        require(current.choice_revision&&meta.choice_revision&&
                current.options==before.options&&meta.options==before.options&&
                current.correct_index==before.correct_index&&meta.correct_index==before.correct_index&&
                current.question==meta.question&&current.explanation==meta.explanation&&
                current.review_feedback==before.review_feedback&&
                current.review_feedback_options==before.review_feedback_options&&
                current.text_semantics==meta.text_semantics&&
                current.choice_revision->raw_json==before.choice_revision->raw_json&&
                current.choice_revision->representation_signature==before.choice_revision->representation_signature&&
                meta.choice_revision->representation_signature==before.choice_revision->representation_signature,
            "Full/progress prose loading changed the original answer, archive, guide or representation");
        for(const auto field:{L"origin",L"current",L"family",L"additions",L"retired",
                L"retirementHistory",L"sourceFormatRevisions",L"choiceRevisions"}) {
            require(layout.HasKey(field)==original_layout.HasKey(field),
                "A prose revision added or removed original state metadata");
            if(layout.HasKey(field))require(layout.GetNamedValue(field).Stringify()==
                    original_layout.GetNamedValue(field).Stringify(),
                "A prose revision rewrote original identities, archives or reading snapshots");
        }
        require(current.choice_revision->original_question==before.choice_revision->original_question&&
                current.choice_revision->original_explanation==before.choice_revision->original_explanation,
            "A current passage was projected backward into the choice archive");
        for(const auto path:{"/question","/explanation"}) {
            const auto& semantics=current.text_semantics.at(path);
            const auto& source=std::string_view(path)=="/question"?current.question:current.explanation;
            require(semantics.source==source,"A current reading descriptor is bound to another passage");
            if(stage>0&&std::string_view(path)=="/question")
                require(source!=before.question&&!semantics.spans.empty(),
                    "The revised question did not acquire its own authored reading source");
            if(stage==2&&std::string_view(path)=="/explanation")
                require(source!=before.explanation&&!semantics.spans.empty(),
                    "The revised explanation did not acquire its own authored reading source");
        }
    }
    require(prose_block(lessons[1]).explanation==before.explanation&&
            prose_block(lessons[2]).question==prose_block(lessons[1]).question,
        "The two independent prose publications were collapsed or reordered");
    std::cout<<"PASS U2L10 choice prose: authentic option, later question and explanation, full/progress loaders and original reading archives\n";
    for(const auto selected:{std::int64_t{0},std::int64_t{1},std::int64_t{2}})
        verify_prose_saved_state(lessons,metadata,selected);
    const auto rejected=manifest.GetNamedArray(L"rejected");
    require(rejected.Size()==13,"The prose fixture lost its protected-contract counterexamples");
    for(const auto& entry:rejected) {
        const auto row=entry.GetObject();
        const auto bytes=read_file(directory/winrt::to_string(row.GetNamedString(L"file")));
        const auto reason=winrt::to_string(row.GetNamedString(L"reason"));
        const auto expected=winrt::to_string(row.GetNamedString(L"diagnostic"));
        for(const bool progress:{false,true}) {
            std::string diagnostic;
            try {
                if(progress)(void)parse_lesson_progress_metadata(bytes,"es");
                else (void)parse_lesson_json(bytes,"es");
            }catch(const std::exception& error){diagnostic=error.what();}
            require(!diagnostic.empty(),"A forged choice prose publication was accepted: "+reason);
            if(!expected.empty())require(diagnostic.find(expected)!=std::string::npos,
                "A different failure masked the intended choice-history rejection: "+reason+" / "+diagnostic);
        }
    }
    std::cout<<"PASS U2L10 choice prose: "<<rejected.Size()
        <<" answer/history/projection/semantic counterexamples rejected by both native loaders\n";
}
}

int main(int argc,char** argv) {
    try {
        if(argc==3&&std::string_view(argv[1])=="--prose-history") {
            verify_prose_history(std::filesystem::path(argv[2]));
            return 0;
        }
        if(argc==3&&std::string_view(argv[1])=="--retirement-history") {
            const std::filesystem::path directory(argv[2]);
            verify_retirement_choice_pair(directory,"synthetic");
            verify_retirement_choice_pair(directory,"published");
            const auto manifest=JsonObject::Parse(winrt::to_hstring(
                read_file(directory/"manifest.json")));
            const auto rejected=manifest.GetNamedArray(L"rejected");
            require(rejected.Size()>0,"The retirement fixture has no protected-contract counterexamples");
            for(const auto& value:rejected) {
                const auto record=value.GetObject();
                const auto source=read_file(directory/winrt::to_string(record.GetNamedString(L"file")));
                const auto reason=winrt::to_string(record.GetNamedString(L"reason"));
                rejected_change(source,[](JsonObject) {},reason);
            }
            std::cout<<"PASS retirement choice: "<<rejected.Size()
                <<" authenticated-history counterexamples rejected by both Windows loaders\n";
            return 0;
        }
        require(argc==4,"Expected original, reviewed and synthetic unmarked-predecessor choice JSON paths");
        const auto original=load_lesson(std::filesystem::path(argv[1]),"es");
        const auto revised=load_lesson(std::filesystem::path(argv[2]),"es");
        require(original.id==revised.id&&original.locale=="es"&&
            revised.state_layout.version==2,
            "A generic editorial choice was attached to another lesson");
        constexpr std::string_view identity="block_0007_0002";
        const auto old_position=stable_block_position(original,identity);
        const auto position=stable_block_position(revised,identity);
        require(old_position&&position&&*position==*old_position&&
            position->page==7&&position->block==2,
            "The general review changed the original activity coordinates");
        const auto& before=original.pages[position->page].blocks[position->block];
        const auto& after=revised.pages[position->page].blocks[position->block];
        require(before.type==BlockType::multiple_choice&&
            after.type==BlockType::multiple_choice&&
            !before.choice_revision&&after.choice_revision&&
            after.correct_index==before.correct_index&&
            after.options.size()==before.options.size()&&
            after.options.at(0)!=before.options.at(0)&&
            after.options.at(1)==before.options.at(1)&&
            after.options.at(2)==before.options.at(2)&&
            after.question==before.question&&
            after.explanation==before.explanation&&
            after.review_feedback_options==before.review_feedback_options,
            "A general choice changed protected answers, feedback or ownership");
        require(after.choice_revision->original_options==before.options&&
            after.choice_revision->general_option_index==0&&
            after.choice_revision->original_correct_index==before.correct_index&&
            after.choice_revision->original_question==before.question&&
            evaluate_choice(after,0).correct&&!evaluate_choice(after,1).correct,
            "The reviewed option lost its original archived answer contract");
        const auto before_marker=before.text_semantics.find("/options/0");
        const auto after_marker=after.text_semantics.find("/options/0");
        require(before_marker!=before.text_semantics.end()&&
            after_marker!=after.text_semantics.end()&&
            before_marker->second.source==before.options.front()&&
            after_marker->second.source==after.options.front()&&
            before_marker->second.spans==after_marker->second.spans,
            "The editor lost its exact revised semantic source or inherited an old marker");
        // The same slot is retained. Historical data are not deleted or
        // overwritten by opening the new editorial representation.
        state::StateData saved;
        state::prepare_lesson_state(saved,original);
        state::Response response;
        response.selected=0;
        response.correct=true;
        response.raw_json=R"({"incorrectAttempts":3,"opaque":{"cursor":11}})";
        const auto legacy=exercise_key(original,before);
        state::put_response(saved,legacy,response);
        const auto old_state=state::encode(saved);
        const auto reopened=state::decode(old_state);
        require(state::encode(reopened)==old_state,
            "The general choice review erased a historical response on roundtrip");
        auto imported=reopened;
        state::prepare_lesson_state(imported,revised);
        require(state::string_value(imported,"learning_progress",legacy)==
                    state::string_value(saved,"learning_progress",legacy),
            "Importing the old response into the revised lesson erased its historical receipt");
        const auto revised_key=state::response_storage_key(revised,after);
        require(revised_key==legacy,
            "A wording-only revision moved the choice into a different storage slot");
        const auto preserved=state::get_response(imported,revised,after);
        require(preserved&&preserved->correct&&preserved->selected==0&&
            !preserved->historical_choice&&
            state::selected_choice_text(*preserved,after)==after.options[0]&&
            state::choice_correct_feedback(*preserved,after)==after.explanation,
            "A valid historical success was lost or displayed as an ungraded old answer");
        // The identical receipt can be reused only while the authenticated
        // current option representation is intact. A post-load mutation
        // must never borrow the valid earlier result.
        for(int mutation=0;mutation<2;++mutation) {
            auto altered=after;
            if(mutation==0)altered.options[0]+=" Falso";
            else altered.correct_index=1;
            bool refused=false;
            try {(void)state::get_response(imported,revised,altered);}
            catch(const std::runtime_error&) {refused=true;}
            require(refused,
                "An unauthenticated option change reused historical success");
        }
        require(state::string_value(imported,"learning_progress",legacy)==
                state::string_value(saved,"learning_progress",legacy),
            "Reading the revised answer rewrote the historical selection or attempts");

        // A reviewed incorrect choice is also compatible: its position and
        // feedback are unchanged. Preserve its original retry/review receipt.
        auto wrong=imported;
        state::Response incorrect;
        incorrect.selected=1;incorrect.correct=false;
        incorrect.raw_json=R"({"incorrectAttempts":4,"opaque":{"caret":7}})";
        state::mark_response_reviewed(incorrect,before);
        state::put_response(wrong,legacy,incorrect);
        const auto wrong_raw=state::string_value(wrong,"learning_progress",legacy);
        const auto restored_wrong=state::get_response(wrong,revised,after);
        require(restored_wrong&&!restored_wrong->correct&&
            restored_wrong->selected==1&&!restored_wrong->historical_choice&&
            state::response_reviewed(*restored_wrong,after)&&
            state::choice_incorrect_feedback(*restored_wrong,after)==
                incorrect_feedback(after,1)&&
            state::string_value(wrong,"learning_progress",legacy)==wrong_raw,
            "An editorial correction erased prior incorrect attempts or review");

        // The eight existing closed revisions must still distinguish old
        // answers from genuinely new response representations.
        auto closed=after;
        closed.choice_revision->general_option_index.reset();
        require(state::response_storage_key(revised,closed)!=legacy,
            "A response-changing closed revision incorrectly borrowed the old receipt");
        const auto closed_history=state::get_response(imported,revised,closed);
        require(closed_history&&closed_history->correct&&
            closed_history->historical_choice&&
            state::selected_choice_text(*closed_history,closed)==before.options[0],
            "An established response-changing revision lost its separate historical selection");

        const auto reviewed_file=read_file(std::filesystem::path(argv[2]));
        // The Python authoring helper refuses whitespace-only replacements.
        // Both Windows loaders must reject the same Unicode cases at the
        // option contract, not merely as an unrelated stale semantic hash.
        for(const wchar_t* blank : {L"\u00a0",L"\u0085",L"\u2003",L"\u202f",L"\u3000",L"\u00a0\t\u2003"}) {
            auto candidate=JsonObject::Parse(winrt::to_hstring(reviewed_file));
            candidate.GetNamedObject(L"stateLayout").GetNamedArray(L"choiceRevisions")
                .GetObjectAt(0).Insert(L"replacement",JsonValue::CreateStringValue(blank));
            // Keep the legacy authored marker internally consistent with the
            // forged replacement; otherwise an earlier SHA mismatch would
            // mask the nonblank-contract regression we intend to exercise.
            const auto digest=state::sha256(winrt::to_string(winrt::hstring(blank)));
            bool marker_updated=false;
            for(const auto& entry:candidate.GetNamedObject(L"textSemantics").GetNamedArray(L"fields")) {
                const auto field=entry.GetObject();
                if(field.GetNamedString(L"block")!=L"block_0007_0002"||
                        field.GetNamedString(L"path")!=L"/options/0")continue;
                require(!marker_updated,"The choice has duplicate authored option markers");
                field.Insert(L"sourceSha256",
                    JsonValue::CreateStringValue(winrt::to_hstring(digest)));
                marker_updated=true;
            }
            require(marker_updated,"The reviewed choice lost its authored option marker");
            const auto authored=winrt::to_string(candidate.Stringify());
            for(const bool progress : {false,true}) {
                std::string diagnostic;
                try {
                    if(progress) (void)parse_lesson_progress_metadata(authored,"es");
                    else (void)parse_lesson_json(authored,"es");
                } catch(const std::exception& error) { diagnostic=error.what(); }
                require(diagnostic.find("one nonblank option")!=std::string::npos,
                    "A Unicode-blank choice was accepted or rejected for an unrelated reason");
            }
        }
        // Exercise the NEW authoring contract against the actual Windows
        // full and metadata loaders. The existing published fixture remains
        // unchanged and retains its legacy five-field archive.
        auto explicit_root=JsonObject::Parse(winrt::to_hstring(reviewed_file));
        const auto revision=explicit_root.GetNamedObject(L"stateLayout")
            .GetNamedArray(L"choiceRevisions").GetObjectAt(0);
        const auto revised_text=revision.GetNamedString(L"replacement");
        const std::wstring_view revised_words(revised_text.c_str());
        constexpr std::wstring_view selected_words=L"condición";
        const auto start=revised_words.find(selected_words);
        require(start!=std::wstring_view::npos,
            "The real revised option lost its exact UTF-16 semantic selection");
        JsonObject semantic_span;
        semantic_span.Insert(L"start",JsonValue::CreateNumberValue(static_cast<double>(start)));
        semantic_span.Insert(L"end",JsonValue::CreateNumberValue(
            static_cast<double>(start+selected_words.size())));
        semantic_span.Insert(L"text",JsonValue::CreateStringValue(L"condición"));
        semantic_span.Insert(L"role",JsonValue::CreateStringValue(L"prose"));
        semantic_span.Insert(L"reading",JsonValue::CreateStringValue(L"natural"));
        auto fields=explicit_root.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");
        bool updated=false;
        for(unsigned i=0;i<fields.Size();++i) {
            const auto field=fields.GetObjectAt(i);
            if(field.GetNamedString(L"block")!=L"block_0007_0002"||
                    field.GetNamedString(L"path")!=L"/options/0")continue;
            require(!updated&&field.GetNamedArray(L"spans").Size()==0,
                "The published legacy fixture unexpectedly has authored option spans");
            field.GetNamedArray(L"spans").Append(semantic_span);
            revision.Insert(L"semanticAfter",field);
            revision.Insert(L"semanticRootBefore",JsonValue::CreateBooleanValue(true));
            updated=true;
        }
        require(updated,"The real choice has no existing semantic owner");
        const auto explicit_bytes=winrt::to_string(explicit_root.Stringify());
        const auto explicitly_marked=parse_lesson_json(explicit_bytes,"es");
        const auto explicitly_metadata=parse_lesson_progress_metadata(explicit_bytes,"es");
        const auto marked_position=stable_block_position(explicitly_marked,identity);
        require(marked_position&&*marked_position==*position,
            "Explicit option semantics changed the published state address");
        const auto& marked=explicitly_marked.pages[position->page].blocks[position->block];
        const auto& marked_meta=explicitly_metadata.pages[position->page].blocks[position->block];
        const auto mark=marked.text_semantics.find("/options/0");
        require(mark!=marked.text_semantics.end()&&mark->second.spans.size()==1&&
                mark->second.source==marked.options.front()&&
                marked_meta.text_semantics==marked.text_semantics,
            "Windows lost explicit authored option text or its new UTF-16 range");
        auto marked_state=imported;
        state::prepare_lesson_state(marked_state,explicitly_marked);
        const auto marked_receipt=state::get_response(marked_state,explicitly_marked,marked);
        require(state::response_storage_key(explicitly_marked,marked)==legacy&&
                marked_receipt&&marked_receipt->correct&&marked_receipt->selected==0,
            "An explicitly revised text marker erased a valid historical answer");
        // A previously unmarked option may acquire a new technical/prose
        // descriptor. Its old reading must remain unmarked, while its proven
        // correct answer and visible new selection survive both loaders.
        // The fixture generator removed the predecessor marker BEFORE
        // authoring this synthetic variant, and validated its transition.
        // Never invent missing historical authoring by editing an archive
        // directly inside the Windows loader test.
        const auto unmarked_bytes=read_file(std::filesystem::path(argv[3]));
        const auto unmarked_lesson=parse_lesson_json(unmarked_bytes,"es");
        const auto unmarked_progress=parse_lesson_progress_metadata(unmarked_bytes,"es");
        const auto& unmarked_block=unmarked_lesson.pages[position->page].blocks[position->block];
        const auto& unmarked_meta=unmarked_progress.pages[position->page].blocks[position->block];
        const auto unmarked_marker=unmarked_block.text_semantics.find("/options/0");
        const auto unmarked_progress_marker=unmarked_meta.text_semantics.find("/options/0");
        require(unmarked_block.choice_revision&&
                unmarked_block.choice_revision->general_option_index==0&&
                !unmarked_block.choice_revision->original_option_text_semantics&&
                unmarked_marker!=unmarked_block.text_semantics.end()&&
                unmarked_marker->second.source==unmarked_block.options.front()&&
                unmarked_marker->second.spans.size()==1&&
                unmarked_marker->second.spans.front().text=="condición para aceptar la transición"&&
                unmarked_progress_marker!=unmarked_meta.text_semantics.end()&&
                unmarked_progress_marker->second==unmarked_marker->second&&
                unmarked_meta.choice_revision&&
                !unmarked_meta.choice_revision->original_option_text_semantics,
            "An originally unmarked option borrowed previous highlights or lost its own new selection");
        // The two independently authored variants intentionally select
        // different ranges. Comparing their entire maps would reject valid
        // data despite correct source ownership and read-only progress.
        require(mark->second.spans.front().text!=unmarked_marker->second.spans.front().text,
            "The independent QA cases no longer exercise distinct current UTF-16 selections");
        auto unmarked_state=imported;
        state::prepare_lesson_state(unmarked_state,unmarked_lesson);
        const auto unmarked_receipt=state::get_response(
            unmarked_state,unmarked_lesson,unmarked_block);
        require(state::response_storage_key(unmarked_lesson,unmarked_block)==legacy&&
                unmarked_receipt&&unmarked_receipt->correct&&
                unmarked_receipt->selected==0,
            "Adding semantic marks to an unmarked option invalidated old response credit");

        rejected_change(unmarked_bytes,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout").GetNamedArray(L"choiceRevisions")
                .GetObjectAt(0).Remove(L"semanticRootBefore");
        },"An originally unmarked revision omitted its required root provenance");
        rejected_change(unmarked_bytes,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout").GetNamedArray(L"choiceRevisions")
                .GetObjectAt(0).GetNamedObject(L"semanticAfter")
                .GetNamedArray(L"spans").Clear();
        },"A newly marked option silently changed its authenticated UTF-16 ranges");

        rejected_change(explicit_bytes,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout").GetNamedArray(L"choiceRevisions")
                .GetObjectAt(0).GetNamedObject(L"semanticAfter")
                .Insert(L"sourceSha256",JsonValue::CreateStringValue(
                    L"0000000000000000000000000000000000000000000000000000000000000000"));
        },"A forged new semantic archive was accepted");
        rejected_change(explicit_bytes,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout").GetNamedArray(L"choiceRevisions")
                .GetObjectAt(0).Insert(L"semanticRootBefore",
                    JsonValue::CreateBooleanValue(false));
        },"A false original semantic-root claim bypassed the previous marker");
        rejected_change(explicit_bytes,[](JsonObject root) {
            auto spans=root.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");
            for(unsigned i=0;i<spans.Size();++i) {
                const auto field=spans.GetObjectAt(i);
                if(field.GetNamedString(L"block")==L"block_0007_0002"&&
                   field.GetNamedString(L"path")==L"/options/0")
                    field.GetNamedArray(L"spans").Clear();
            }
        },"An active option silently lost its archived new emphasis");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedArray(L"pages").GetObjectAt(7)
                .GetNamedArray(L"blocks").GetObjectAt(2)
                .GetNamedArray(L"options").SetAt(1,
                    JsonValue::CreateStringValue(L"Alternativa falsificada"));
        },"An unrelated option changed without review");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedArray(L"pages").GetObjectAt(7)
                .GetNamedArray(L"blocks").GetObjectAt(2).Insert(
                    L"correctIndex",JsonValue::CreateNumberValue(1));
        },"The correct choice index was changed");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout")
                .GetNamedArray(L"choiceRevisions").GetObjectAt(0).Insert(
                    L"replacement",JsonValue::CreateStringValue(L"Otro contrato"));
        },"A forged replacement option gained credit");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout")
                .GetNamedArray(L"choiceRevisions").GetObjectAt(0)
                .GetNamedObject(L"block").GetNamedArray(L"options").SetAt(
                    0,JsonValue::CreateStringValue(L"Origen falso"));
        },"A forged full previous block bypassed the response signature");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout")
                .GetNamedArray(L"choiceRevisions").GetObjectAt(0)
                .GetNamedObject(L"semanticBefore").Insert(L"sourceSha256",
                    JsonValue::CreateStringValue(L"0000000000000000000000000000000000000000000000000000000000000000"));
        },"A forged previous semantic SHA was accepted");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout")
                .GetNamedArray(L"choiceRevisions").GetObjectAt(0)
                .GetNamedObject(L"semanticBefore").GetNamedArray(L"spans")
                .Append(JsonObject::Parse(L"{\"start\":0,\"end\":2,\"text\":\"La\",\"role\":\"prose\"}"));
        },"A rewritten previous text span was accepted");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout")
                .GetNamedArray(L"choiceRevisions").GetObjectAt(0)
                .Remove(L"semanticBefore");
        },"The prior semantic archive was deleted");
        rejected_change(reviewed_file,[](JsonObject root) {
            root.GetNamedObject(L"stateLayout")
                .GetNamedArray(L"choiceRevisions").Clear();
        },"A reviewed option lost its previous history archive");
        rejected_change(reviewed_file,[](JsonObject root) {
            const auto descriptors=root.GetNamedObject(L"textSemantics")
                .GetNamedArray(L"fields");
            for(unsigned i=0;i<descriptors.Size();++i) {
                const auto descriptor=descriptors.GetObjectAt(i);
                if(descriptor.GetNamedString(L"block")==L"block_0007_0002"&&
                   descriptor.GetNamedString(L"path")==L"/options/0") {
                    descriptor.Insert(L"sourceSha256",
                        JsonValue::CreateStringValue(
                            L"3b045ec5f155cd6fbfb0fb089cbe0165ec05ad5cea96b07a6336ee4ccf23c8f0"));
                    break;
                }
            }
        },"A stale pre-review voice/text digest was accepted");
        std::cout<<"PASS general U9 choice review: unchanged addresses, answers, archive and rejected tampering\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"FAIL general choice editorial: "<<e.what()<<'\n';
        return 1;
    }
}
