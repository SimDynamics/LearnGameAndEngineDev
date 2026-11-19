// Real Windows StateLayout regression for an unpublished matching text revision.
// Uses the exact existing U5L13 lesson; edits only WinRT JSON held in memory.
#include "core/compile_check_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::test {
namespace {
using namespace curriculum;
using namespace windows;
using namespace winrt::Windows::Data::Json;

void check(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(std::string(message));
}
winrt::hstring wide(std::string_view value) {return winrt::to_hstring(value);}
std::string utf8(const winrt::hstring& value) {return winrt::to_string(value);}
std::string fingerprint(const IJsonValue& value) {
    return compile_check_test_fingerprint(value);
}
std::string replace_name(std::string original) {
    return std::regex_replace(original,std::regex(R"(\bmaxima\b)"),
                              "frecuencia_maxima_encontrada");
}
const Block& existing(const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);
    check(position.has_value(),"Missing original matching activity");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
JsonObject owner(const JsonObject& root,const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);
    check(position.has_value(),"The matching source lost its stable ID");
    return root.GetNamedArray(L"pages").GetObjectAt(
        static_cast<unsigned>(position->page)).GetNamedArray(L"blocks").GetObjectAt(
            static_cast<unsigned>(position->block));
}
std::string pointer_text(const JsonObject& block,std::string_view path) {
    IJsonValue current=block;
    for(std::size_t start=1;start<path.size();) {
        const auto end=path.find('/',start);
        const auto segment=path.substr(start,end==path.npos?
            std::string_view::npos:end-start);
        if(current.ValueType()==JsonValueType::Object)
            current=current.GetObject().GetNamedValue(wide(segment));
        else if(current.ValueType()==JsonValueType::Array) {
            const auto position=static_cast<unsigned>(
                std::stoul(std::string(segment)));
            check(position<current.GetArray().Size(),
                "An authored span moved to an unrelated card");
            current=current.GetArray().GetAt(position);
        }else throw std::runtime_error("Invalid matching public path");
        if(end==path.npos)break;
        start=end+1;
    }
    check(current.ValueType()==JsonValueType::String,
        "Matching public path does not refer to text");
    return utf8(current.GetString());
}
std::size_t old_occurrence(const winrt::hstring& source,
        const winrt::hstring& selected,std::size_t start) {
    const std::wstring_view original(source.c_str(),source.size());
    const std::wstring_view value(selected.c_str(),selected.size());
    check(start+value.size()<=original.size()&&
        original.substr(start,value.size())==value,
        "Previous UTF-16 span no longer selects its exact source");
    std::size_t ordinal=0;
    for(auto at=original.find(value);at!=std::wstring_view::npos&&at<start;
        at=original.find(value,at+1))++ordinal;
    return ordinal;
}
std::size_t selected_position(const winrt::hstring& source,
        const winrt::hstring& selection,std::size_t occurrence) {
    const std::wstring_view full(source.c_str(),source.size());
    const std::wstring_view part(selection.c_str(),selection.size());
    auto at=full.find(part);
    for(std::size_t n=0;n<occurrence&&at!=std::wstring_view::npos;++n)
        at=full.find(part,at+1);
    check(at!=std::wstring_view::npos,
        "A revised span lost the selected occurrence of its source");
    return at;
}
JsonObject relocated_marker(const JsonObject& prior,
        std::string_view previous_source,std::string_view current_source) {
    auto changed=JsonObject::Parse(prior.Stringify());
    changed.Insert(L"sourceSha256",
        JsonValue::CreateStringValue(wide(state::sha256(current_source))));
    const auto spans=changed.GetNamedArray(L"spans");
    const auto old_source=wide(previous_source),new_source=wide(current_source);
    for(unsigned index=0;index<spans.Size();++index) {
        const auto span=spans.GetObjectAt(index);
        const auto old_selection=span.GetNamedString(L"text");
        const auto start=static_cast<std::size_t>(
            span.GetNamedNumber(L"start"));
        const auto ordinal=old_occurrence(old_source,old_selection,start);
        const auto selection=wide(replace_name(utf8(old_selection)));
        const auto at=selected_position(new_source,selection,ordinal);
        span.Insert(L"start",JsonValue::CreateNumberValue(
            static_cast<double>(at)));
        span.Insert(L"end",JsonValue::CreateNumberValue(
            static_cast<double>(at+selection.size())));
        span.Insert(L"text",JsonValue::CreateStringValue(selection));
    }
    return changed;
}
void rejects_both_loaders(const JsonObject& source,
        std::string_view reason) {
    const auto json=utf8(source.Stringify());
    for(const bool compact:{false,true}) {
        bool refused=false;
        try {
            if(compact)(void)parse_lesson_progress_metadata(json,"es");
            else (void)parse_lesson_json(json,"es");
        }catch(const std::exception& error) {
            refused=true;
            check(std::string_view(error.what()).find(reason)!=std::string_view::npos,
                "A falsified matching archive was rejected for an unrelated reason");
        }
        check(refused,"The native loader accepted a forged matching-text archive");
    }
}
} // namespace

