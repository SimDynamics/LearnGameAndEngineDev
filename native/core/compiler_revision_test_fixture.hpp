#pragma once
// QA only: proposals are assembled in memory, never written to authored assets.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include <algorithm>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace lgaed::test {
struct CompilerRevisionCase {
    const char* filename;
    const char* identity;
    const char* before_full;
    const char* after_full;
    const char* suffix;
    const char* reference_identity=nullptr;
    const char* reference_before=nullptr;
    const char* reference_after=nullptr;
    const char* calls=nullptr;
};
inline const std::vector<CompilerRevisionCase>& compiler_revision_cases() {
    static const std::vector<CompilerRevisionCase> cases{
        {"lesson_construir_un_lector_de_entrada_preparada.json","block_0003_0005",
            "0ff28cd1a7cb92334d0a2486d5fcd773c7727813c84a082f8908950a6a3399c6","e3d81fc9c5d397dfdd1be6df7271b34cba21d56a696cce43ca5ad1c8e5ae088a",nullptr},
        {"lesson_tokens_con_categoria_y_valor.json","block_0004_0004",
            "a0633caa9e65905eb3216a603cc7bfcb23b766fcad3715756d47c08796f2940e","0800175e415321a20eb95403a67778d11549225a67ba9732cc9358a031138629",
            "Numero: 8\nSigno: -\nNumero: 4\nSigno: *\nSigno: (\nNumero: 2\nSigno: )\nSigno: /\nNumero: 1\nSigno: ;\nFin\n",
            "block_0007_0001","ddd301c6c0a3c53e70ac18b616b2a3a1aae584590cf7b9e632d550088f6bde5a","4d2480ce9aa48f34eb3b1777ab5d0ad88524436faa22cf666a0abb3c010edb46","    probar(\"8-4*(2)/1;\");\n"},
        {"lesson_numeros_de_varios_digitos_y_limites.json","block_0004_0003",
            "c28f8e645fd47269c44c418648e69b765ff614441a5cb4c2e3723e6ac29c7e10","27698af9c281a19311b4c1c070cc49a86bf68223d85175aeff0bae4aaffc3d7a",
            "Numero: 7\nRepetido: 7\nSigno: +\nSiguiente: 2\n",
            "block_0008_0001","1b9b8dc7b0973c441205dcff6182317983b4dfa6865f15738fa20d61bc165055","10f38c37b8a211edd53a6a9bb059296360bd2c2fe5c11d865a8b48d29d8b5c07","    trazar(\"0007+2;\");\n"},
        {"lesson_una_gramatica_para_sumar_y_restar.json","block_0004_0003",
            "3e793a0dcd1ed13fd57e97c68f4a22f960585f9acf8611c7aa09927a5c8404f2","55b38dc4d94d9cea6ed446040c7e043dde447be7a18ead7735df182de91cee01",
            "Resultado: 100\nResultado: -100\nError: Resultado fuera de dominio\nError: Resultado fuera de dominio\n",
            "block_0008_0001","05a7b84c5fe6eecd11e75fb9548d9051cc7575dbe421894fdb9ef3cab207256d","eee52db854d07ecc2e3319731c18c07940ed543f9836b722ba85ea5e790cc5e4",
            "    probar(\"99+1;\");\n    probar(\"0-99-1;\");\n    probar(\"99+2-2;\");\n    probar(\"0-99-2+2;\");\n"}
    };return cases;
}
inline std::string compiler_revision_json(const curriculum::Lesson& original,std::string_view identity) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,identity);const auto& cases=compiler_revision_cases();
    const auto entry=std::ranges::find_if(cases,[&](const auto& value){const std::string_view file=value.filename;
        return value.identity==identity&&original.id.ends_with("__"+std::string(file.substr(7,file.size()-12)));});
    if(!position||entry==cases.end())throw std::runtime_error("Unknown QA compiler revision");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json)),layout=root.GetNamedObject(L"stateLayout");
    const auto block=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
    const auto old=JsonObject::Parse(block.Stringify());const auto outputs=block.GetNamedObject(L"outputCheck").GetNamedArray(L"cases");
    if(entry->suffix) {
        const auto expected=outputs.GetAt(0).GetObject().GetNamedArray(L"outputs");
        expected.SetAt(0,JsonValue::CreateStringValue(winrt::to_hstring(winrt::to_string(expected.GetAt(0).GetString())+entry->suffix)));
    }else for(const auto& input:{std::string("4:0 aviso"),std::string("4;0 aviso")}) {
        JsonObject item;item.Insert(L"standardInput",JsonValue::CreateStringValue(winrt::to_hstring(input)));JsonArray expected;
        expected.Append(JsonValue::CreateStringValue(winrt::to_hstring(std::string(input[1]==':'?"Datos adicionales":"Formato invalido")+" | Ocupadas: 3\nAceptadas: 0\nOcupadas: 3\n")));
        item.Insert(L"outputs",expected);outputs.Append(item);
    }
    const auto records=layout.HasKey(L"compilerRevisions")?layout.GetNamedArray(L"compilerRevisions"):JsonArray{};
    JsonObject record;record.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));record.Insert(L"block",old);
    if(entry->reference_identity) {
        const auto reference_position=curriculum::stable_block_position(original,entry->reference_identity);
        if(!reference_position)throw std::runtime_error("Missing original QA compiler reference");
        const auto reference=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(reference_position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(reference_position->block)).GetObject();
        const auto original_reference=JsonObject::Parse(reference.Stringify());auto code=winrt::to_string(reference.GetNamedString(L"code"));
        const auto insertion=code.rfind("    return 0;\n}\n");if(insertion==std::string::npos)throw std::runtime_error("Missing final QA main return");
        code.insert(insertion,entry->calls);reference.Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(code)));
        JsonArray archives;JsonObject archive;archive.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(entry->reference_identity)));archive.Insert(L"block",original_reference);archives.Append(archive);record.Insert(L"references",archives);
        JsonObject reference_proof;reference_proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(entry->reference_identity)));
        reference_proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(entry->reference_before)));
        reference_proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(entry->reference_after)));layout.GetNamedArray(L"editorial").Append(reference_proof);
    }
    records.Append(record);layout.Insert(L"compilerRevisions",records);
    JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(entry->before_full)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(entry->after_full)));layout.GetNamedArray(L"editorial").Append(proof);
    return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson revise_compiler_lesson(const curriculum::Lesson& original,std::string_view identity) {
    auto lesson=windows::parse_lesson_json(compiler_revision_json(original,identity),original.locale);lesson.source_path=original.source_path;return lesson;
}
}
