#pragma once
// QA only: builds the reviewed contract and editorial edge in memory. No asset
// or published source is modified and no native receipt is manufactured here.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::test {
struct CompileCheckCase {
    const char* filename;
    const char* identity;
    const char* diagnostic;
    const char* statement;
    const char* error;
    const char* before;
};
inline constexpr std::array<CompileCheckCase,5> compile_check_cases{{
    {"lesson_consultas_const_y_objetos_constantes.json","block_0006_0006","const-member-call","fija.cambiar_a(4,2);","C2662",
        "05cdb3e534649bd7e3efd1e080e5199443deace1e074d56d45490a8ff32c4c27"},
    {"lesson_valores_por_defecto_con_significado.json","block_0012_0008","explicit-copy-initialization","Contador no_permitido = 3;","C2440",
        "ac0365d7452a3a6ff52a0f6c306d01815a09196f9f49e2bc90cdd4ec5dbc9fe9"},
    {"lesson_valores_por_defecto_con_significado.json","block_0012_0011","explicit-argument-conversion","leer(3);","C2664",
        "9f7db3f3a1004a235926bc12e06e9d0bf1a93b707f3ca92f6de2876677fd9c58"},
    {"lesson_valores_por_defecto_con_significado.json","block_0014_0011","explicit-assignment","destino = 3;","C2679",
        "3e8eb728eac0d493c864596ec2364fe6c69ff85ce01408dd0020cec36ad6b087"},
    {"lesson_argumentos_con_tipos_que_expresan_su_funcion.json","block_0006_0006","swapped-named-arguments","const Caja mal{Alto{4}, Ancho{3}};","C2440",
        "b35436bcbac5b5e98731d953fb50499d5e7d3ad66ed1ad88771d56e481a8c9ed"}
}};
// The adoption tests start before the five integrated contracts, using exact
// local Git bytes exported into the build tree. Never infer an old source by
// stripping today's compileCheck/editorial fields or fall back to live assets.
inline curriculum::Lesson load_pre_compile_check_lesson(std::string_view filename) {
    constexpr std::array<std::pair<std::string_view,std::string_view>,3> sources{{
        {"lesson_consultas_const_y_objetos_constantes.json","81361b13427e9e5a0364e4f7985829f4c09e0e987fc652b8bee103f77bc1babe"},
        {"lesson_valores_por_defecto_con_significado.json","72d00361184d58d72e0c7f362f08ea8f9a707faa85373c0d23d84cbaa676dc70"},
        {"lesson_argumentos_con_tipos_que_expresan_su_funcion.json","baa356c561869b91b18adc340e8dbc61c2af0909c799b93894f41f8bff13a97a"}
    }};
    const auto input=std::ranges::find(sources,filename,[](const auto& item){return item.first;});
    if(input==sources.end())throw std::runtime_error("Unknown pre-diagnostic QA source");
    std::array<wchar_t,32768> executable{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size())throw std::runtime_error("Cannot resolve historical diagnostic fixture directory");
    const auto path=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()
        /"test-fixtures/diagnostics-7862f10"/filename;
    if(state::sha256(state::read_bytes(path))!=input->second)
        throw std::runtime_error("Historical diagnostic QA source differs from pinned 7862f1064487a961d5cfc9668d4316da6d1b3ef2 Git bytes");
    auto result=windows::load_lesson(path);
    result.source_path=std::filesystem::path(build::source_root)
        /"app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido"/filename;
    for(const auto& plan:compile_check_cases)if(filename==plan.filename) {
        const auto position=curriculum::stable_block_position(result,plan.identity);
        if(!position||curriculum::has_compiler_objective(result.pages[position->page].blocks[position->block]))
            throw std::runtime_error("Pinned diagnostic baseline must retain each original free compiler");
    }
    return result;
}
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
    const auto plan=std::ranges::find_if(compile_check_cases,[&](const auto& item){
        const std::string_view file=item.filename;
        return identity==item.identity&&diagnostic==item.diagnostic
            &&original.id.ends_with("__"+std::string(file.substr(7,file.size()-12)));
    });
    const auto before=compile_check_test_fingerprint(block);
    if(plan==compile_check_cases.end()||before!=plan->before)
        throw std::runtime_error("QA diagnostic adoption requires its exact original owner/source fingerprint");
    JsonObject check;
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