int math_matching_text_live_windows(const std::filesystem::path& content,
        const std::filesystem::path& qa) {
    constexpr std::string_view identity="block_0007_0007";
    const auto path=content/
        "colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json";
    const auto baseline=load_lesson(path);
    const auto& previous=existing(baseline,identity);
    check(previous.type==BlockType::math_puzzle&&
        previous.math_evaluation_raw_json.empty(),
        "The genuine frequency matching activity changed its original evaluation");

    const auto root=JsonObject::Parse(wide(baseline.raw_json));
    const auto current=owner(root,baseline,identity);
    const auto original=JsonObject::Parse(current.Stringify());
    const auto original_sha=fingerprint(original);
    const auto layout=root.GetNamedObject(L"stateLayout");
    const auto base_editorial=JsonArray::Parse(
        layout.GetNamedArray(L"editorial").Stringify());
    JsonObject prefix;
    prefix.Insert(L"lessonId",JsonValue::CreateStringValue(wide(baseline.id)));
    prefix.Insert(L"locale",JsonValue::CreateStringValue(L"es"));
    prefix.Insert(L"editorial",base_editorial);
    const auto history=fingerprint(prefix);
    std::vector<std::string> paths{
        "/prompt","/feedback/correct",
        "/responses/0/sources/2/content/0/text",
        "/responses/0/sources/2/label",
        "/responses/0/sources/3/content/0/text",
        "/responses/0/sources/3/label"
    };
    std::ranges::sort(paths);
    auto response=current.GetNamedArray(L"responses").GetObjectAt(0);
    auto old_source=response.GetNamedArray(L"sources");
    const auto edit=[&](const JsonObject& obj,const wchar_t* key) {
        const auto before=utf8(obj.GetNamedString(key));
        const auto after=replace_name(before);
        check(before!=after,"A genuine matching text field was not revised");
        obj.Insert(key,JsonValue::CreateStringValue(wide(after)));
    };
    edit(current,L"prompt");
    edit(current.GetNamedObject(L"feedback"),L"correct");
    for(unsigned i:{2U,3U}) {
        const auto card=old_source.GetObjectAt(i);
        edit(card,L"label");
        edit(card.GetNamedArray(L"content").GetObjectAt(0),L"text");
    }
    const auto actual_sha=fingerprint(current);
    check(actual_sha!=original_sha,
        "Matching text review needs an actual textual difference");

    const auto overlay=root.GetNamedObject(L"textSemantics");
    const auto fields=overlay.GetNamedArray(L"fields");
    JsonObject semantic_before,semantic_after;
    JsonArray path_list;
    for(const auto& name:paths) {
        path_list.Append(JsonValue::CreateStringValue(wide(name)));
        if(name.ends_with("/label")&&name.find("/sources/")!=name.npos) {
            semantic_before.Insert(wide(name),JsonValue::Parse(L"null"));
            semantic_after.Insert(wide(name),JsonValue::Parse(L"null"));
            continue;
        }
        std::optional<unsigned> position;
        for(unsigned index=0;index<fields.Size();++index) {
            const auto field=fields.GetObjectAt(index);
            if(field.GetNamedString(L"block")==wide(identity)&&
                field.GetNamedString(L"path")==wide(name)) {
                check(!position,"Duplicate authored matching-text marker");
                position=index;
            }
        }
        check(position.has_value(),"Missing original UTF-16 source field");
        const auto original_marker=JsonObject::Parse(
            fields.GetObjectAt(*position).Stringify());
        const auto revised_marker=relocated_marker(original_marker,
            pointer_text(original,name),pointer_text(current,name));
        semantic_before.Insert(wide(name),original_marker);
        semantic_after.Insert(wide(name),revised_marker);
        fields.SetAt(*position,revised_marker);
    }

    const auto record=JsonObject{};
    record.Insert(L"id",JsonValue::CreateStringValue(wide(identity)));
    record.Insert(L"rawJson",JsonValue::CreateStringValue(original.Stringify()));
    record.Insert(L"paths",path_list);
    record.Insert(L"semanticBefore",semantic_before);
    record.Insert(L"semanticAfter",semantic_after);
    record.Insert(L"editorialIndex",JsonValue::CreateNumberValue(
        static_cast<double>(base_editorial.Size())));
    record.Insert(L"historySha256",JsonValue::CreateStringValue(wide(history)));
    record.Insert(L"beforeSha256",JsonValue::CreateStringValue(wide(original_sha)));
    record.Insert(L"afterSha256",JsonValue::CreateStringValue(wide(actual_sha)));
    JsonArray journal;journal.Append(record);
    layout.Insert(L"mathMatchingTextRevisions",journal);
    JsonObject edge;
    edge.Insert(L"id",JsonValue::CreateStringValue(wide(identity)));
    edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(wide(original_sha)));
    edge.Insert(L"afterSha256",JsonValue::CreateStringValue(wide(actual_sha)));
    layout.GetNamedArray(L"editorial").Append(edge);
    const auto serialized=utf8(root.Stringify());
    const auto revised=parse_lesson_json(serialized,"es");
    const auto metadata=parse_lesson_progress_metadata(serialized,"es");
    const auto& now=existing(revised,identity);
    const auto& compact=existing(metadata,identity);
    check(revised.state_layout.math_matching_text_revisions.size()==1&&
        metadata.state_layout.math_matching_text_revisions.size()==1&&
        revised.state_layout.signatures==baseline.state_layout.signatures&&
        metadata.state_layout.signatures==baseline.state_layout.signatures&&
        now.address.page==previous.address.page&&
        now.address.block==previous.address.block&&
        compact.address.page==previous.address.page&&
        compact.address.block==previous.address.block&&
        now.math_evaluation_raw_json==utf8(original.Stringify())&&
        compact.math_evaluation_raw_json==now.math_evaluation_raw_json,
        "Matching text revision changed grading, storage or original evaluation JSON");
    check(now.raw_json.find("frecuencia_maxima_encontrada")!=std::string::npos&&
        now.math_evaluation_raw_json.find("frecuencia_maxima_encontrada")==std::string::npos,
        "Matching review did not separate the visible and evaluable mathematical source");

    state::StateStore store(qa/"math-matching-text");
    auto data=store.load();
    state::prepare_lesson_state(data,baseline);
    const auto stable=state::response_storage_key(baseline,previous);
    data.preferences["matching-qa"]["foreign"]=std::string("unchanged");
    narration::History before_audio;
    before_audio.entries.push_back({"old-math-matching","block_0007_0007",
        {4,170,0,false},"page_0007"});
    narration::write_history(data,before_audio);
    const auto saved_audio=narration::read_history(data);
    store.save(data);
    state::StateStore loaded(store.root().parent_path());
    data=loaded.load();
    const auto old_data=data;
    state::prepare_lesson_state(data,revised);
    state::validate_lesson_state(data,revised);
    state::validate_lesson_state(data,metadata);
    check(state::response_storage_key(revised,now)==stable&&
        state::response_storage_key(metadata,compact)==stable&&
        narration::read_history(data)==saved_audio&&
        state::string_value(data,"matching-qa","foreign")=="unchanged",
        "Matching text created a new response owner or modified a reading marker");
    loaded.save(data);
    state::StateStore reopened(loaded.root().parent_path());
    const auto persisted=reopened.load();
    state::validate_lesson_state(persisted,revised);
    check(persisted==data&&narration::read_history(persisted)==saved_audio&&
        data==old_data,
        "Matching review mutated an existing learner profile or its historical audio");

    const auto rejects=[&](std::string_view reason,const auto& mutate) {
        const auto forged=JsonObject::Parse(wide(serialized));
        mutate(forged);
        rejects_both_loaders(forged,reason);
    };
    rejects("A matching text archive changed IDs, pairs, grading or JSON order",
        [](const JsonObject& modified) {
            const auto before=modified.GetNamedArray(L"pages").GetObjectAt(7)
                .GetNamedArray(L"blocks").GetObjectAt(7);
            auto expected=before.GetNamedArray(L"responses").GetObjectAt(0)
                .GetNamedObject(L"validator").GetNamedObject(L"expected");
            expected.Insert(L"supera_maximo",JsonValue::CreateStringValue(L"conservar_empate"));
            const auto new_sha=fingerprint(before);
            auto record=modified.GetNamedObject(L"stateLayout").GetNamedArray(
                L"mathMatchingTextRevisions").GetObjectAt(0);
            record.Insert(L"afterSha256",JsonValue::CreateStringValue(wide(new_sha)));
            const auto editorial=modified.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
            editorial.GetObjectAt(editorial.Size()-1).Insert(
                L"afterSha256",JsonValue::CreateStringValue(wide(new_sha)));
        });
    rejects("A matching text marker uses another source's hash",
        [](const JsonObject& modified) {
            auto record=modified.GetNamedObject(L"stateLayout").GetNamedArray(
                L"mathMatchingTextRevisions").GetObjectAt(0);
            record.GetNamedObject(L"semanticAfter").GetNamedObject(L"/prompt").Insert(
                L"sourceSha256",JsonValue::CreateStringValue(wide(std::string(64,'0'))));
        });
    rejects("Matching text archive borrowed a foreign editorial prefix",
        [](const JsonObject& modified) {
            auto record=modified.GetNamedObject(L"stateLayout").GetNamedArray(
                L"mathMatchingTextRevisions").GetObjectAt(0);
            record.Insert(L"historySha256",
                JsonValue::CreateStringValue(wide(std::string(64,'0'))));
        });
    check(load_lesson(path).raw_json==baseline.raw_json,
        "A matching text regression wrote to the real curricular source");
    std::cout<<"PASS native U5L13 matching public text: six reviewed paths, "
             <<"unaltered grading/IDs, exact UTF-16 spans, source-history SHA, "
             <<"stable response, reopened learner audio and three hostile variants.\n";
    return 0;
}
} // namespace lgaed::test
