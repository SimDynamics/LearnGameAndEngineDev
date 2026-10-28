#pragma once
// QA only: builds the reviewed contract and editorial edge in memory. No asset
// or published source is modified and no native receipt is manufactured here.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include <array>
#include <map>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::test {
struct CompileCheckCase {
    const char* filename;
    const char* identity;
    const char* diagnostic;
    const char* statement;
    const char* error;
};
inline constexpr std::array<CompileCheckCase,5> compile_check_cases{{
    {"lesson_consultas_const_y_objetos_constantes.json","block_0006_0006","const-member-call","fija.cambiar_a(4,2);","C2662"},
    {"lesson_valores_por_defecto_con_significado.json","block_0012_0008","explicit-copy-initialization","Contador no_permitido = 3;","C2440"},
    {"lesson_valores_por_defecto_con_significado.json","block_0012_0011","explicit-argument-conversion","leer(3);","C2664"},
    {"lesson_valores_por_defecto_con_significado.json","block_0014_0011","explicit-assignment","destino = 3;","C2679"},
    {"lesson_argumentos_con_tipos_que_expresan_su_funcion.json","block_0006_0006","swapped-named-arguments","const Caja mal{Alto{4}, Ancho{3}};","C2440"}
}};
inline void compile_check_test_canonical(const winrt::Windows::Data::Json::IJsonValue& value,std::string& result) {
    using namespace winrt::Windows::Data::Json;
    const auto string=[&](const winrt::hstring& source){const auto bytes=winrt::to_string(source);result+='s'+std::to_string(bytes.size())+':'+bytes;};
    switch(value.ValueType()) {
    case JsonValueType::String:string(value.GetString());break;
    case JsonValueType::Object:{result+='{';std::map<std::wstring,IJsonValue> sorted;
        for(const auto& entry:value.GetObject())sorted.emplace(std::wstring(entry.Key()),entry.Value());
        for(const auto& [name,item]:sorted){string(winrt::hstring(name));compile_check_test_canonical(item,result);}result+='}';break;}
    case JsonValueType::Array:result+='[';for(const auto& item:value.GetArray())compile_check_test_canonical(item,result);result+=']';break;
    case JsonValueType::Boolean:result+=value.GetBoolean()?'t':'f';break;
    case JsonValueType::Null:result+='z';break;
    case JsonValueType::Number:result+='n'+std::to_string(static_cast<std::int64_t>(value.GetNumber()))+';';break;
    default:throw std::runtime_error("Unsupported QA compile-check payload");
    }
}
inline std::string compile_check_test_fingerprint(const winrt::Windows::Data::Json::IJsonValue& value) {
    std::string frame;compile_check_test_canonical(value,frame);return state::sha256(frame);
}
inline std::string compile_check_json(const curriculum::Lesson& original,std::string_view identity,
        std::string_view diagnostic,std::string_view explanation={}) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,identity);
    if(!position)throw std::runtime_error("Missing reviewed QA diagnostic editor");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json)),layout=root.GetNamedObject(L"stateLayout");
    const auto block=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject()
        .GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
    if(block.HasKey(L"compileCheck"))throw std::runtime_error("The QA diagnostic editor is already authored");
    const auto before=compile_check_test_fingerprint(block);JsonObject check;
    check.Insert(L"diagnostic",JsonValue::CreateStringValue(winrt::to_hstring(diagnostic)));
    if(!explanation.empty())check.Insert(L"explanation",JsonValue::CreateStringValue(winrt::to_hstring(explanation)));
    block.Insert(L"compileCheck",check);JsonObject proof;
    proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(before)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(compile_check_test_fingerprint(block))));
    layout.GetNamedArray(L"editorial").Append(proof);return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson with_compile_check(const curriculum::Lesson& original,std::string_view identity,
        std::string_view diagnostic,std::string_view explanation={}) {
    auto result=windows::parse_lesson_json(compile_check_json(original,identity,diagnostic,explanation),original.locale);
    result.source_path=original.source_path;return result;
}
} // namespace lgaed::test
