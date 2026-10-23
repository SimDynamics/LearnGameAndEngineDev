#pragma once
#include "core/lesson_state.hpp"
#include <windows.h>

namespace lgaed::presentation {
inline std::string interface_language(const state::StateData& data){
    const auto selected=state::string_value(data,"native_windows","ui_language","auto");
    if(selected=="es"||selected=="en"||selected=="fr")return selected;
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if(GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH)){
        const std::wstring_view tag(name);
        if(tag.starts_with(L"es"))return "es";
        if(tag.starts_with(L"fr"))return "fr";
    }
    return "en";
}
// One application-language choice drives controls, curriculum and the initial
// reading language. Voice identities for every language remain untouched.
inline void apply_application_language(state::StateData& data,std::string_view selection){
    if(selection!="auto"&&selection!="es"&&selection!="en"&&selection!="fr")return;
    data.preferences["native_windows"]["ui_language"]=std::string(selection);
    const auto resolved=interface_language(data);
    data.preferences["native_windows"]["content_language"]=resolved;
    const auto previous=state::string_value(data,"lesson_narration_settings","language");
    data.preferences["lesson_narration_settings"]["language"]=resolved=="en"?(previous=="en-GB"?std::string("en-GB"):std::string("en-US")):resolved;
}
}
