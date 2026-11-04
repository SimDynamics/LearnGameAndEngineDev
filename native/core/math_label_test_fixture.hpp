#pragma once
// QA only: complete original/revised blocks are independently pinned. Both
// states are reconstructed in memory, so Work may integrate the reviewed label.
#include "core/compile_check_test_fixture.hpp"
#include "platform/windows/content_loader.hpp"
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>

namespace lgaed::test {
inline constexpr std::string_view u11_domain_identity="block_0008_0002";
inline constexpr std::string_view u11_domain_before="fed5b6d2dcfb3321715725fe61a366c8c1f337d1b24a7e3e0b1fd43ef1be0549";
inline constexpr std::string_view u11_domain_after="d2ec5c530dba31ba0c4d2b3575d609bfbd688fca0dbb4d94a0fd1524a9803187";
inline constexpr std::string_view u11_domain_android_before="34045982a2a7b895013785d3756ebbee81e4da9b5e08de8cd22f53285c2186fb";
inline constexpr std::string_view u11_domain_android_after="a7d0293021148d49a4fe25719292b52dc7becdb63d75b5f84e5dc93575abb77e";
struct MathLabelFixture { curriculum::Lesson before,after;curriculum::StatePosition position; };
inline MathLabelFixture u11_domain_label_fixture(const curriculum::Lesson& source){
    using namespace winrt::Windows::Data::Json;
    const auto ensure=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    ensure(source.id=="cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas"&&source.locale=="es",
        "The U11 domain fixture requires its original Spanish lesson");
    const auto validated=windows::parse_lesson_json(source.raw_json,source.locale);
    const auto at=curriculum::stable_block_position(validated,u11_domain_identity);
    ensure(at&&at->page==8&&at->block==2,"The U11 domain fixture lost its actual matching identity");
    const auto root=JsonObject::Parse(winrt::to_hstring(validated.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(8).GetNamedArray(L"blocks").GetObjectAt(2);
    const auto digest=compile_check_test_fingerprint(block);
    ensure(digest==u11_domain_before||digest==u11_domain_after,"The U11 domain source differs from both complete reviewed payloads");
    if(digest==u11_domain_after){
        JsonArray retained;unsigned removed=0;
        for(const auto& value:root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial")){
            const auto edge=value.GetObject();
            if(edge.GetNamedString(L"id")==L"block_0008_0002"
                &&winrt::to_string(edge.GetNamedString(L"beforeSha256"))==u11_domain_before
                &&winrt::to_string(edge.GetNamedString(L"afterSha256"))==u11_domain_after){++removed;continue;}
            retained.Append(value);
        }
        ensure(removed==1,"An integrated U11 domain label requires its exact reviewed edge");
        root.GetNamedObject(L"stateLayout").Insert(L"editorial",retained);
        block.GetNamedArray(L"responses").GetObjectAt(0).GetNamedArray(L"sources").GetObjectAt(5)
            .Insert(L"label",JsonValue::CreateStringValue(L"100+1;"));
    }
    ensure(compile_check_test_fingerprint(block)==u11_domain_before,"The complete original U11 domain block was not authenticated");
    auto before=windows::parse_lesson_json(winrt::to_string(root.Stringify()),source.locale);before.source_path=source.source_path;
    block.GetNamedArray(L"responses").GetObjectAt(0).GetNamedArray(L"sources").GetObjectAt(5)
        .Insert(L"label",JsonValue::CreateStringValue(L"99+2;"));
    ensure(compile_check_test_fingerprint(block)==u11_domain_after,"The U11 domain fixture changed more than the exact label");
    JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(L"block_0008_0002"));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(u11_domain_before)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(u11_domain_after)));
    root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(proof);
    auto after=windows::parse_lesson_json(winrt::to_string(root.Stringify()),source.locale);after.source_path=source.source_path;
    return {std::move(before),std::move(after),*at};
}
}
