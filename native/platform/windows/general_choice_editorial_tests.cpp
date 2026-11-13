#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

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
    require(stream.good(),"The generated U9 general-choice fixture was not found");
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void rejected_change(std::string_view original,
        const std::function<void(JsonObject)>& change,std::string_view reason) {
    auto root=JsonObject::Parse(winrt::to_hstring(original));
    change(root);
    bool rejected=false;
    try { (void)parse_lesson_json(winrt::to_string(root.Stringify()),"es"); }
    catch(const std::exception&) {rejected=true;}
    require(rejected,reason);
}
}

int main(int argc,char** argv) {
    try {
        require(argc==3,"Expected original and generated choice revision JSON paths");
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
