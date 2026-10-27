#pragma once
// QA only: reviewed proposals are built in memory. Never write curriculum assets.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include <algorithm>
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
            "8f36e0a7b384b20953b45f76011c01c1fab1766eb641ff11ce7f4c94c62e5a91","123f9105afc6bd9cab76c21cde2af2d83b4004a89ec91e8593573ae8fe4dd4e1"}
    };
    return cases;
}
inline std::string choice_revision_json(const curriculum::Lesson& original,std::string_view identity) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,identity);
    const auto& cases=choice_revision_cases();
    const auto entry=std::ranges::find_if(cases,[&](const auto& item){return item.identity==identity;});
    if(!position||entry==cases.end())throw std::runtime_error("Unknown QA choice revision");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto layout=root.GetNamedObject(L"stateLayout");
    const auto block=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
    const auto old=JsonObject::Parse(block.Stringify());
    block.GetNamedArray(L"options").SetAt(static_cast<unsigned>(entry->option),JsonValue::CreateStringValue(winrt::to_hstring(entry->replacement)));
    if(entry->feedback)block.GetNamedArray(L"reviewFeedback").SetAt(static_cast<unsigned>(entry->option),JsonValue::CreateStringValue(winrt::to_hstring(entry->feedback)));
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
