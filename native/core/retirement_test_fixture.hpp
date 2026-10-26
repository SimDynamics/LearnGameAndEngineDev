#pragma once
// QA-only application of Work's explicit U1 retirement plan. No asset is written.
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <array>
#include <algorithm>
#include <set>
#include <span>
#include <stdexcept>
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::test {
// Immutable inputs for migration tests, exported from the locally verified
// d1933edf703e19ae0569ae60849b037c48edb687 Git objects by the test-fixture target.
// Integrated-asset tests deliberately load the active curriculum separately.
inline curriculum::Lesson load_pre_retirement_lesson(std::string_view filename) {
    constexpr std::array<std::pair<std::string_view,std::string_view>,3> sources{{
        {"lesson_primer_programa_y_salida.json","72165efd0d74dc0374a9df2e4d020db32d1664ad89c6bd972f8452b8d6c7c71d"},
        {"lesson_texto_secuencia_y_formato.json","026dd83ea21a79a6a7b44107f8be6a3b4838ea7d36f0ecc55d05ddb39d3ed53d"},
        {"lesson_fuente_compilacion_y_diagnostico.json","eacd78eacf76f9ad68984c3d0eaba45b3c989e74bacf138b94292ecb9c37849d"}
    }};
    const auto source=std::ranges::find(sources,filename,[](const auto& item){return item.first;});
    if(source==sources.end())throw std::runtime_error("Unknown pre-retirement QA source");
    std::array<wchar_t,32768> executable{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size())throw std::runtime_error("Cannot resolve pre-retirement QA fixture directory");
    const auto path=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()
        /"test-fixtures/u1-d1933ed"/filename;
    if(state::sha256(state::read_bytes(path))!=source->second)
        throw std::runtime_error("Pre-retirement QA source differs from its pinned d1933ed Git bytes");
    auto result=windows::load_lesson(path);
    result.source_path=std::filesystem::path(build::source_root)
        /"app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion"/filename;
    return result;
}
inline std::string retire_lesson_json(const curriculum::Lesson& source,
        std::span<const std::string> identities, bool required = true) {
    using namespace winrt::Windows::Data::Json;
    auto root = JsonObject::Parse(winrt::to_hstring(source.raw_json));
    auto layout = root.GetNamedObject(L"stateLayout");
    const std::set<std::string> retiring(identities.begin(),identities.end());
    if (retiring.size() != identities.size() || retiring.empty()) throw std::runtime_error("Invalid QA retirement plan");
    JsonArray archive = layout.HasKey(L"retired") ? layout.GetNamedArray(L"retired") : JsonArray{};
    const auto pages = root.GetNamedArray(L"pages"), current = layout.GetNamedArray(L"current");
    JsonArray active_pages, active_current, batch;
    JsonObject snapshot;
    snapshot.Insert(L"current",JsonArray::Parse(current.Stringify()));
    snapshot.Insert(L"pages",JsonArray::Parse(pages.Stringify()));
    snapshot.Insert(L"compilerPuzzlesRequired",JsonValue::CreateBooleanValue(source.compiler_puzzles_required));
    for(const auto field:{L"referenceAfter",L"activityAfter"})
        snapshot.Insert(field,root.HasKey(field)?JsonArray::Parse(root.GetNamedArray(field).Stringify()):JsonArray{});
    std::size_t found = 0;
    for (std::uint32_t p = 0; p < pages.Size(); ++p) {
        auto page = JsonObject::Parse(pages.GetAt(p).Stringify());
        auto record = JsonObject::Parse(current.GetAt(p).Stringify());
        const auto blocks = page.GetNamedArray(L"blocks"), ids = record.GetNamedArray(L"blocks");
        if (blocks.Size() != ids.Size()) throw std::runtime_error("QA identity ownership mismatch");
        JsonArray active_blocks, active_ids;
        for (std::uint32_t b = 0; b < ids.Size(); ++b) {
            const auto id = winrt::to_string(ids.GetAt(b).GetString());
            if (!retiring.contains(id)) {
                active_blocks.Append(blocks.GetAt(b)); active_ids.Append(ids.GetAt(b)); continue;
            }
            ++found;
            std::string successor;
            for (auto n = b + 1; n < ids.Size(); ++n) {
                const auto candidate = winrt::to_string(ids.GetAt(n).GetString());
                if (!retiring.contains(candidate)) { successor = candidate; break; }
            }
            JsonObject archived;
            archived.Insert(L"id",ids.GetAt(b)); archived.Insert(L"page",record.GetNamedValue(L"id"));
            archived.Insert(L"next",JsonValue::CreateStringValue(winrt::to_hstring(successor)));
            archived.Insert(L"block",blocks.GetAt(b)); archive.Append(archived);
            batch.Append(ids.GetAt(b));
        }
        page.Insert(L"blocks",active_blocks); record.Insert(L"blocks",active_ids);
        active_pages.Append(page); active_current.Append(record);
    }
    if (found != retiring.size()) throw std::runtime_error("Unknown QA retirement identity");
    snapshot.Insert(L"retired",batch);
    auto history=layout.HasKey(L"retirementHistory")?layout.GetNamedArray(L"retirementHistory"):JsonArray{};
    history.Append(snapshot);layout.Insert(L"retirementHistory",history);
    root.Insert(L"pages",active_pages); layout.Insert(L"current",active_current); layout.Insert(L"retired",archive);
    root.Insert(L"stateLayout",layout);
    root.Insert(L"compilerPuzzlesRequired",JsonValue::CreateBooleanValue(required));
    // Explicit U1 decisions: the retired original is no longer a prerequisite;
    // all relationships between useful attempts remain byte-value equivalent.
    for (const auto field : {L"activityAfter",L"referenceAfter"}) {
        if (!root.HasKey(field)) continue;
        const auto entries_key = std::wstring_view(field) == L"activityAfter" ? L"requires" : L"blocks";
        JsonArray active_links;
        for (const auto& value : root.GetNamedArray(field)) {
            auto relation = JsonObject::Parse(value.Stringify());
            if (retiring.contains(winrt::to_string(relation.GetNamedString(L"activity")))) continue;
            JsonArray entries;
            for (const auto& entry : relation.GetNamedArray(entries_key))
                if (!retiring.contains(winrt::to_string(entry.GetString()))) entries.Append(entry);
            if (entries.Size()) { relation.Insert(entries_key,entries); active_links.Append(relation); }
        }
        root.Insert(field,active_links);
    }
    return winrt::to_string(root.Stringify());
}
inline curriculum::Lesson retire_lesson(const curriculum::Lesson& source,
        std::span<const std::string> identities, bool required = true) {
    return windows::parse_lesson_json(retire_lesson_json(source,identities,required),source.locale);
}
} // namespace lgaed::test
