#pragma once
// Test-only separation of the actual published predecessor from an adopted
// lesson. Neither an active root nor a student profile is modified by QA.
#include "platform/windows/content_loader.hpp"
#include "platform/windows/published_adoption_test_source.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"

namespace lgaed::qa {
inline curriculum::Lesson load_adoption_predecessor(const std::filesystem::path& path) {
    windows::ensure_winrt_apartment();
    const auto root=std::filesystem::path(build::source_root);
    const auto relative=path.lexically_normal().lexically_relative(root.lexically_normal());
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")
        throw std::runtime_error("Adoption QA lesson is outside the pinned repository");
    return windows::parse_lesson_json(
        published_adoption_source(root,relative.generic_string()),"es");
}

// Compare the actual declared goal to the reviewed projection independently
// of JSON object member order. Preserve arrays and all primitive values.
inline bool same_adoption_json(
        const winrt::Windows::Data::Json::IJsonValue& a,
        const winrt::Windows::Data::Json::IJsonValue& b) {
    using namespace winrt::Windows::Data::Json;
    if(a.ValueType()!=b.ValueType())return false;
    switch(a.ValueType()) {
    case JsonValueType::Object: {
        const auto left=a.GetObject(),right=b.GetObject();
        if(left.Size()!=right.Size())return false;
        for(const auto& entry:left)
            if(!right.HasKey(entry.Key())||
                    !same_adoption_json(entry.Value(),right.GetNamedValue(entry.Key())))
                return false;
        return true;
    }
    case JsonValueType::Array: {
        const auto left=a.GetArray(),right=b.GetArray();
        if(left.Size()!=right.Size())return false;
        for(std::uint32_t i=0;i<left.Size();++i)
            if(!same_adoption_json(left.GetAt(i),right.GetAt(i)))return false;
        return true;
    }
    case JsonValueType::String:return a.GetString()==b.GetString();
    case JsonValueType::Number:return a.GetNumber()==b.GetNumber();
    case JsonValueType::Boolean:return a.GetBoolean()==b.GetBoolean();
    case JsonValueType::Null:return true;
    default:return false;
    }
}

inline std::string adopted_or_projected_source(const curriculum::Lesson& live,
        std::string projected,std::wstring_view field) {
    using namespace winrt::Windows::Data::Json;
    const auto name=winrt::hstring(field);
    const auto actual=JsonObject::Parse(winrt::to_hstring(live.raw_json));
    const auto expected=JsonObject::Parse(winrt::to_hstring(projected));
    if(!expected.HasKey(name))
        throw std::runtime_error("The historical QA projection lacks the reviewed root");
    if(!actual.HasKey(name))return projected;
    if(!same_adoption_json(actual.GetNamedValue(name),expected.GetNamedValue(name)))
        throw std::runtime_error("The actual published adoption differs from the reviewed root: "+
            winrt::to_string(name));
    return live.raw_json;
}
} // namespace lgaed::qa
