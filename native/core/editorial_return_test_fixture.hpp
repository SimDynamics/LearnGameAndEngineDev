#pragma once
// QA only. A and intermediate B are authenticated local Git exports. Final C
// is the integrated live asset, independently checked against the B->C return.
#include "core/compile_check_test_fixture.hpp"
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <array>
#include <stdexcept>
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace lgaed::test {
inline constexpr auto editorial_return_intermediate_name="lesson_estado_asignacion_y_copia-df2f9fa.json";
inline constexpr auto editorial_return_original_sha="1f88d26abb2b93108257f62a6e7914744bae8fe874f6664aac2fb7ad31ba8dcb";
inline constexpr auto editorial_return_intermediate_sha="7c3e963deba83299f771924ec2c0d1d06ef1eae3db4763bdffe2457aa47f2c81";
inline curriculum::Lesson editorial_return_historical_lesson(const std::filesystem::path& path,bool intermediate){
    const auto bytes=state::read_bytes(path);
    if(state::sha256(bytes)!=(intermediate?editorial_return_intermediate_sha:editorial_return_original_sha))
        throw std::runtime_error("Editorial return history differs from its independent A/B Git byte pin");
    auto result=windows::parse_lesson_json(std::string(bytes.begin(),bytes.end()),"es");result.source_path=path;return result;
}
struct EditorialReturnCase {const char* identity;const char* before;const char* after;const char* marked;const char* plain;};
inline constexpr std::array<EditorialReturnCase,2> editorial_return_cases{{
    {"block_0006_0001","56aa7fbc312649200f5c8e4f09b9ee892be872d19cb8d378a5e4048b608386b6",
        "be3322fd95ee7ff42a3e52e0092b7da737a878d666133f49cff9cc651f788971","Antes: `5`\nAhora: `8`","Antes: 5\nAhora: 8"},
    {"block_0006_0005","5459af71fdbd2bb10f0e3733cad545e8eeb9538bf7c80186161ac26d0e6ecaa6",
        "81d903df40525897458b6629f1f64cddd3c0ed8743bc22b7c796d6f23b6bc4b9","Antes: `8`\nAhora: `8`","Antes: 8\nAhora: 8"}
}};
inline std::string editorial_return_history_hash(const winrt::Windows::Data::Json::JsonObject& root,
        const winrt::Windows::Data::Json::JsonArray& prefix){
    using namespace winrt::Windows::Data::Json;
    JsonObject frame;frame.Insert(L"lessonId",root.GetNamedValue(L"id"));
    frame.Insert(L"locale",root.GetNamedObject(L"stateLayout").GetNamedValue(L"locale"));
    frame.Insert(L"editorial",prefix);return compile_check_test_fingerprint(frame);
}
inline std::string editorial_return_json(const curriculum::Lesson& intermediate){
    using namespace winrt::Windows::Data::Json;
    if(intermediate.id!="cpp_fundamentals__objetos_y_estado__estado_asignacion_y_copia"||intermediate.locale!="es")
        throw std::runtime_error("Wrong lesson/locale for the reviewed editorial return QA");
    const auto root=JsonObject::Parse(winrt::to_hstring(intermediate.raw_json));
    const auto layout=root.GetNamedObject(L"stateLayout");const auto proofs=layout.GetNamedArray(L"editorial");
    for(const auto& plan:editorial_return_cases){
        const auto at=curriculum::stable_block_position(intermediate,plan.identity);
        if(!at||at->page!=6)throw std::runtime_error("An editorial return paragraph lost its stable page");
        const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
            .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block));
        if(compile_check_test_fingerprint(block)!=plan.before||block.GetNamedString(L"type")!=L"paragraph")
            throw std::runtime_error("The actual intermediate paragraph differs from its independent full-payload pin");
        const auto previous=JsonObject::Parse(block.Stringify());
        auto text=winrt::to_string(block.GetNamedString(L"text"));const auto start=text.find(plan.marked);
        if(start==std::string::npos||text.find(plan.marked,start+1)!=std::string::npos)
            throw std::runtime_error("The four designated output numbers are no longer unique");
        text.replace(start,std::string_view(plan.marked).size(),plan.plain);
        block.Insert(L"text",JsonValue::CreateStringValue(winrt::to_hstring(text)));
        if(compile_check_test_fingerprint(block)!=plan.after)
            throw std::runtime_error("The proposed return changed more than the reviewed numeric delimiters");
        // Revision zero is the first before-node for THIS identity, not an
        // index into another identity's or the global editorial chain.
        bool first=true,root_matches=false;
        for(const auto& value:proofs){const auto proof=value.GetObject();
            if(winrt::to_string(proof.GetNamedString(L"id"))==plan.identity&&first){
                root_matches=winrt::to_string(proof.GetNamedString(L"beforeSha256"))==plan.after;first=false;}}
        if(!root_matches)throw std::runtime_error("The reviewed prior presentation is not the authenticated per-identity root");
        JsonObject returned,proof;returned.Insert(L"revision",JsonValue::CreateNumberValue(0));
        returned.Insert(L"historySha256",JsonValue::CreateStringValue(winrt::to_hstring(editorial_return_history_hash(root,proofs))));
        returned.Insert(L"previousBlock",previous);
        proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(plan.identity)));
        proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(plan.before)));
        proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(plan.after)));
        proof.Insert(L"returnTo",returned);proofs.Append(proof);
    }
    return winrt::to_string(root.Stringify());
}
struct EditorialReturnFixture {
    curriculum::Lesson original,intermediate,returned;
    std::filesystem::path source,original_archive,intermediate_archive;
    std::vector<std::uint8_t> source_bytes;
};
inline EditorialReturnFixture editorial_return_fixture(){
    EditorialReturnFixture result;result.source=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_estado_asignacion_y_copia.json";
    result.source_bytes=state::read_bytes(result.source);result.returned=windows::load_lesson(result.source);
    std::array<wchar_t,32768> executable{};const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!count||count>=executable.size())throw std::runtime_error("Cannot locate the authenticated editorial return QA export");
    const auto archive=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()/"test-fixtures/editorial-return-1fe0975";
    result.original_archive=archive/"lesson_estado_asignacion_y_copia.json";
    result.intermediate_archive=archive/editorial_return_intermediate_name;
    // Exact Git object 1fe097550c458d9267113d2b2409863d21c75d99, including
    // its unrelated contemporary prose and its complete historical metadata.
    result.original=editorial_return_historical_lesson(result.original_archive,false);result.original.source_path=result.source;
    // B is the last complete publication before these returns, including the
    // preceding turnos editorial correction. Older 22443d8 lacks that edge.
    // Exact Git object df2f9fa44737f857ffbe06fadb4843110b06d579.
    result.intermediate=editorial_return_historical_lesson(result.intermediate_archive,true);result.intermediate.source_path=result.source;
    if(result.original.id!=result.intermediate.id||result.original.state_layout.lineage!=result.intermediate.state_layout.lineage
        ||result.returned.id!=result.intermediate.id||result.returned.state_layout.lineage!=result.intermediate.state_layout.lineage)
        throw std::runtime_error("Editorial return A/B/live C no longer share their immutable state family");
    for(const auto& plan:editorial_return_cases){const auto at=curriculum::stable_block_position(result.original,plan.identity);
        if(!at||compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(
            winrt::to_hstring(result.original.pages[at->page].blocks[at->block].raw_json)))!=plan.after)
            throw std::runtime_error("The genuine historical A does not contain the independently pinned return presentation");}
    const auto expected=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(editorial_return_json(result.intermediate)));
    const auto integrated=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(result.returned.raw_json));
    if(compile_check_test_fingerprint(expected)!=compile_check_test_fingerprint(integrated))
        throw std::runtime_error("The integrated live C differs from the independently reviewed B-to-C return");
    if(result.original.raw_json==result.intermediate.raw_json||result.intermediate.raw_json==result.returned.raw_json)
        throw std::runtime_error("Editorial return QA must exercise three distinct publications, not a no-op");
    return result;
}
} // namespace lgaed::test
