#pragma once

// QA only. Adopt the exact historical U12L13 exploratory editor in memory;
// preserve the authored program, every layout owner and all published history.
#include "core/compile_check_test_fixture.hpp"

namespace lgaed::test {
inline constexpr std::string_view intentional_exit_filename="lesson_compartir_flujos_sin_acoplar_el_calculo.json";
// Exact Git blob, without checkout newline conversion. Never reconstruct the
// exploratory document by removing a goal from today's integrated curriculum.
inline curriculum::Lesson load_pre_intentional_exit_lesson() {
    std::array<wchar_t,32768> executable{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size())throw std::runtime_error("Cannot resolve historical intentional-exit fixture directory");
    const auto path=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()
        /"test-fixtures/exit-585cef9"/intentional_exit_filename;
    const auto bytes=state::read_bytes(path);
    if(bytes.size()!=59848||state::sha256(bytes)!="09e6e7922ca9504bb0f485189d154c66d666e1e4c0c91fb0b7c4783fc0089488")
        throw std::runtime_error("Historical intentional-exit source differs from pinned 585cef9ff64dcc1bfbe37457ac6a59074650f978 Git bytes");
    auto result=windows::load_lesson(path);
    result.source_path=std::filesystem::path(build::source_root)
        /"app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo"/intentional_exit_filename;
    return result;
}
inline constexpr std::string_view intentional_exit_lesson_id=
    "cpp_fundamentals__archivos_y_programa_completo__compartir_flujos_sin_acoplar_el_calculo";
inline constexpr std::string_view intentional_exit_editor_id="block_0005_0002";
inline constexpr std::string_view intentional_exit_editor_before=
    "d263ac4c841168cb7723bdc2daaeaf6ddc839c4830f17a26f6166ff919fb252c";
inline constexpr std::string_view intentional_exit_explanation=
    "La segunda extracción falla. Los valores candidatos no se publican: se conservan 9 y 4, "
    "se muestra Entrada invalida|9:4 y el programa termina con código 1. Esto no restaura "
    "los caracteres consumidos ni el estado del flujo.";

inline std::string intentional_exit_json(const curriculum::Lesson& original) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,intentional_exit_editor_id);
    if(original.id!=intentional_exit_lesson_id||original.locale!="es"||original.state_layout.version!=2||!position)
        throw std::runtime_error("The intentional-exit QA fixture requires its exact Spanish v2 owner");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));
    if(block.HasKey(L"outputCheck")||compile_check_test_fingerprint(block)!=intentional_exit_editor_before)
        throw std::runtime_error("The intentional-exit QA fixture requires its complete unchanged exploratory editor");
    JsonObject check;
    check.Insert(L"cases",JsonArray::Parse(LR"json([{"standardInput":"","outputs":["Entrada invalida|9:4"],"exitCode":1,"explanation":"pending"}])json"));
    check.GetNamedArray(L"cases").GetObjectAt(0).Insert(L"explanation",
        JsonValue::CreateStringValue(winrt::to_hstring(intentional_exit_explanation)));
    block.Insert(L"outputCheck",check);
    JsonObject proof;
    proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(intentional_exit_editor_id)));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(intentional_exit_editor_before)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(compile_check_test_fingerprint(block))));
    root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(proof);
    return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson with_intentional_exit(const curriculum::Lesson& original) {
    auto result=windows::parse_lesson_json(intentional_exit_json(original),original.locale);
    result.source_path=original.source_path;return result;
}
} // namespace lgaed::test
