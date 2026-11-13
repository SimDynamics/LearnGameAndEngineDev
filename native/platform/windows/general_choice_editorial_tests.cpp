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
            after.choice_revision->original_correct_index==before.correct_index&&
            after.choice_revision->original_question==before.question&&
            evaluate_choice(after,0).correct&&!evaluate_choice(after,1).correct,
            "The reviewed option lost its original archived answer contract");
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
        const auto new_key=state::response_storage_key(revised,after);
        require(!new_key.empty()&&new_key!=legacy,
            "A revised option silently reused its historical answer receipt");

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
                .GetNamedArray(L"choiceRevisions").Clear();
        },"A reviewed option lost its previous history archive");
        std::cout<<"PASS general U9 choice review: unchanged addresses, answers, archive and rejected tampering\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"FAIL general choice editorial: "<<e.what()<<'\n';
        return 1;
    }
}
