#pragma once
// QA only: reviewed proposals are built in memory. Never write curriculum assets.
#include "core/lesson.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "platform/windows/content_loader.hpp"
#include <algorithm>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::test {
struct ChoiceRevisionCase {
    const char* filename;
    const char* identity;
    std::size_t option;
    const char* replacement;
    const char* feedback;
    const char* before_full;
    const char* after_full;
    const char* explanation=nullptr;
    bool allow_unexpanded_source=false;
};
inline const std::vector<ChoiceRevisionCase>& choice_revision_cases() {
    static const std::vector<ChoiceRevisionCase> cases{
        {"lesson_estados_del_flujo_y_datos_validos.json","block_0006_0004",2,
            "La entrada necesita siempre un espacio después de 10 para que la última extracción sea válida.",
            "Revisa qué garantiza el éxito de la extracción y si detectar el final durante esa misma operación invalida necesariamente el dato obtenido.",
            "ccba0a5461601602d2c74520a20b3b800156e23c4156c6f9edb613908d4c3a5f","0853764d61cd61df3f1ab10f310ff237a8ee4b057151d5c37e99490963589d3d"},
        {"lesson_validar_una_operacion_y_recuperar_la_siguiente.json","block_0005_0004",2,
            "Porque el bucle vuelve a construir el flujo exterior en cada vuelta.",
            "Localiza dónde se construye origen y dónde se crea el flujo local de análisis. Distingue qué objeto se reutiliza en el bucle y cuál se crea para una sola línea.",
            "c694da695d8cb9b889ece1b22570c0e25cfcb37c004a3b22d71f3862a61d373f","5dfe0ec554c75abe71df341c5fda4a0c68fa41747bb2d9bbefb753ef0f636713"},
        {"lesson_construir_un_lector_de_entrada_preparada.json","block_0000_0004",0,
            "El campo Entrada, manteniendo el código del lector.",nullptr,
            "1588a86d210ef1c95b08fee6120684afc51caa303e932519d401b13981eb65dc","35964088aa38c6231c8febb7b1a00da3a8389e4f1605112af426d03cde09d360"},
        {"lesson_tokens_con_categoria_y_valor.json","block_0003_0002",2,
            "Un token numérico de valor 1; el segundo dígito queda descartado al devolver el primero.",
            "Distingue terminar una llamada de consumir el resto del flujo. Sigue desde qué posición empieza la llamada siguiente.",
            "9b28a5432859a50e1d336098022614188ad75f81eb7cb2db87284b98838efca1","4d598fee502105e520f800f975a57486947851153339a448ea92cdff768ac400"},
        {"lesson_tokens_con_categoria_y_valor.json","block_0006_0001",2,
            "Los tres se rechazan al combinar tokens, porque todo error de una expresión es de gramática.",
            "Un orden de unidades permitido puede incumplir las condiciones de una operación. Distingue su forma de los valores con los que se intenta calcular.",
            "8f36e0a7b384b20953b45f76011c01c1fab1766eb641ff11ce7f4c94c62e5a91","123f9105afc6bd9cab76c21cde2af2d83b4004a89ec91e8593573ae8fe4dd4e1"},
        {"lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json","block_0007_0001",1,
            "Lanzar una excepción de recuperación y terminar ese recorrido.",nullptr,
            "1a4a51d3a8dcc2c9ace27e6d9422d8fff1d2fe5acb7e7da49eed6b4efe6a52f6","571e7f17ffd3bf84f8a15c500503af9cfeceed04f8e6a23738c9ed2856a4f1ba",nullptr,true},
        {"lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json","block_0003_0004",0,
            "Para conservar ese token inicial, que la gramática todavía debe consumir.",nullptr,
            "a2b0f46e6254179eaf9d67047e4c5c6c1dd7acd1807e260a923a24bc44accc9a","cb07aff65f16ac14875e50631d73cd447262afbc2a033cafa9bf5958fefcc8a2",
            "El ciclo leyó un token para distinguir el final del texto de la presencia de datos por analizar. Lo devuelve para que la gramática pueda consumirlo al interpretar la operación.",true}
    };
    return cases;
}
inline const ChoiceRevisionCase& choice_revision_case(const curriculum::Lesson& lesson,std::string_view identity) {
    const auto& cases=choice_revision_cases();
    const auto entry=std::ranges::find_if(cases,[&](const auto& item){const std::string_view file=item.filename;
        return item.identity==identity&&lesson.id.ends_with("__"+std::string(file.substr(7,file.size()-12)));});
    if(entry==cases.end())throw std::runtime_error("Unknown QA choice revision identity/lesson");
    return *entry;
}
// Reconstruct only the QA state before this one reviewed option change. Other
// integrated changes stay active; this is not a past publication of the lesson.
// Both live and archived blocks have independent immutable full-payload pins.
inline curriculum::Lesson choice_revision_before(const curriculum::Lesson& integrated,std::string_view identity) {
    using namespace winrt::Windows::Data::Json;
    const auto& entry=choice_revision_case(integrated,identity);
    const auto validated=windows::parse_lesson_json(integrated.raw_json,integrated.locale);
    const auto position=curriculum::stable_block_position(validated,identity);
    if(!position)throw std::runtime_error("Missing integrated QA choice identity");
    const auto root=JsonObject::Parse(winrt::to_hstring(validated.raw_json)),layout=root.GetNamedObject(L"stateLayout");
    const auto blocks=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks");
    const auto current=blocks.GetObjectAt(static_cast<unsigned>(position->block));
    if(compile_check_test_fingerprint(current)==entry.before_full) {
        if(!entry.allow_unexpanded_source||validated.pages[position->page].blocks[position->block].choice_revision)
            throw std::runtime_error("An already integrated choice QA asset lost its published revision");
        if(layout.HasKey(L"choiceRevisions"))for(const auto& value:layout.GetNamedArray(L"choiceRevisions"))
            if(winrt::to_string(value.GetObject().GetNamedString(L"id"))==identity)
                throw std::runtime_error("An unrevised QA choice retained its archive");
        auto result=validated;result.source_path=integrated.source_path;return result;
    }
    if(compile_check_test_fingerprint(current)!=entry.after_full)
        throw std::runtime_error("The integrated choice differs from its independently pinned current payload");
    const auto& active=validated.pages[position->page].blocks[position->block];
    if(!active.choice_revision||active.choice_revision->sha256!=entry.before_full)
        throw std::runtime_error("The integrated choice has no authenticated complete original");
    JsonArray retained;JsonObject archive;unsigned records=0;
    for(const auto& value:layout.GetNamedArray(L"choiceRevisions")){
        const auto record=value.GetObject();
        if(winrt::to_string(record.GetNamedString(L"id"))!=identity){retained.Append(value);continue;}
        archive=record.GetNamedObject(L"block");++records;
    }
    if(records!=1||compile_check_test_fingerprint(archive)!=entry.before_full)
        throw std::runtime_error("Choice QA requires exactly one complete pinned original");
    JsonArray proofs;unsigned removed=0;
    for(const auto& value:layout.GetNamedArray(L"editorial")){
        const auto proof=value.GetObject();
        if(winrt::to_string(proof.GetNamedString(L"id"))==identity
            &&winrt::to_string(proof.GetNamedString(L"beforeSha256"))==entry.before_full
            &&winrt::to_string(proof.GetNamedString(L"afterSha256"))==entry.after_full){++removed;continue;}
        proofs.Append(value);
    }
    if(removed!=1)throw std::runtime_error("Choice QA requires its exact terminal editorial transition");
    blocks.SetAt(static_cast<unsigned>(position->block),archive);layout.Insert(L"editorial",proofs);
    if(retained.Size())layout.Insert(L"choiceRevisions",retained);else layout.Remove(L"choiceRevisions");
    auto result=windows::parse_lesson_json(winrt::to_string(root.Stringify()),integrated.locale);
    result.source_path=integrated.source_path;
    const auto& previous=result.pages[position->page].blocks[position->block];
    if(previous.choice_revision||compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(previous.raw_json)))!=entry.before_full)
        throw std::runtime_error("Choice QA failed to restore the distinct authenticated previous state");
    return result;
}
inline std::string choice_revision_json(const curriculum::Lesson& original,std::string_view identity) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,identity);
    const auto& reviewed=choice_revision_case(original,identity);const auto* entry=&reviewed;
    if(!position)throw std::runtime_error("Unknown QA choice revision");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto layout=root.GetNamedObject(L"stateLayout");
    const auto block=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
    if(original.pages[position->page].blocks[position->block].choice_revision
        ||compile_check_test_fingerprint(block)!=entry->before_full)
        throw std::runtime_error("Choice QA transition requires its pinned unrevised source, never an integrated no-op");
    const auto old=JsonObject::Parse(block.Stringify());
    block.GetNamedArray(L"options").SetAt(static_cast<unsigned>(entry->option),JsonValue::CreateStringValue(winrt::to_hstring(entry->replacement)));
    if(entry->feedback)block.GetNamedArray(L"reviewFeedback").SetAt(static_cast<unsigned>(entry->option),JsonValue::CreateStringValue(winrt::to_hstring(entry->feedback)));
    if(entry->explanation)block.Insert(L"explanation",JsonValue::CreateStringValue(winrt::to_hstring(entry->explanation)));
    if(compile_check_test_fingerprint(block)!=entry->after_full)
        throw std::runtime_error("Choice QA proposal differs from the independently pinned integrated payload");
    const auto records=layout.HasKey(L"choiceRevisions")?layout.GetNamedArray(L"choiceRevisions"):JsonArray{};
    JsonObject record;record.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));record.Insert(L"block",old);records.Append(record);
    layout.Insert(L"choiceRevisions",records);
    JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(entry->before_full)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(entry->after_full)));layout.GetNamedArray(L"editorial").Append(proof);
    return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson revise_choice_lesson(const curriculum::Lesson& original,std::string_view identity) {
    auto lesson=windows::parse_lesson_json(choice_revision_json(original,identity),original.locale);
    lesson.source_path=original.source_path;return lesson;
}
} // namespace lgaed::test
