#pragma once

// QA only: an exact prior Git lesson and one reviewed output spelling in memory.
// No active asset, source, answer, draft or native completion is manufactured.
#include "core/compile_check_test_fixture.hpp"

namespace lgaed::test {
inline constexpr std::string_view crt_output_lesson_id=
    "cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia";
inline constexpr std::string_view crt_output_editor_id="block_0005_0004";
inline constexpr std::string_view crt_output_before="b475e21c42a6b93cb635f5bfd5197b66b7c1107145a413f2fa9582ab142cda35";
inline constexpr std::string_view crt_output_after="53648380e15d1148a779de47c0f94dc08f63e2319f79bdc08e72d25a6992ed31";
inline curriculum::Lesson load_pre_crt_output_lesson() {
    std::array<wchar_t,32768> executable{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size())throw std::runtime_error("Cannot resolve the prior CRT fixture directory");
    const auto path=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()
        /"test-fixtures/crt-b9e32b9/lesson_clasificar_caracteres_y_normalizar_una_copia.json";
    if(state::sha256(state::read_bytes(path))!="d8ebd48f60f6c7ac382777e9d51c63d6854c1fee6056034d9d2351a064849116")
        throw std::runtime_error("Prior CRT lesson differs from pinned b9e32b9 Git bytes");
    return windows::load_lesson(path);
}
inline std::string crt_output_json(const curriculum::Lesson& original) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,crt_output_editor_id);
    if(original.id!=crt_output_lesson_id||original.locale!="es"||original.state_layout.version!=2||!position)
        throw std::runtime_error("The CRT QA adoption requires its exact existing Spanish v2 editor");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));
    if(compile_check_test_fingerprint(block)!=crt_output_before)
        throw std::runtime_error("The CRT QA adoption requires its complete unchanged published block");
    const auto outputs=block.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0).GetNamedArray(L"outputs");
    if(outputs.Size()!=1)throw std::runtime_error("The CRT QA adoption requires its original sole output");
    const auto text=winrt::to_string(outputs.GetStringAt(0));std::string variant;
    for(const char character:text){if(character=='\n')variant+='\r';variant+=character;}
    if(text.size()!=802||std::ranges::count(text,'\n')!=42||std::ranges::count(text,'\r')!=3||variant.size()!=844)
        throw std::runtime_error("The CRT QA adoption lost its exact raw original control characters");
    outputs.Append(JsonValue::CreateStringValue(winrt::to_hstring(variant)));
    if(compile_check_test_fingerprint(block)!=crt_output_after)
        throw std::runtime_error("The CRT QA adoption differs from the exact reviewed new block");
    JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(crt_output_editor_id)));
    edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(crt_output_before)));
    edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(crt_output_after)));
    root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(edge);
    return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson with_crt_output_variant(const curriculum::Lesson& original) {
    auto result=windows::parse_lesson_json(crt_output_json(original),original.locale);
    result.source_path=original.source_path;return result;
}
// QA authoring of ordinary supported guidance. The complete reviewed execution
// payload and original adoption edge remain unchanged; only one honest full
// editorial before/after edge is appended for the supplied current wording.
inline std::string crt_output_guidance_json(const curriculum::Lesson& variant,std::string_view explanation,
        std::span<const curriculum::OutputReviewFeedback> reviews) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(variant,crt_output_editor_id);
    if(variant.id!=crt_output_lesson_id||variant.locale!="es"||variant.state_layout.version!=2||!position||
        !variant.pages[position->page].blocks[position->block].crt_output_original)
        throw std::runtime_error("Guidance QA requires the exact reviewed CRT variant");
    const auto root=JsonObject::Parse(winrt::to_hstring(variant.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));
    const auto before=compile_check_test_fingerprint(block);const auto item=block.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
    if(explanation.empty()){if(item.HasKey(L"explanation"))item.Remove(L"explanation");}
    else item.Insert(L"explanation",JsonValue::CreateStringValue(winrt::to_hstring(explanation)));
    JsonArray rules;for(const auto& review:reviews){JsonArray outputs;for(const auto& output:review.outputs)outputs.Append(JsonValue::CreateStringValue(winrt::to_hstring(output)));
        JsonObject rule;rule.Insert(L"outputs",outputs);rule.Insert(L"text",JsonValue::CreateStringValue(winrt::to_hstring(review.text)));rules.Append(rule);}
    item.Insert(L"reviewFeedback",rules);const auto after=compile_check_test_fingerprint(block);
    if(before!=after){JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(crt_output_editor_id)));
        edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(before)));edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(after)));
        root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(edge);}
    return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson with_crt_output_guidance(const curriculum::Lesson& variant,std::string_view explanation,
        std::span<const curriculum::OutputReviewFeedback> reviews) {
    auto result=windows::parse_lesson_json(crt_output_guidance_json(variant,explanation,reviews),variant.locale);
    result.source_path=variant.source_path;return result;
}
} // namespace lgaed::test
