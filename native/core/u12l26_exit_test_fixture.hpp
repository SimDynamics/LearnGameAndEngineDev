#pragma once

// QA only: exact immutable Git source plus one in-memory reviewed objective.
// This fixture does not grant execution credit or change active curriculum.
#include "core/compile_check_test_fixture.hpp"

namespace lgaed::test {
inline constexpr std::string_view u12l26_exit_filename="lesson_construir_un_programa_de_carga_y_guardado.json";
inline constexpr std::string_view u12l26_exit_lesson_id=
    "cpp_fundamentals__archivos_y_programa_completo__construir_un_programa_de_carga_y_guardado";
inline constexpr std::string_view u12l26_exit_editor_id="block_0004_0017";
inline constexpr std::string_view u12l26_exit_before=
    "6f796c5c55683ac266ae3f83c54f935273c341c34da087161276c7e361524ae8";
inline constexpr std::string_view u12l26_exit_explanation=
    "La muestra se crea como un archivo normal. Usarlo como carpeta al abrir la salida falla: "
    "se muestra Error al abrir salida, sin salto final, y el programa termina con código 1.";

inline curriculum::Lesson load_pre_u12l26_exit_lesson() {
    std::array<wchar_t,32768> executable{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size())throw std::runtime_error("Cannot resolve historical U12L26 exit fixture directory");
    const auto path=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()
        /"test-fixtures/exit-l26-e8389c8"/u12l26_exit_filename;
    const auto bytes=state::read_bytes(path);
    if(bytes.size()!=195186||state::sha256(bytes)!="6b6763a94875c720040322cfe259b7bf1693a743b042ffa30c1a24983e34fd67")
        throw std::runtime_error("Historical U12L26 fixture differs from exact e8389c86c3cd3b572cb77c6308103163a1e2cf7d Git bytes");
    auto result=windows::load_lesson(path);
    result.source_path=std::filesystem::path(build::source_root)
        /"app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo"/u12l26_exit_filename;
    return result;
}
inline std::string u12l26_intentional_exit_json(const curriculum::Lesson& original) {
    using namespace winrt::Windows::Data::Json;
    const auto position=curriculum::stable_block_position(original,u12l26_exit_editor_id);
    if(original.id!=u12l26_exit_lesson_id||original.locale!="es"||original.state_layout.version!=2||!position)
        throw std::runtime_error("U12L26 exit QA requires its exact Spanish v2 owner");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));
    if(block.HasKey(L"outputCheck")||compile_check_test_fingerprint(block)!=u12l26_exit_before)
        throw std::runtime_error("U12L26 exit QA requires its complete unchanged exploratory editor");
    JsonObject check;
    check.Insert(L"cases",JsonArray::Parse(LR"json([{"standardInput":"","outputs":["Error al abrir salida"],"exitCode":1,"explanation":"pending"}])json"));
    check.GetNamedArray(L"cases").GetObjectAt(0).Insert(L"explanation",
        JsonValue::CreateStringValue(winrt::to_hstring(u12l26_exit_explanation)));
    block.Insert(L"outputCheck",check);
    JsonObject proof;
    proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(u12l26_exit_editor_id)));
    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(u12l26_exit_before)));
    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(compile_check_test_fingerprint(block))));
    root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(proof);
    return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson with_u12l26_intentional_exit(const curriculum::Lesson& original) {
    auto result=windows::parse_lesson_json(u12l26_intentional_exit_json(original),original.locale);
    result.source_path=original.source_path;return result;
}
} // namespace lgaed::test
