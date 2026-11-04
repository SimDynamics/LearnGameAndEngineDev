#pragma once
// QA only: proposals are assembled in memory, never written to authored assets.
#include "core/lesson.hpp"
#include "core/compile_check_test_fixture.hpp"
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
    bool allow_unexpanded_source=false;
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
            "    probar(\"99+1;\");\n    probar(\"0-99-1;\");\n    probar(\"99+2-2;\");\n    probar(\"0-99-2+2;\");\n"},
        {"lesson_terminos_prioridad_y_division_segura.json","block_0004_0003",
            "9bddda66a2a7024ef1280fc82a82e7ad59cd189a2b3089138b5f06c06de78f1d","dcd52cc803827c671d2d783d6af8e423e79cb8ef7377a365ff5afbcec05a926e",
            "Error: Resultado fuera de dominio\n",
            "block_0008_0001","44f26849307e799bc3263578b3353b4979218eae494c39a906c2fe09bafb2cb5","a396321ffa32d8e9c4ca4cc1df1f01cf8e8acef762295d2ec6a8e8077460074a",
            "    probar(\"11*10/2;\");\n",true},
        {"lesson_signos_unarios_parentesis_y_progreso_recursivo.json","block_0004_0004",
            "ffef99d8c0504d333a81d68da83eb473974d43ba573626a4808eab6015bddd30","efcd239dc6e8f02d04a418f2b81e0c18e54e8125c7dc0c6dcac57e45abc57300",
            "Error: Resultado fuera de dominio\nError: Anidamiento excesivo\n",
            "block_0009_0001","70662e3574fbc6db855250d8a9579466ef5827e0ea768477076290e6191b577b","44dc46d8aace0591bb180e11022c9a3b171c20e4b792fdc91da672a6618c0a44",
            "    probar(\"11*10/2;\");\n    probar(\"(((((((((1)))))))));\");\n",true},
        {"lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json","block_0004_0004",
            "dc68860e953dd941d4495f68d295b6a7cbca70fe286ee356675f405bb8bd2502","cf3c9ab1f070195689b4f2bce4bd635ae8368f760384663a2ebecbb5a1f7bfdd",
            "Caso 11\nError: Caracter no admitido\nResultado: 4\n",
            "block_0008_0001","61c61981819c377ca9bc8194d889e9f8abfff6d1b572da2ac96a74b8658aa7e6","6a9952bd00bb00a74a3b51fba27757940cadcb72708a0ea3af5e8447660f18d6",
            "    std::cout << \"Caso 11\\n\";\n    probar(\"?z;4;\");\n",true}
    };return cases;
}
inline const CompilerRevisionCase& compiler_revision_case(const curriculum::Lesson& lesson,std::string_view identity) {
    const auto& cases=compiler_revision_cases();
    const auto entry=std::ranges::find_if(cases,[&](const auto& value){const std::string_view file=value.filename;
        return value.identity==identity&&lesson.id.ends_with("__"+std::string(file.substr(7,file.size()-12)));});
    if(entry==cases.end())throw std::runtime_error("Unknown QA compiler revision identity/lesson");
    return *entry;
}
// QA projection immediately before this compiler's reviewed expansion. Complete
// archived blocks and references are authenticated independently of live data;
// unrelated integrated changes are intentionally retained, not dated backwards.
// A not-yet-integrated proposal starts only from the independently pinned full
// original editor/reference, with no existing revision archive for that owner.
inline curriculum::Lesson compiler_revision_before(const curriculum::Lesson& integrated,std::string_view identity) {
    using namespace winrt::Windows::Data::Json;
    const auto& entry=compiler_revision_case(integrated,identity);
    const auto validated=windows::parse_lesson_json(integrated.raw_json,integrated.locale);
    const auto position=curriculum::stable_block_position(validated,identity);
    if(!position)throw std::runtime_error("Missing integrated QA compiler identity");
    const auto root=JsonObject::Parse(winrt::to_hstring(validated.raw_json)),layout=root.GetNamedObject(L"stateLayout");
    const auto blocks=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks");
    if(compile_check_test_fingerprint(blocks.GetObjectAt(static_cast<unsigned>(position->block)))==entry.before_full) {
        if(!entry.allow_unexpanded_source)
            throw std::runtime_error("An already integrated compiler QA asset lost its published expansion");
        if(validated.pages[position->page].blocks[position->block].compiler_revision)
            throw std::runtime_error("An unexpanded QA source cannot already own a compiler revision");
        if(layout.HasKey(L"compilerRevisions"))for(const auto& value:layout.GetNamedArray(L"compilerRevisions"))
            if(winrt::to_string(value.GetObject().GetNamedString(L"id"))==identity)
                throw std::runtime_error("An unexpanded QA source retained its revision archive");
        if(entry.reference_identity) {
            const auto reference=curriculum::stable_block_position(validated,entry.reference_identity);
            if(!reference||compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(
                validated.pages[reference->page].blocks[reference->block].raw_json)))!=entry.reference_before)
                throw std::runtime_error("An unexpanded QA source changed its complete original reference");
        }
        auto result=validated;result.source_path=integrated.source_path;return result;
    }
    if(compile_check_test_fingerprint(blocks.GetObjectAt(static_cast<unsigned>(position->block)))!=entry.after_full)
        throw std::runtime_error("The integrated compiler differs from its independently pinned current payload");
    const auto& active=validated.pages[position->page].blocks[position->block];
    if(!active.compiler_revision||active.compiler_revision->sha256!=entry.before_full)
        throw std::runtime_error("The integrated compiler has no authenticated complete original");
    JsonArray retained;JsonObject record;unsigned records=0;
    for(const auto& value:layout.GetNamedArray(L"compilerRevisions")){
        const auto candidate=value.GetObject();
        if(winrt::to_string(candidate.GetNamedString(L"id"))!=identity){retained.Append(value);continue;}
        record=candidate;++records;
    }
    if(records!=1||compile_check_test_fingerprint(record.GetNamedObject(L"block"))!=entry.before_full)
        throw std::runtime_error("Compiler QA requires exactly one complete pinned original");
    const auto remove_proof=[&](std::string_view id,std::string_view before,std::string_view after){
        JsonArray proofs;unsigned removed=0;
        for(const auto& value:layout.GetNamedArray(L"editorial")){
            const auto proof=value.GetObject();
            if(winrt::to_string(proof.GetNamedString(L"id"))==id
                &&winrt::to_string(proof.GetNamedString(L"beforeSha256"))==before
                &&winrt::to_string(proof.GetNamedString(L"afterSha256"))==after){++removed;continue;}
            proofs.Append(value);
        }
        if(removed!=1)throw std::runtime_error("Compiler QA requires its exact terminal editorial transition");
        layout.Insert(L"editorial",proofs);
    };
    if(entry.reference_identity){
        const auto at=curriculum::stable_block_position(validated,entry.reference_identity);
        if(!at||active.compiler_revision->references.size()!=1||!record.HasKey(L"references")
            ||record.GetNamedArray(L"references").Size()!=1)
            throw std::runtime_error("Compiler QA lost its complete reviewed reference archive");
        const auto archive=record.GetNamedArray(L"references").GetObjectAt(0);
        const auto reference_blocks=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page)).GetNamedArray(L"blocks");
        if(winrt::to_string(archive.GetNamedString(L"id"))!=entry.reference_identity
            ||compile_check_test_fingerprint(archive.GetNamedObject(L"block"))!=entry.reference_before
            ||compile_check_test_fingerprint(reference_blocks.GetObjectAt(static_cast<unsigned>(at->block)))!=entry.reference_after)
            throw std::runtime_error("Compiler QA reference differs from its independent before/after pins");
        reference_blocks.SetAt(static_cast<unsigned>(at->block),archive.GetNamedObject(L"block"));
        remove_proof(entry.reference_identity,entry.reference_before,entry.reference_after);
    }else if(!active.compiler_revision->references.empty()||record.HasKey(L"references"))
        throw std::runtime_error("Compiler QA contains an unreviewed reference archive");
    blocks.SetAt(static_cast<unsigned>(position->block),record.GetNamedObject(L"block"));
    remove_proof(identity,entry.before_full,entry.after_full);
    if(retained.Size())layout.Insert(L"compilerRevisions",retained);else layout.Remove(L"compilerRevisions");
    auto result=windows::parse_lesson_json(winrt::to_string(root.Stringify()),integrated.locale);
    result.source_path=integrated.source_path;
    const auto& previous=result.pages[position->page].blocks[position->block];
    if(previous.compiler_revision||compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(previous.raw_json)))!=entry.before_full)
        throw std::runtime_error("Compiler QA failed to restore the distinct authenticated previous state");
    return result;
}
inline std::string compiler_revision_json(const curriculum::Lesson& original,std::string_view identity) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,identity);
    const auto& reviewed=compiler_revision_case(original,identity);const auto* entry=&reviewed;
    if(!position)throw std::runtime_error("Unknown QA compiler revision");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json)),layout=root.GetNamedObject(L"stateLayout");
    const auto block=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
    if(original.pages[position->page].blocks[position->block].compiler_revision
        ||compile_check_test_fingerprint(block)!=entry->before_full)
        throw std::runtime_error("Compiler QA transition requires its pinned unexpanded source, never an integrated no-op");
    const auto old=JsonObject::Parse(block.Stringify());const auto outputs=block.GetNamedObject(L"outputCheck").GetNamedArray(L"cases");
    if(entry->suffix) {
        const auto expected=outputs.GetAt(0).GetObject().GetNamedArray(L"outputs");
        expected.SetAt(0,JsonValue::CreateStringValue(winrt::to_hstring(winrt::to_string(expected.GetAt(0).GetString())+entry->suffix)));
    }else for(const auto& input:{std::string("4:0 aviso"),std::string("4;0 aviso")}) {
        JsonObject item;item.Insert(L"standardInput",JsonValue::CreateStringValue(winrt::to_hstring(input)));JsonArray expected;
        expected.Append(JsonValue::CreateStringValue(winrt::to_hstring(std::string(input[1]==':'?"Datos adicionales":"Formato invalido")+" | Ocupadas: 3\nAceptadas: 0\nOcupadas: 3\n")));
        item.Insert(L"outputs",expected);outputs.Append(item);
    }
    if(compile_check_test_fingerprint(block)!=entry->after_full)
        throw std::runtime_error("Compiler QA proposal differs from the independently pinned integrated payload");
    const auto records=layout.HasKey(L"compilerRevisions")?layout.GetNamedArray(L"compilerRevisions"):JsonArray{};
    JsonObject record;record.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));record.Insert(L"block",old);
    if(entry->reference_identity) {
        const auto reference_position=curriculum::stable_block_position(original,entry->reference_identity);
        if(!reference_position)throw std::runtime_error("Missing original QA compiler reference");
        const auto reference=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(reference_position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(reference_position->block)).GetObject();
        if(compile_check_test_fingerprint(reference)!=entry->reference_before)
            throw std::runtime_error("Compiler QA requires its independently pinned previous reference");
        const auto original_reference=JsonObject::Parse(reference.Stringify());auto code=winrt::to_string(reference.GetNamedString(L"code"));
        const auto insertion=code.rfind("    return 0;\n}\n");if(insertion==std::string::npos)throw std::runtime_error("Missing final QA main return");
        code.insert(insertion,entry->calls);reference.Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(code)));
        if(compile_check_test_fingerprint(reference)!=entry->reference_after)
            throw std::runtime_error("Compiler QA proposal differs from the independently pinned integrated reference");
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
