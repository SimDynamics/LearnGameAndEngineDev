#pragma once

#include "core/compile_check_test_fixture.hpp"

namespace lgaed::test {
struct U6DiagnosticCase {std::string_view file,identity,diagnostic;bool link=false;};
inline constexpr std::array<U6DiagnosticCase,12> u6_diagnostic_cases{{
    {"lesson_declaraciones_y_firmas.json","block_0014_0002","u6-difference-unselected-definition",true},
    {"lesson_declaraciones_y_firmas.json","block_0015_0002","u6-difference-conflicting-return"},
    {"lesson_declaraciones_y_firmas.json","block_0017_0002","u6-difference-missing-header"},
    {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","block_0016_0002","u6-envelopes-missing-header"},
    {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","block_0017_0002","u6-envelopes-missing-declaration"},
    {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","block_0018_0002","u6-envelopes-unselected-definitions",true},
    {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","block_0019_0002","u6-envelopes-duplicate-definition",true},
    {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","block_0021_0002","u6-series-unknown-call"},
    {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","block_0022_0002","u6-series-unselected-definitions",true},
    {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","block_0023_0002","u6-series-duplicate-definition",true},
    {"lesson_construir_un_proyecto_desde_su_interfaz.json","block_0013_0002","u6-shelf-unselected-definition",true},
    {"lesson_construir_un_proyecto_desde_su_interfaz.json","block_0014_0002","u6-shelf-conflicting-return"}
}};
inline curriculum::Lesson load_pre_u6_diagnostic_lesson(std::string_view filename) {
    constexpr std::array<std::pair<std::string_view,std::string_view>,3> pins{{
        {"lesson_declaraciones_y_firmas.json","b5c659ef75033d6fbed9ce5bc42a640b89ce3d33a8f063aaf32292d2ea9a38b7"},
        {"lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json","d250c29f303f8523ad74099086489565dce69699978ad57850a333439e31f595"},
        {"lesson_construir_un_proyecto_desde_su_interfaz.json","64b9e31d061e8d4e04e093c608b5be41a3c55367b6591028592ac82661c2bc73"}
    }};
    const auto pin=std::ranges::find(pins,filename,[](const auto& item){return item.first;});
    if(pin==pins.end())throw std::runtime_error("Unknown immutable U6 diagnostic lesson");
    std::array<wchar_t,32768> executable{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size())throw std::runtime_error("Cannot resolve the U6 diagnostic fixture tree");
    const auto path=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()
        /"test-fixtures/u6-diagnostics-e8389c8"/filename;
    if(state::sha256(state::read_bytes(path))!=pin->second)throw std::runtime_error("The U6 diagnostic source differs from pinned e8389c8 Git bytes");
    auto result=windows::load_lesson(path);
    result.source_path=std::filesystem::path(build::source_root)
        /"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos"/filename;
    return result;
}
inline curriculum::Lesson with_u6_diagnostic(const curriculum::Lesson& original,const U6DiagnosticCase& spec) {
    using namespace winrt::Windows::Data::Json;
    const auto location=curriculum::stable_block_position(original,spec.identity);
    if(!location)throw std::runtime_error("The published U6 editor is absent");
    const auto root=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(location->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(location->block));
    if(block.HasKey(L"compileCheck")||block.HasKey(L"outputCheck"))throw std::runtime_error("The U6 adoption fixture must precede its objective");
    const auto before=compile_check_test_fingerprint(block);
    JsonObject check;check.Insert(L"diagnostic",JsonValue::CreateStringValue(winrt::to_hstring(spec.diagnostic)));
    check.Insert(L"explanation",JsonValue::CreateStringValue(L"El diagnóstico corresponde a la etapa y a la causa solicitadas. El programa no se ejecutó."));
    block.Insert(L"compileCheck",check);
    JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(spec.identity)));
    edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(before)));
    edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(compile_check_test_fingerprint(block))));
    root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(edge);
    auto result=windows::parse_lesson_json(winrt::to_string(root.Stringify()),original.locale);
    result.source_path=original.source_path;return result;
}
} // namespace lgaed::test
